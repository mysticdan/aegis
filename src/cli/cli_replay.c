#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cjson/cJSON.h>

#include "aegis/cli_command.h"
#include "aegis/trace_reader.h"

static int event_selected(
    const CliOptions *options,
    const cJSON *event
)
{
    cJSON *step = cJSON_GetObjectItemCaseSensitive(event, "step");
    cJSON *type = cJSON_GetObjectItemCaseSensitive(event, "type");
    int value = cJSON_IsNumber(step) ? step->valueint : 0;
    const char *mode = options->replay_mode
        ? options->replay_mode
        : options->mode;

    if (options->has_from_step && value < options->from_step) {
        return 0;
    }
    if (options->has_to_step && value > options->to_step) {
        return 0;
    }
    if (!mode || strcmp(mode, "timeline") == 0 ||
        strcmp(mode, "dry-run") == 0 ||
        strcmp(mode, "compare") == 0) {
        return 1;
    }
    if (!cJSON_IsString(type)) {
        return 0;
    }
    if (strcmp(mode, "tools-only") == 0) {
        return strstr(type->valuestring, "tool") != NULL;
    }
    if (strcmp(mode, "policy-only") == 0) {
        return strstr(type->valuestring, "policy") != NULL ||
            strstr(type->valuestring, "approval") != NULL ||
            strcmp(type->valuestring, "tool_call") == 0;
    }
    return 0;
}

static int events_equivalent(const cJSON *left, const cJSON *right)
{
    cJSON *left_copy = cJSON_Duplicate(left, 1);
    cJSON *right_copy = cJSON_Duplicate(right, 1);
    char *left_text;
    char *right_text;
    int equivalent;

    if (!left_copy || !right_copy) {
        cJSON_Delete(left_copy);
        cJSON_Delete(right_copy);
        return 0;
    }
    cJSON_DeleteItemFromObjectCaseSensitive(left_copy, "sequence");
    cJSON_DeleteItemFromObjectCaseSensitive(left_copy, "timestamp_ms");
    cJSON_DeleteItemFromObjectCaseSensitive(left_copy, "session_id");
    cJSON_DeleteItemFromObjectCaseSensitive(right_copy, "sequence");
    cJSON_DeleteItemFromObjectCaseSensitive(right_copy, "timestamp_ms");
    cJSON_DeleteItemFromObjectCaseSensitive(right_copy, "session_id");
    left_text = cJSON_PrintUnformatted(left_copy);
    right_text = cJSON_PrintUnformatted(right_copy);
    equivalent = left_text && right_text &&
        strcmp(left_text, right_text) == 0;
    cJSON_free(left_text);
    cJSON_free(right_text);
    cJSON_Delete(left_copy);
    cJSON_Delete(right_copy);
    return equivalent;
}

static int print_compare(
    const CliOptions *options,
    const AegisTraceDocument *left
)
{
    AegisTraceDocument right;
    AegisStatus status;
    size_t common;
    size_t changed = 0U;
    size_t index;

    if (!options->against) {
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "replay compare requires --against <trace>");
    }
    status = aegis_trace_document_load(options->against, &right);
    if (status != AEGIS_OK) {
        return cli_error(
            options, AEGIS_CLI_EXIT_TRACE,
            "failed to load comparison trace");
    }
    common = left->count < right.count ? left->count : right.count;
    for (index = 0U; index < common; ++index) {
        if (!events_equivalent(left->events[index], right.events[index])) {
            ++changed;
        }
    }
    changed += left->count > common
        ? left->count - common
        : right.count - common;
    if (options->json) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "status", "success");
        cJSON_AddStringToObject(root, "command", "replay");
        cJSON_AddStringToObject(root, "mode", "compare");
        cJSON_AddNumberToObject(root, "left_events", (double)left->count);
        cJSON_AddNumberToObject(root, "right_events", (double)right.count);
        cJSON_AddNumberToObject(
            root,
            "event_delta",
            (double)((long long)left->count - (long long)right.count)
        );
        cJSON_AddNumberToObject(root, "changed_events", (double)changed);
        cli_json_print(root);
        cJSON_Delete(root);
    } else {
        printf("Left events  : %zu\n", left->count);
        printf("Right events : %zu\n", right.count);
        printf(
            "Delta        : %lld\n",
            (long long)left->count - (long long)right.count
        );
        printf("Changed      : %zu\n", changed);
    }
    aegis_trace_document_clear(&right);
    return AEGIS_CLI_EXIT_SUCCESS;
}

int aegis_cli_cmd_replay(const CliOptions *options)
{
    const char *path = options->trace;
    const char *mode;
    AegisTraceDocument document;
    AegisStatus status;
    size_t index;

    if (!path && options->positional_count == 1U) {
        path = options->positionals[0];
    } else if (options->positional_count > 0U) {
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "usage: aegis replay --trace <path>");
    }
    if (!path) {
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE, "missing trace path");
    }
    mode = options->replay_mode ? options->replay_mode : options->mode;
    if (mode && strcmp(mode, "timeline") != 0 &&
        strcmp(mode, "dry-run") != 0 &&
        strcmp(mode, "compare") != 0 &&
        strcmp(mode, "tools-only") != 0 &&
        strcmp(mode, "policy-only") != 0) {
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE, "invalid replay mode: %s", mode);
    }
    status = aegis_trace_document_load(path, &document);
    if (status != AEGIS_OK) {
        return cli_error(
            options, AEGIS_CLI_EXIT_TRACE, "invalid trace: %s", path);
    }
    if (mode && strcmp(mode, "compare") == 0) {
        int result = print_compare(options, &document);
        aegis_trace_document_clear(&document);
        return result;
    }
    if (options->json) {
        cJSON *root = cJSON_CreateObject();
        cJSON *events = cJSON_CreateArray();
        cJSON_AddStringToObject(root, "status", "success");
        cJSON_AddStringToObject(root, "command", "replay");
        cJSON_AddStringToObject(root, "mode", mode ? mode : "timeline");
        cJSON_AddStringToObject(root, "trace", path);
        cJSON_AddItemToObject(root, "events", events);
        for (index = 0U; index < document.count; ++index) {
            if (event_selected(options, document.events[index])) {
                cJSON_AddItemToArray(
                    events, cJSON_Duplicate(document.events[index], 1));
            }
        }
        cli_json_print(root);
        cJSON_Delete(root);
    } else {
        printf("Replay: %s\n\n", path);
        for (index = 0U; index < document.count; ++index) {
            cJSON *event = document.events[index];
            cJSON *step;
            cJSON *type;
            cJSON *payload;
            char *payload_text;

            if (!event_selected(options, event)) {
                continue;
            }
            step = cJSON_GetObjectItemCaseSensitive(event, "step");
            type = cJSON_GetObjectItemCaseSensitive(event, "type");
            payload = cJSON_GetObjectItemCaseSensitive(event, "payload");
            payload_text = payload
                ? cJSON_PrintUnformatted(payload)
                : NULL;
            printf(
                "[%d] %s\n    %s\n",
                cJSON_IsNumber(step) ? step->valueint : 0,
                cJSON_IsString(type) ? type->valuestring : "unknown",
                payload_text ? payload_text : "{}"
            );
            cJSON_free(payload_text);
        }
    }
    aegis_trace_document_clear(&document);
    return AEGIS_CLI_EXIT_SUCCESS;
}

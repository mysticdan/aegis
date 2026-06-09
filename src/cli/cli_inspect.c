#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cjson/cJSON.h>

#include "aegis/cli_command.h"
#include "aegis/state.h"
#include "aegis/trace_reader.h"

static int resolve_session_trace(
    const CliOptions *options,
    char *trace_path,
    size_t trace_path_size
)
{
    CliEnvironment environment;
    AegisState state;
    AegisSessionRecord record;
    char error[AEGIS_CLI_ERROR_MAX];
    char state_path[AEGIS_CONFIG_PATH_MAX * 2U];
    int exit_code;

    memset(&environment, 0, sizeof(environment));
    memset(&state, 0, sizeof(state));
    memset(&record, 0, sizeof(record));
    exit_code = cli_load_environment(
        options, &environment, error, sizeof(error));
    if (exit_code != AEGIS_CLI_EXIT_SUCCESS) {
        return cli_error(options, exit_code, "%s", error);
    }
    if (environment.config.state_path[0] == '/') {
        snprintf(
            state_path, sizeof(state_path), "%s",
            environment.config.state_path);
    } else if (!cli_join_path(
            state_path,
            sizeof(state_path),
            environment.workspace,
            environment.config.state_path)) {
        cli_environment_clear(&environment);
        return cli_error(
            options, AEGIS_CLI_EXIT_STATE, "state path is too long");
    }
    if (aegis_state_open(&state, state_path) != AEGIS_OK ||
        aegis_state_get_session(&state, options->session, &record) !=
            AEGIS_OK) {
        aegis_state_close(&state);
        cli_environment_clear(&environment);
        return cli_error(
            options, AEGIS_CLI_EXIT_STATE, "session not found");
    }
    if (strlen(record.trace_path) >= trace_path_size) {
        exit_code = cli_error(
            options, AEGIS_CLI_EXIT_TRACE, "trace path is too long");
    } else {
        memcpy(trace_path, record.trace_path, strlen(record.trace_path) + 1U);
        exit_code = AEGIS_CLI_EXIT_SUCCESS;
    }
    aegis_session_record_clear(&record);
    aegis_state_close(&state);
    cli_environment_clear(&environment);
    return exit_code;
}

static int inspect_event_selected(
    const CliOptions *options,
    const cJSON *event
)
{
    cJSON *step = cJSON_GetObjectItemCaseSensitive(event, "step");
    cJSON *type = cJSON_GetObjectItemCaseSensitive(event, "type");
    int selected = !options->has_step ||
        (cJSON_IsNumber(step) && step->valueint == options->step);

    if (selected && options->tools_only) {
        selected = cJSON_IsString(type) &&
            strstr(type->valuestring, "tool") != NULL;
    }
    if (selected && options->policy_only) {
        selected = cJSON_IsString(type) &&
            (strstr(type->valuestring, "policy") != NULL ||
             strstr(type->valuestring, "approval") != NULL ||
             strcmp(type->valuestring, "tool_call") == 0);
    }
    return selected;
}

int aegis_cli_cmd_inspect(const CliOptions *options)
{
    char session_trace[AEGIS_CONFIG_PATH_MAX * 2U];
    const char *path = options->trace;
    AegisTraceDocument document;
    AegisStatus status;
    size_t index;
    size_t tool_events = 0U;
    size_t policy_events = 0U;
    int maximum_step = 0;
    const char *session_id = "";
    const char *final_status = "";

    if ((options->trace != NULL) == (options->session != NULL)) {
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "inspect requires exactly one of --trace or --session");
    }
    if (options->session) {
        int result = resolve_session_trace(
            options, session_trace, sizeof(session_trace));
        if (result != AEGIS_CLI_EXIT_SUCCESS) {
            return result;
        }
        path = session_trace;
    }
    status = aegis_trace_document_load(path, &document);
    if (status != AEGIS_OK) {
        return cli_error(
            options, AEGIS_CLI_EXIT_TRACE, "invalid trace: %s", path);
    }
    for (index = 0U; index < document.count; ++index) {
        cJSON *event = document.events[index];
        cJSON *type = cJSON_GetObjectItemCaseSensitive(event, "type");
        cJSON *step = cJSON_GetObjectItemCaseSensitive(event, "step");
        cJSON *id = cJSON_GetObjectItemCaseSensitive(event, "session_id");
        cJSON *payload = cJSON_GetObjectItemCaseSensitive(event, "payload");

        if (cJSON_IsNumber(step) && step->valueint > maximum_step) {
            maximum_step = step->valueint;
        }
        if (cJSON_IsString(id)) {
            session_id = id->valuestring;
        }
        if (cJSON_IsString(type) && strstr(type->valuestring, "tool")) {
            ++tool_events;
        }
        if (cJSON_IsString(type) &&
            (strstr(type->valuestring, "policy") ||
             strstr(type->valuestring, "approval") ||
             strcmp(type->valuestring, "tool_call") == 0)) {
            ++policy_events;
        }
        if (cJSON_IsString(type) &&
            strcmp(type->valuestring, "session_end") == 0 &&
            cJSON_IsObject(payload)) {
            cJSON *value =
                cJSON_GetObjectItemCaseSensitive(payload, "status");
            if (cJSON_IsString(value)) {
                final_status = value->valuestring;
            }
        }
    }
    if (options->json) {
        cJSON *root = cJSON_CreateObject();
        cJSON *events = cJSON_CreateArray();
        cJSON_AddStringToObject(root, "status", "success");
        cJSON_AddStringToObject(root, "command", "inspect");
        cJSON_AddStringToObject(root, "session_id", session_id);
        cJSON_AddStringToObject(root, "session_status", final_status);
        cJSON_AddStringToObject(root, "trace", path);
        cJSON_AddNumberToObject(root, "steps", maximum_step);
        cJSON_AddNumberToObject(root, "event_count", (double)document.count);
        cJSON_AddNumberToObject(root, "tool_events", (double)tool_events);
        cJSON_AddNumberToObject(root, "policy_events", (double)policy_events);
        cJSON_AddItemToObject(root, "events", events);
        for (index = 0U; index < document.count; ++index) {
            if (inspect_event_selected(options, document.events[index])) {
                cJSON_AddItemToArray(
                    events,
                    cJSON_Duplicate(document.events[index], 1)
                );
            }
        }
        cli_json_print(root);
        cJSON_Delete(root);
    } else {
        printf("Session : %s\n", session_id);
        printf("Status  : %s\n", final_status);
        printf("Trace   : %s\n", path);
        printf("Steps   : %d\n", maximum_step);
        printf("Events  : %zu\n", document.count);
        printf("Tools   : %zu\n", tool_events);
        printf("Policy  : %zu\n", policy_events);
        for (index = 0U; index < document.count; ++index) {
            cJSON *event = document.events[index];
            cJSON *step;
            cJSON *type;
            cJSON *payload;
            char *rendered;

            if (!inspect_event_selected(options, event)) {
                continue;
            }
            step = cJSON_GetObjectItemCaseSensitive(event, "step");
            type = cJSON_GetObjectItemCaseSensitive(event, "type");
            payload = cJSON_GetObjectItemCaseSensitive(event, "payload");
            rendered = payload
                ? cJSON_PrintUnformatted(payload)
                : NULL;
            printf(
                "\nStep %d\n  Kind    : %s\n  Payload : %s\n",
                cJSON_IsNumber(step) ? step->valueint : 0,
                cJSON_IsString(type) ? type->valuestring : "unknown",
                rendered ? rendered : "{}"
            );
            cJSON_free(rendered);
        }
    }
    aegis_trace_document_clear(&document);
    return AEGIS_CLI_EXIT_SUCCESS;
}

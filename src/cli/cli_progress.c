#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <cjson/cJSON.h>

#include "aegis/cli_command.h"
#include "aegis/message.h"

#define PREVIEW_MAX 240U
#define SUMMARY_MAX 800U

/* Model-controlled strings must never inject terminal escapes. */
static void print_safe(const char *value, size_t limit)
{
    size_t i;
    if (!value) {
        fputs("(null)", stderr);
        return;
    }
    for (i = 0U; value[i] && i < limit; ++i) {
        unsigned char ch = (unsigned char)value[i];
        fputc(ch < 32U || ch == 127U ? ' ' : ch, stderr);
    }
    if (value[i]) {
        fputs("... [truncated]", stderr);
    }
}

static int contains_ci(const char *value, const char *needle)
{
    size_t i, j, length, size;
    if (!value || !needle) {
        return 0;
    }
    length = strlen(needle);
    size = strlen(value);
    if (length > size) {
        return 0;
    }
    for (i = 0U; i <= size - length; ++i) {
        for (j = 0U; j < length; ++j) {
            if (tolower((unsigned char)value[i+j]) !=
                tolower((unsigned char)needle[j])) {
                break;
            }
        }
        if (j == length) {
            return 1;
        }
    }
    return 0;
}

static int secret_key(const char *key)
{
    return !key ||
        strcmp(key, "key") == 0 ||
        contains_ci(key, "_key") ||
        contains_ci(key, "password") ||
        contains_ci(key, "secret") ||
        contains_ci(key, "token") ||
        contains_ci(key, "credential") ||
        contains_ci(key, "authorization") ||
        contains_ci(key, "cookie") ||
        contains_ci(key, "apikey") ||
        contains_ci(key, "private_key");
}

static int secret_value(const char *value)
{
    return contains_ci(value, "password=") ||
        contains_ci(value, "token=") ||
        contains_ci(value, "api_key=") ||
        contains_ci(value, "secret=") ||
        contains_ci(value, "authorization:") ||
        contains_ci(value, "bearer ") ||
        contains_ci(value, "--password") ||
        contains_ci(value, "--token") ||
        contains_ci(value, "--api-key");
}

static void argument_preview(const char *key, const cJSON *value)
{
    const char *str = cJSON_IsString(value) ? value->valuestring : NULL;
    fputs("    ", stderr);
    print_safe(key, 64U);
    fputs(": ", stderr);
    if (secret_key(key)) {
        fputs("[REDACTED]", stderr);
    } else if (contains_ci(key, "content") ||
               contains_ci(key, "body") ||
               contains_ci(key, "payload")) {
        if (str) {
            fprintf(stderr, "<%zu bytes; body hidden>", strlen(str));
        } else {
            fputs("<body hidden>", stderr);
        }
    } else if (str) {
        if (secret_value(str)) {
            fputs("[REDACTED: sensitive value]", stderr);
        } else if (contains_ci(key, "url")) {
            const char *end = strpbrk(str, "?#");
            const char *at = strchr(str, '@');
            if (at && (!end || at < end)) {
                fputs("[REDACTED: possible URL credentials]", stderr);
            } else if (end) {
                size_t n = (size_t)(end - str);
                size_t i;
                for (i = 0U; i < n && i < PREVIEW_MAX; ++i) {
                    unsigned char ch = (unsigned char)str[i];
                    fputc(ch < 32U || ch == 127U ? ' ' : ch, stderr);
                }
                fputs("?[query/fragment hidden]", stderr);
            } else {
                print_safe(str, PREVIEW_MAX);
            }
        } else {
            print_safe(str, PREVIEW_MAX);
        }
    } else if (cJSON_IsNumber(value)) {
        fprintf(stderr, "%g", value->valuedouble);
    } else if (cJSON_IsBool(value)) {
        fputs(cJSON_IsTrue(value) ? "true" : "false", stderr);
    } else if (cJSON_IsNull(value)) {
        fputs("null", stderr);
    } else {
        fputs("<complex value hidden>", stderr);
    }
    fputc('\n', stderr);
}

static void print_tool_arguments(const char *payload)
{
    cJSON *root;
    const cJSON *args, *item;
    size_t count = 0U;
    if (!payload) {
        fputs("    arguments: unavailable\n", stderr);
        return;
    }
    root = cJSON_Parse(payload);
    args = cJSON_GetObjectItemCaseSensitive(root, "arguments");
    if (!cJSON_IsObject(args)) {
        fputs("    arguments: unavailable\n", stderr);
        cJSON_Delete(root);
        return;
    }
    cJSON_ArrayForEach(item, args) {
        if (count++ >= 16U) {
            fputs("    ... [more arguments hidden]\n", stderr);
            break;
        }
        argument_preview(item->string, item);
    }
    cJSON_Delete(root);
}

void cli_agent_progress(void *userdata, const AegisProgressEvent *event)
{
    const CliOptions *options = (const CliOptions *)userdata;
    int approval;
    if (!event) {
        return;
    }
    approval = event->kind == AEGIS_PROGRESS_TOOL_CALL &&
        event->decision &&
        strcmp(event->decision, "require_approval") == 0;
    /* Keep --json stdout machine-readable; prompt details go to stderr. */
    if (options && (options->json || options->quiet) && !approval) {
        return;
    }
    switch (event->kind) {
        case AEGIS_PROGRESS_MODEL_REQUEST:
            fprintf(stderr, "[step %d] Thinking: waiting for model", event->step);
            if (event->detail) {
                fputs(" (", stderr);
                print_safe(event->detail, 80U);
                fputc(')', stderr);
            }
            fputs("...\n", stderr);
            break;
        case AEGIS_PROGRESS_MODEL_RESPONSE:
            fprintf(stderr, "[step %d] Model responded (%ld ms)\n",
                    event->step, event->duration_ms);
            break;
        case AEGIS_PROGRESS_REASONING_SUMMARY:
            fprintf(stderr, "[step %d] Provider reasoning summary: ", event->step);
            print_safe(event->detail, SUMMARY_MAX);
            fputc('\n', stderr);
            break;
        case AEGIS_PROGRESS_MODEL_ERROR:
            fprintf(stderr, "[step %d] Model request failed\n", event->step);
            break;
        case AEGIS_PROGRESS_INVALID_RESPONSE:
            fprintf(stderr, "[step %d] Invalid action; repairing response\n", event->step);
            break;
        case AEGIS_PROGRESS_TOOL_CALL:
            fprintf(stderr, "[step %d] Agent requests tool: ", event->step);
            print_safe(event->tool, 80U);
            fputs(" (risk=", stderr);
            print_safe(event->risk, 20U);
            fputs(", policy=", stderr);
            print_safe(event->decision, 40U);
            fputs(")\n", stderr);
            print_tool_arguments(event->payload_json);
            break;
        case AEGIS_PROGRESS_APPROVAL:
            fprintf(stderr, "[step %d] Permission: %s (%s)\n",
                    event->step,
                    event->success ? "granted" : "denied",
                    event->detail ? event->detail : "manual");
            break;
        case AEGIS_PROGRESS_TOOL_RESULT:
            fprintf(stderr, "[step %d] Tool ", event->step);
            print_safe(event->tool, 80U);
            fprintf(stderr, ": %s (exit=%d, bytes=%zu, %ld ms)\n",
                    event->success ? "OK" : "FAILED",
                    event->exit_code, event->output_bytes, event->duration_ms);
            break;
        default:
            break;
    }
    fflush(stderr);
}

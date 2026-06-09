#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <curl/curl.h>
#include <cjson/cJSON.h>

#include "aegis/agent.h"
#include "aegis/config.h"
#include "aegis/runtime.h"
#include "aegis/session.h"

struct AegisRuntime {
    AegisConfig config;
};

static long long unix_milliseconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        return 0;
    }
    return (long long)now.tv_sec * 1000LL + now.tv_nsec / 1000000L;
}

static int join_workspace_path(
    char *output,
    size_t size,
    const char *workspace,
    const char *path
)
{
    int written;

    if (!output || !workspace || !path) {
        return 0;
    }
    if (path[0] == '/') {
        written = snprintf(output, size, "%s", path);
    } else {
        written = snprintf(output, size, "%s/%s", workspace, path);
    }
    return written >= 0 && (size_t)written < size;
}

static int safe_runtime_path(
    char *output,
    size_t size,
    const char *workspace,
    const char *path
)
{
    const char *cursor;
    size_t workspace_length;

    if (!output || !workspace || !path || !path[0]) {
        return 0;
    }
    cursor = path;
    while (*cursor) {
        const char *end;
        size_t length;

        while (*cursor == '/') {
            ++cursor;
        }
        end = strchr(cursor, '/');
        length = end ? (size_t)(end - cursor) : strlen(cursor);
        if (length == 2U &&
            cursor[0] == '.' && cursor[1] == '.') {
            return 0;
        }
        cursor = end ? end : cursor + length;
    }
    if (!join_workspace_path(output, size, workspace, path)) {
        return 0;
    }
    workspace_length = strlen(workspace);
    return strncmp(output, workspace, workspace_length) == 0 &&
        (output[workspace_length] == '/' ||
         output[workspace_length] == '\0');
}

static int ensure_directory_tree(const char *path)
{
    char copy[4096];
    char *cursor;

    if (!path || !path[0] || strlen(path) >= sizeof(copy)) {
        return 0;
    }
    memcpy(copy, path, strlen(path) + 1U);
    for (cursor = copy + 1; *cursor; ++cursor) {
        if (*cursor != '/') {
            continue;
        }
        *cursor = '\0';
        if (mkdir(copy, 0755) != 0 && errno != EEXIST) {
            return 0;
        }
        *cursor = '/';
    }
    if (mkdir(copy, 0755) != 0 && errno != EEXIST) {
        return 0;
    }
    return 1;
}

static int ensure_parent_directory(const char *path)
{
    char copy[4096];
    char *separator;

    if (!path || strlen(path) >= sizeof(copy)) {
        return 0;
    }
    memcpy(copy, path, strlen(path) + 1U);
    separator = strrchr(copy, '/');
    if (!separator) {
        return 1;
    }
    if (separator == copy) {
        return 1;
    }
    *separator = '\0';
    return ensure_directory_tree(copy);
}

AegisRuntime *aegis_runtime_new(const char *config_path)
{
    AegisRuntime *runtime;

    if (!config_path) {
        return NULL;
    }
    runtime = calloc(1U, sizeof(*runtime));
    if (!runtime) {
        return NULL;
    }
    if (aegis_config_load_json(config_path, &runtime->config) != AEGIS_OK) {
        free(runtime);
        return NULL;
    }
    (void)curl_global_init(CURL_GLOBAL_DEFAULT);
    return runtime;
}

AegisRuntime *aegis_runtime_new_with_config(const AegisConfig *config)
{
    AegisRuntime *runtime;

    if (!config) {
        return NULL;
    }
    runtime = calloc(1U, sizeof(*runtime));
    if (!runtime) {
        return NULL;
    }
    runtime->config = *config;
    (void)curl_global_init(CURL_GLOBAL_DEFAULT);
    return runtime;
}

void aegis_runtime_free(AegisRuntime *runtime)
{
    free(runtime);
}

AegisStatus aegis_runtime_handle_message(
    AegisRuntime *runtime,
    const AegisMessage *message,
    AegisResponse *response
)
{
    const char *workspace;
    AegisState state;
    AegisTrace trace;
    AegisSessionRecord record;
    AegisSessionRecord previous;
    AegisStatus status;
    char state_path[4096];
    char trace_directory[4096];
    char trace_path[4096];
    int written;
    long long now;

    if (!runtime || !response || !aegis_message_is_valid(message) ||
        !aegis_session_id_is_valid(message->session_id)) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    workspace = message->workspace && message->workspace[0]
        ? message->workspace
        : runtime->config.workspace_root;
    memset(&state, 0, sizeof(state));
    memset(&trace, 0, sizeof(trace));
    memset(&record, 0, sizeof(record));
    memset(&previous, 0, sizeof(previous));
    if (!safe_runtime_path(
            state_path,
            sizeof(state_path),
            workspace,
            runtime->config.state_path) ||
        !safe_runtime_path(
            trace_directory,
            sizeof(trace_directory),
            workspace,
            runtime->config.trace_directory)) {
        return AEGIS_ERR_PATH_ESCAPE;
    }
    if (message->trace_path && message->trace_path[0]) {
        if (!safe_runtime_path(
                trace_path,
                sizeof(trace_path),
                workspace,
                message->trace_path)) {
            return AEGIS_ERR_PATH_ESCAPE;
        }
    } else {
        written = snprintf(
            trace_path,
            sizeof(trace_path),
            "%s/%s.jsonl",
            trace_directory,
            message->session_id
        );
        if (written < 0 || (size_t)written >= sizeof(trace_path)) {
            return AEGIS_ERR_PATH_ESCAPE;
        }
    }

    if (runtime->config.state_enabled) {
        if (!ensure_parent_directory(state_path) ||
            aegis_state_open(&state, state_path) != AEGIS_OK) {
            return AEGIS_ERR_STATE;
        }
    }
    if (runtime->config.trace_enabled) {
        if (!ensure_parent_directory(trace_path) ||
            aegis_trace_open(&trace, trace_path) != AEGIS_OK) {
            aegis_state_close(&state);
            return AEGIS_ERR_IO;
        }
        aegis_trace_set_redaction(
            &trace,
            runtime->config.redact_secrets,
            runtime->config.api_key_env[0]
                ? getenv(runtime->config.api_key_env)
                : NULL
        );
    }

    now = unix_milliseconds();
    snprintf(record.id, sizeof(record.id), "%s", message->session_id);
    snprintf(record.status, sizeof(record.status), "running");
    snprintf(
        record.profile,
        sizeof(record.profile),
        "%s",
        runtime->config.active_profile.id
    );
    snprintf(record.workspace, sizeof(record.workspace), "%s", workspace);
    snprintf(record.trace_path, sizeof(record.trace_path), "%s", trace_path);
    record.task = (char *)message->text;
    record.created_ms = now;
    if (state.database && message->initial_step > 0 &&
        aegis_state_get_session(
            &state, message->session_id, &previous) == AEGIS_OK) {
        record.created_ms = previous.created_ms;
        record.task = previous.task;
    }
    record.updated_ms = now;
    if (state.database &&
        aegis_state_upsert_session(&state, &record) != AEGIS_OK) {
        aegis_trace_close(&trace);
        aegis_state_close(&state);
        aegis_session_record_clear(&previous);
        return AEGIS_ERR_STATE;
    }
    if (trace.file) {
        cJSON *task_json = cJSON_CreateString(message->text);
        char *task_payload = task_json
            ? cJSON_PrintUnformatted(task_json)
            : NULL;
        (void)aegis_trace_event(
            &trace,
            message->session_id,
            message->initial_step,
            "session_start",
            task_payload ? task_payload : "\"\""
        );
        cJSON_free(task_payload);
        cJSON_Delete(task_json);
    }

    status = aegis_agent_run(
        &runtime->config,
        message,
        state.database ? &state : NULL,
        trace.file ? &trace : NULL,
        response
    );
    snprintf(response->session_id, sizeof(response->session_id), "%s",
             message->session_id);
    snprintf(response->trace_path, sizeof(response->trace_path), "%s",
             trace_path);
    if (!response->status[0]) {
        const char *session_status = "failed";

        if (status == AEGIS_OK) {
            session_status = "success";
        } else if (status == AEGIS_ERR_APPROVAL_REJECTED) {
            session_status = "waiting_approval";
        } else if (status == AEGIS_ERR_MAX_STEPS) {
            session_status = "max_steps";
        } else if (status == AEGIS_ERR_INTERRUPTED) {
            session_status = "cancelled";
        }
        snprintf(
            response->status,
            sizeof(response->status),
            "%s",
            session_status
        );
    }
    record.final_text = response->text;
    record.steps = response->steps;
    record.updated_ms = unix_milliseconds();
    snprintf(record.status, sizeof(record.status), "%s", response->status);
    if (state.database) {
        (void)aegis_state_upsert_session(&state, &record);
    }
    if (trace.file) {
        char payload[128];
        snprintf(
            payload,
            sizeof(payload),
            "{\"status\":\"%s\",\"result\":\"%s\"}",
            response->status,
            aegis_status_string(status)
        );
        (void)aegis_trace_event(
            &trace,
            message->session_id,
            response->steps,
            "session_end",
            payload
        );
    }
    aegis_trace_close(&trace);
    aegis_state_close(&state);
    aegis_session_record_clear(&previous);
    return status;
}

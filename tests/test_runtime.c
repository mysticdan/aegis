#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#include "aegis/action.h"
#include "aegis/config.h"
#include "aegis/message.h"
#include "aegis/response.h"
#include "aegis/runtime.h"
#include "aegis/state.h"
#include "aegis/trace.h"
#include "aegis/trace_reader.h"

#define ASSERT_OK(expr) do { \
    AegisStatus _s = (expr); \
    if (_s != AEGIS_OK) { \
        fprintf(stderr, "FAIL:%d: %s == %d\n", __LINE__, #expr, _s); \
        abort(); \
    } \
} while (0)

#define ASSERT_ERR(expr, expected) do { \
    AegisStatus _s = (expr); \
    if (_s != (expected)) { \
        fprintf(stderr, "FAIL:%d: %s == %d (expected %d)\n", \
                __LINE__, #expr, _s, (int)(expected)); \
        abort(); \
    } \
} while (0)

#define ASSERT_NOT_NULL(expr) do { \
    void *_p = (void *)(expr); \
    if (!_p) { \
        fprintf(stderr, "FAIL:%d: %s is NULL\n", __LINE__, #expr); \
        abort(); \
    } \
} while (0)

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL:%d: %s\n", __LINE__, #expr); \
        abort(); \
    } \
} while (0)

static void test_action_parser(void)
{
    AegisAction action;

    aegis_action_init(&action);
    ASSERT_OK(aegis_action_parse(
        "{\"type\":\"final\",\"message\":\"done\"}",
        &action
    ));
    ASSERT_TRUE(action.type == AEGIS_ACTION_FINAL);
    ASSERT_TRUE(strcmp(action.message, "done") == 0);

    ASSERT_OK(aegis_action_parse(
        "{\"type\":\"tool_call\",\"tool\":\"read_file\","
        "\"arguments\":{\"path\":\"README.md\"}}",
        &action
    ));
    ASSERT_TRUE(action.type == AEGIS_ACTION_TOOL_CALL);
    ASSERT_TRUE(strcmp(action.tool, "read_file") == 0);
    ASSERT_TRUE(cJSON_IsObject(action.arguments));

    ASSERT_ERR(aegis_action_parse(
        "{\"type\":\"final\",\"message\":\"done\",\"extra\":true}",
        &action
    ), AEGIS_ERR_PARSE);
    ASSERT_ERR(aegis_action_parse(
        "{\"type\":\"tool_call\",\"tool\":\"\",\"arguments\":{}}",
        &action
    ), AEGIS_ERR_PARSE);
    ASSERT_ERR(aegis_action_parse(
        "{\"type\":\"final\",\"message\":\"done\"} trailing",
        &action
    ), AEGIS_ERR_PARSE);
    aegis_action_clear(&action);
}

static void test_state(const char *directory)
{
    char path[4096];
    AegisState state;
    AegisSessionRecord input;
    AegisSessionRecord output;
    AegisSessionRecord *records = NULL;
    size_t count = 0U;
    char *events = NULL;

    ASSERT_TRUE(snprintf(
        path, sizeof(path), "%s/state.db", directory
    ) > 0);
    memset(&state, 0, sizeof(state));
    memset(&input, 0, sizeof(input));
    memset(&output, 0, sizeof(output));
    ASSERT_OK(aegis_state_open(&state, path));
    snprintf(input.id, sizeof(input.id), "s_state_test");
    snprintf(input.status, sizeof(input.status), "running");
    snprintf(input.profile, sizeof(input.profile), "coding_agent");
    snprintf(input.workspace, sizeof(input.workspace), "%s", directory);
    snprintf(input.trace_path, sizeof(input.trace_path), "%s/trace.jsonl",
             directory);
    input.task = "state task";
    input.steps = 1;
    input.created_ms = 100;
    input.updated_ms = 200;
    ASSERT_OK(aegis_state_upsert_session(&state, &input));
    ASSERT_OK(aegis_state_add_event(
        &state, input.id, 1, "model_response", "{\"ok\":true}"
    ));
    ASSERT_OK(aegis_state_add_reminder(
        &state, input.id, "check result", "tomorrow"
    ));

    ASSERT_OK(aegis_state_get_session(
        &state, input.id, &output
    ));
    ASSERT_TRUE(strcmp(output.task, "state task") == 0);
    ASSERT_TRUE(output.steps == 1);
    aegis_session_record_clear(&output);

    ASSERT_OK(aegis_state_list_sessions(
        &state, &records, &count
    ));
    ASSERT_TRUE(count == 1U);
    ASSERT_TRUE(strcmp(records[0].id, input.id) == 0);
    aegis_session_record_clear(&records[0]);
    free(records);

    ASSERT_OK(aegis_state_session_events_json(
        &state, input.id, &events
    ));
    ASSERT_TRUE(strstr(events, "model_response") != NULL);
    free(events);

    ASSERT_OK(aegis_state_delete_session(&state, input.id));
    ASSERT_ERR(aegis_state_get_session(
        &state, input.id, &output
    ), AEGIS_ERR_NOT_FOUND);
    aegis_state_close(&state);
    unlink(path);
}

static void test_trace(const char *directory)
{
    char path[4096];
    AegisTrace trace;
    AegisTraceDocument document;
    cJSON *payload;
    cJSON *api_key;
    cJSON *authorization;
    cJSON *note;

    ASSERT_TRUE(snprintf(
        path, sizeof(path), "%s/trace.jsonl", directory
    ) > 0);
    memset(&trace, 0, sizeof(trace));
    memset(&document, 0, sizeof(document));
    ASSERT_OK(aegis_trace_open(&trace, path));
    aegis_trace_set_redaction(&trace, 1, "top-secret");
    ASSERT_OK(aegis_trace_event(
        &trace,
        "s_trace_test",
        1,
        "model_response",
        "{\"api_key\":\"visible\",\"Authorization\":\"Bearer visible\","
        "\"note\":\"token=top-secret\"}"
    ));
    aegis_trace_close(&trace);

    ASSERT_OK(aegis_trace_open(&trace, path));
    ASSERT_TRUE(trace.sequence == 1U);
    ASSERT_OK(aegis_trace_event(
        &trace, "s_trace_test", 2, "final", "{\"message\":\"done\"}"
    ));
    aegis_trace_close(&trace);

    ASSERT_OK(aegis_trace_document_load(path, &document));
    ASSERT_TRUE(document.count == 2U);
    payload = cJSON_GetObjectItemCaseSensitive(
        document.events[0], "payload");
    api_key = cJSON_GetObjectItemCaseSensitive(payload, "api_key");
    authorization = cJSON_GetObjectItemCaseSensitive(
        payload, "Authorization");
    note = cJSON_GetObjectItemCaseSensitive(payload, "note");
    ASSERT_TRUE(cJSON_IsString(api_key));
    ASSERT_TRUE(strcmp(api_key->valuestring, "[REDACTED]") == 0);
    ASSERT_TRUE(cJSON_IsString(authorization));
    ASSERT_TRUE(strcmp(authorization->valuestring, "[REDACTED]") == 0);
    ASSERT_TRUE(cJSON_IsString(note));
    ASSERT_TRUE(strstr(note->valuestring, "top-secret") == NULL);
    ASSERT_TRUE(strstr(note->valuestring, "[REDACTED]") != NULL);
    ASSERT_TRUE(cJSON_GetObjectItemCaseSensitive(
        document.events[1], "sequence")->valueint == 2);
    aegis_trace_document_clear(&document);
    unlink(path);
}

static void test_runtime(const char *directory)
{
    static const char responses[] =
        "[{\"type\":\"tool_call\",\"tool\":\"write_file\","
        "\"arguments\":{\"path\":\"runtime-created.txt\","
        "\"content\":\"from runtime\"}},"
        "{\"type\":\"final\",\"message\":\"runtime complete\"}]";
    AegisConfig config;
    AegisRuntime *runtime;
    AegisMessage message;
    AegisResponse response;
    AegisState state;
    AegisSessionRecord record;
    AegisTraceDocument trace;
    char state_path[4096];
    char trace_path[4096];
    char created_path[4096];
    FILE *created;
    char content[32];

    ASSERT_OK(aegis_config_load_preset("aegis", &config));
    ASSERT_OK(aegis_config_select_provider(&config, "mock"));
    snprintf(config.state_path, sizeof(config.state_path), "runtime/state.db");
    snprintf(
        config.trace_directory,
        sizeof(config.trace_directory),
        "runtime/traces"
    );
    ASSERT_TRUE(setenv("AEGIS_MOCK_RESPONSES", responses, 1) == 0);

    runtime = aegis_runtime_new_with_config(&config);
    ASSERT_NOT_NULL(runtime);
    memset(&message, 0, sizeof(message));
    message.channel = "test";
    message.user_id = "tester";
    message.session_id = "s_runtime_test";
    message.text = "write a file";
    message.workspace = directory;
    message.profile = "coding_agent";
    message.auto_approve = 1;
    message.no_input = 1;
    aegis_response_init(&response);
    ASSERT_OK(aegis_runtime_handle_message(
        runtime, &message, &response
    ));
    ASSERT_TRUE(strcmp(response.status, "success") == 0);
    ASSERT_TRUE(strcmp(response.text, "runtime complete") == 0);
    ASSERT_TRUE(response.steps == 2);
    aegis_runtime_free(runtime);
    ASSERT_TRUE(unsetenv("AEGIS_MOCK_RESPONSES") == 0);

    ASSERT_TRUE(snprintf(
        created_path,
        sizeof(created_path),
        "%s/runtime-created.txt",
        directory
    ) > 0);
    created = fopen(created_path, "rb");
    ASSERT_NOT_NULL(created);
    ASSERT_TRUE(fread(content, 1U, sizeof(content) - 1U, created) ==
           strlen("from runtime"));
    content[strlen("from runtime")] = '\0';
    fclose(created);
    ASSERT_TRUE(strcmp(content, "from runtime") == 0);

    ASSERT_TRUE(snprintf(
        state_path, sizeof(state_path), "%s/runtime/state.db", directory
    ) > 0);
    memset(&state, 0, sizeof(state));
    memset(&record, 0, sizeof(record));
    ASSERT_OK(aegis_state_open(&state, state_path));
    ASSERT_OK(aegis_state_get_session(
        &state, message.session_id, &record
    ));
    ASSERT_TRUE(strcmp(record.status, "success") == 0);
    ASSERT_TRUE(record.steps == 2);
    aegis_session_record_clear(&record);
    aegis_state_close(&state);

    ASSERT_TRUE(snprintf(
        trace_path,
        sizeof(trace_path),
        "%s/runtime/traces/%s.jsonl",
        directory,
        message.session_id
    ) > 0);
    memset(&trace, 0, sizeof(trace));
    ASSERT_OK(aegis_trace_document_load(trace_path, &trace));
    ASSERT_TRUE(trace.count >= 8U);
    aegis_trace_document_clear(&trace);

    aegis_response_free(&response);
    unlink(created_path);
    unlink(trace_path);
    unlink(state_path);
    {
        char journal[4096];
        ASSERT_TRUE(snprintf(
            journal, sizeof(journal), "%s-wal", state_path
        ) > 0);
        unlink(journal);
        ASSERT_TRUE(snprintf(
            journal, sizeof(journal), "%s-shm", state_path
        ) > 0);
        unlink(journal);
    }
    {
        char path[4096];
        ASSERT_TRUE(snprintf(
            path, sizeof(path), "%s/runtime/traces", directory
        ) > 0);
        rmdir(path);
        ASSERT_TRUE(snprintf(path, sizeof(path), "%s/runtime", directory) > 0);
        rmdir(path);
    }
}

int main(void)
{
    char template[] = "/tmp/aegis-runtime-test-XXXXXX";
    char *directory = mkdtemp(template);

    ASSERT_NOT_NULL(directory);
    test_action_parser();
    test_state(directory);
    test_trace(directory);
    test_runtime(directory);
    rmdir(directory);
    puts("aegis runtime tests: ok");
    return 0;
}

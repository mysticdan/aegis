#define _POSIX_C_SOURCE 200809L

#include <assert.h>
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

static void test_action_parser(void)
{
    AegisAction action;

    aegis_action_init(&action);
    assert(aegis_action_parse(
        "{\"type\":\"final\",\"message\":\"done\"}",
        &action
    ) == AEGIS_OK);
    assert(action.type == AEGIS_ACTION_FINAL);
    assert(strcmp(action.message, "done") == 0);

    assert(aegis_action_parse(
        "{\"type\":\"tool_call\",\"tool\":\"read_file\","
        "\"arguments\":{\"path\":\"README.md\"}}",
        &action
    ) == AEGIS_OK);
    assert(action.type == AEGIS_ACTION_TOOL_CALL);
    assert(strcmp(action.tool, "read_file") == 0);
    assert(cJSON_IsObject(action.arguments));

    assert(aegis_action_parse(
        "{\"type\":\"final\",\"message\":\"done\",\"extra\":true}",
        &action
    ) == AEGIS_ERR_PARSE);
    assert(aegis_action_parse(
        "{\"type\":\"tool_call\",\"tool\":\"\",\"arguments\":{}}",
        &action
    ) == AEGIS_ERR_PARSE);
    assert(aegis_action_parse(
        "{\"type\":\"final\",\"message\":\"done\"} trailing",
        &action
    ) == AEGIS_ERR_PARSE);
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

    assert(snprintf(
        path, sizeof(path), "%s/state.db", directory
    ) > 0);
    memset(&state, 0, sizeof(state));
    memset(&input, 0, sizeof(input));
    memset(&output, 0, sizeof(output));
    assert(aegis_state_open(&state, path) == AEGIS_OK);
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
    assert(aegis_state_upsert_session(&state, &input) == AEGIS_OK);
    assert(aegis_state_add_event(
        &state, input.id, 1, "model_response", "{\"ok\":true}"
    ) == AEGIS_OK);
    assert(aegis_state_add_reminder(
        &state, input.id, "check result", "tomorrow"
    ) == AEGIS_OK);

    assert(aegis_state_get_session(
        &state, input.id, &output
    ) == AEGIS_OK);
    assert(strcmp(output.task, "state task") == 0);
    assert(output.steps == 1);
    aegis_session_record_clear(&output);

    assert(aegis_state_list_sessions(
        &state, &records, &count
    ) == AEGIS_OK);
    assert(count == 1U);
    assert(strcmp(records[0].id, input.id) == 0);
    aegis_session_record_clear(&records[0]);
    free(records);

    assert(aegis_state_session_events_json(
        &state, input.id, &events
    ) == AEGIS_OK);
    assert(strstr(events, "model_response") != NULL);
    free(events);

    assert(aegis_state_delete_session(&state, input.id) == AEGIS_OK);
    assert(aegis_state_get_session(
        &state, input.id, &output
    ) == AEGIS_ERR_NOT_FOUND);
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

    assert(snprintf(
        path, sizeof(path), "%s/trace.jsonl", directory
    ) > 0);
    memset(&trace, 0, sizeof(trace));
    memset(&document, 0, sizeof(document));
    assert(aegis_trace_open(&trace, path) == AEGIS_OK);
    aegis_trace_set_redaction(&trace, 1, "top-secret");
    assert(aegis_trace_event(
        &trace,
        "s_trace_test",
        1,
        "model_response",
        "{\"api_key\":\"visible\",\"Authorization\":\"Bearer visible\","
        "\"note\":\"token=top-secret\"}"
    ) == AEGIS_OK);
    aegis_trace_close(&trace);

    assert(aegis_trace_open(&trace, path) == AEGIS_OK);
    assert(trace.sequence == 1U);
    assert(aegis_trace_event(
        &trace, "s_trace_test", 2, "final", "{\"message\":\"done\"}"
    ) == AEGIS_OK);
    aegis_trace_close(&trace);

    assert(aegis_trace_document_load(path, &document) == AEGIS_OK);
    assert(document.count == 2U);
    payload = cJSON_GetObjectItemCaseSensitive(
        document.events[0], "payload");
    api_key = cJSON_GetObjectItemCaseSensitive(payload, "api_key");
    authorization = cJSON_GetObjectItemCaseSensitive(
        payload, "Authorization");
    note = cJSON_GetObjectItemCaseSensitive(payload, "note");
    assert(cJSON_IsString(api_key));
    assert(strcmp(api_key->valuestring, "[REDACTED]") == 0);
    assert(cJSON_IsString(authorization));
    assert(strcmp(authorization->valuestring, "[REDACTED]") == 0);
    assert(cJSON_IsString(note));
    assert(strstr(note->valuestring, "top-secret") == NULL);
    assert(strstr(note->valuestring, "[REDACTED]") != NULL);
    assert(cJSON_GetObjectItemCaseSensitive(
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

    assert(aegis_config_load_preset("aegis", &config) == AEGIS_OK);
    assert(aegis_config_select_provider(&config, "mock") == AEGIS_OK);
    snprintf(config.state_path, sizeof(config.state_path), "runtime/state.db");
    snprintf(
        config.trace_directory,
        sizeof(config.trace_directory),
        "runtime/traces"
    );
    assert(setenv("AEGIS_MOCK_RESPONSES", responses, 1) == 0);

    runtime = aegis_runtime_new_with_config(&config);
    assert(runtime != NULL);
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
    assert(aegis_runtime_handle_message(
        runtime, &message, &response
    ) == AEGIS_OK);
    assert(strcmp(response.status, "success") == 0);
    assert(strcmp(response.text, "runtime complete") == 0);
    assert(response.steps == 2);
    aegis_runtime_free(runtime);
    assert(unsetenv("AEGIS_MOCK_RESPONSES") == 0);

    assert(snprintf(
        created_path,
        sizeof(created_path),
        "%s/runtime-created.txt",
        directory
    ) > 0);
    created = fopen(created_path, "rb");
    assert(created != NULL);
    assert(fread(content, 1U, sizeof(content) - 1U, created) ==
           strlen("from runtime"));
    content[strlen("from runtime")] = '\0';
    fclose(created);
    assert(strcmp(content, "from runtime") == 0);

    assert(snprintf(
        state_path, sizeof(state_path), "%s/runtime/state.db", directory
    ) > 0);
    memset(&state, 0, sizeof(state));
    memset(&record, 0, sizeof(record));
    assert(aegis_state_open(&state, state_path) == AEGIS_OK);
    assert(aegis_state_get_session(
        &state, message.session_id, &record
    ) == AEGIS_OK);
    assert(strcmp(record.status, "success") == 0);
    assert(record.steps == 2);
    aegis_session_record_clear(&record);
    aegis_state_close(&state);

    assert(snprintf(
        trace_path,
        sizeof(trace_path),
        "%s/runtime/traces/%s.jsonl",
        directory,
        message.session_id
    ) > 0);
    memset(&trace, 0, sizeof(trace));
    assert(aegis_trace_document_load(trace_path, &trace) == AEGIS_OK);
    assert(trace.count >= 8U);
    aegis_trace_document_clear(&trace);

    aegis_response_free(&response);
    unlink(created_path);
    unlink(trace_path);
    unlink(state_path);
    {
        char journal[4096];
        assert(snprintf(
            journal, sizeof(journal), "%s-wal", state_path
        ) > 0);
        unlink(journal);
        assert(snprintf(
            journal, sizeof(journal), "%s-shm", state_path
        ) > 0);
        unlink(journal);
    }
    {
        char path[4096];
        assert(snprintf(
            path, sizeof(path), "%s/runtime/traces", directory
        ) > 0);
        rmdir(path);
        assert(snprintf(path, sizeof(path), "%s/runtime", directory) > 0);
        rmdir(path);
    }
}

int main(void)
{
    char template[] = "/tmp/aegis-runtime-test-XXXXXX";
    char *directory = mkdtemp(template);

    assert(directory != NULL);
    test_action_parser();
    test_state(directory);
    test_trace(directory);
    test_runtime(directory);
    rmdir(directory);
    puts("aegis runtime tests: ok");
    return 0;
}

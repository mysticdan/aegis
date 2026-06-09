#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cjson/cJSON.h>

#include "aegis/cli_command.h"
#include "aegis/message.h"
#include "aegis/response.h"
#include "aegis/runtime.h"
#include "aegis/session.h"
#include "aegis/state.h"

static int resume_ask_user(
    void *userdata,
    const char *question,
    char **answer
)
{
    char *line = NULL;
    size_t capacity = 0U;
    ssize_t length;

    (void)userdata;
    fprintf(stderr, "%s ", question);
    fflush(stderr);
    length = getline(&line, &capacity, stdin);
    if (length < 0) {
        free(line);
        return 0;
    }
    while (length > 0 &&
           (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        line[--length] = '\0';
    }
    *answer = line;
    return 1;
}

static int resume_send_message(
    void *userdata,
    const char *message
)
{
    (void)userdata;
    fprintf(stderr, "%s\n", message);
    return 1;
}

int aegis_cli_cmd_resume(const CliOptions *options)
{
    CliEnvironment environment;
    AegisState state;
    AegisSessionRecord record;
    AegisRuntime *runtime;
    AegisMessage message;
    AegisResponse response;
    const char *session_id = options->session;
    char error[AEGIS_CLI_ERROR_MAX];
    char state_path[AEGIS_CONFIG_PATH_MAX * 2U];
    char *events = NULL;
    char *task;
    size_t task_size;
    int exit_code;
    AegisStatus status;

    if (!session_id && options->positional_count == 1U) {
        session_id = options->positionals[0];
    } else if (options->positional_count > 0U) {
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "usage: aegis resume <session-id>");
    }
    if (!session_id) {
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE, "missing session id");
    }
    if (!aegis_session_id_is_valid(session_id)) {
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE, "invalid session id");
    }
    exit_code = cli_load_environment(
        options, &environment, error, sizeof(error));
    if (exit_code != AEGIS_CLI_EXIT_SUCCESS) {
        cli_environment_clear(&environment);
        return cli_error(options, exit_code, "%s", error);
    }
    exit_code = cli_confirm_agent_execution(
        options, &environment.config, 0);
    if (exit_code != AEGIS_CLI_EXIT_SUCCESS) {
        cli_environment_clear(&environment);
        return exit_code;
    }
    if (environment.config.state_path[0] == '/') {
        snprintf(
            state_path, sizeof(state_path), "%s",
            environment.config.state_path);
    } else if (!cli_join_path(
            state_path, sizeof(state_path), environment.workspace,
            environment.config.state_path)) {
        cli_environment_clear(&environment);
        return cli_error(
            options, AEGIS_CLI_EXIT_STATE, "state path is too long");
    }
    memset(&state, 0, sizeof(state));
    memset(&record, 0, sizeof(record));
    if (aegis_state_open(&state, state_path) != AEGIS_OK ||
        aegis_state_get_session(&state, session_id, &record) != AEGIS_OK) {
        aegis_state_close(&state);
        cli_environment_clear(&environment);
        return cli_error(
            options, AEGIS_CLI_EXIT_STATE, "session not found");
    }
    if (!options->profile &&
        aegis_config_load_profile(
            &environment.config,
            record.profile) != AEGIS_OK) {
        char stored_profile[AEGIS_CONFIG_NAME_MAX];

        snprintf(
            stored_profile,
            sizeof(stored_profile),
            "%s",
            record.profile
        );
        aegis_session_record_clear(&record);
        aegis_state_close(&state);
        cli_environment_clear(&environment);
        return cli_error(
            options,
            AEGIS_CLI_EXIT_PROFILE,
            "stored session profile is unavailable: %s",
            stored_profile
        );
    }
    if (options->has_max_steps) {
        if (options->max_steps > environment.config.max_steps) {
            aegis_session_record_clear(&record);
            aegis_state_close(&state);
            cli_environment_clear(&environment);
            return cli_error(
                options,
                AEGIS_CLI_EXIT_USAGE,
                "--max-steps exceeds stored profile limit"
            );
        }
        environment.config.max_steps = options->max_steps;
    }
    (void)aegis_state_session_events_json(&state, session_id, &events);
    task_size = strlen(record.task) + (events ? strlen(events) : 0U) + 128U;
    task = malloc(task_size);
    if (!task) {
        free(events);
        aegis_session_record_clear(&record);
        aegis_state_close(&state);
        cli_environment_clear(&environment);
        return cli_error(
            options, AEGIS_CLI_EXIT_GENERAL, "out of memory");
    }
    snprintf(
        task,
        task_size,
        "Resume the previous task.\nOriginal task: %s\n"
        "Previous execution events: %s\nContinue from the latest state.",
        record.task,
        events ? events : "[]"
    );
    free(events);
    memset(&message, 0, sizeof(message));
    message.channel = "cli";
    message.user_id = "local";
    message.session_id = session_id;
    message.text = task;
    message.workspace = environment.workspace;
    message.profile = environment.config.active_profile.id;
    message.trace_path = record.trace_path;
    message.auto_approve = options->yes;
    message.no_input = options->no_input ||
        (options->approval &&
         strcmp(options->approval, "never") == 0);
    if (!message.no_input) {
        message.ask_user = resume_ask_user;
        message.send_message = resume_send_message;
    }
    message.is_cancelled = cli_interrupted;
    message.initial_step = record.steps;
    aegis_response_init(&response);
    runtime = aegis_runtime_new_with_config(&environment.config);
    status = runtime
        ? aegis_runtime_handle_message(runtime, &message, &response)
        : AEGIS_ERR_RUNTIME;
    aegis_runtime_free(runtime);
    if (status == AEGIS_OK) {
        if (options->json) {
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "status", response.status);
            cJSON_AddStringToObject(root, "command", "resume");
            cJSON_AddStringToObject(root, "session_id", session_id);
            cJSON_AddNumberToObject(root, "steps", response.steps);
            cJSON_AddStringToObject(
                root, "final", response.text ? response.text : "");
            cJSON_AddStringToObject(root, "trace", response.trace_path);
            cli_json_print(root);
            cJSON_Delete(root);
        } else {
            printf("Resuming session: %s\n", session_id);
            printf("Previous status : %s\n", record.status);
            printf("Previous steps  : %d\n\n", record.steps);
            printf("%s\n", response.text ? response.text : "");
        }
        exit_code = AEGIS_CLI_EXIT_SUCCESS;
    } else {
        exit_code = cli_error(
            options,
            cli_status_exit_code(status),
            "%s",
            aegis_status_string(status)
        );
    }
    aegis_response_free(&response);
    free(task);
    aegis_session_record_clear(&record);
    aegis_state_close(&state);
    cli_environment_clear(&environment);
    return exit_code;
}

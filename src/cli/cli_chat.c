#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aegis/cli_command.h"
#include "aegis/message.h"
#include "aegis/response.h"
#include "aegis/runtime.h"
#include "aegis/session.h"
#include "aegis/state.h"
#include "aegis/str.h"

static int cli_ask_user(
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

static int cli_send_message(
    void *userdata,
    const char *message
)
{
    (void)userdata;
    fprintf(stderr, "%s\n", message);
    return 1;
}

static char *conversation_task(
    const char *history,
    const char *message
)
{
    size_t history_length = history ? strlen(history) : 0U;
    size_t message_length = strlen(message);
    char *task = malloc(history_length + message_length + 32U);

    if (!task) {
        return NULL;
    }
    snprintf(
        task,
        history_length + message_length + 32U,
        "%sUser: %s",
        history ? history : "",
        message
    );
    return task;
}

static int append_conversation(
    char **history,
    const char *user,
    const char *assistant
)
{
    size_t old_length = *history ? strlen(*history) : 0U;
    size_t required = old_length + strlen(user) + strlen(assistant) + 32U;
    char *resized = realloc(*history, required);

    if (!resized) {
        return 0;
    }
    *history = resized;
    snprintf(
        resized + old_length,
        required - old_length,
        "User: %s\nAssistant: %s\n",
        user,
        assistant
    );
    return 1;
}

static int load_existing_chat(
    const CliEnvironment *environment,
    const char *session_id,
    int *step_count,
    char **history
)
{
    char state_path[AEGIS_CONFIG_PATH_MAX * 2U];
    AegisState state;
    AegisSessionRecord record;
    size_t required;
    AegisStatus status;

    if (!cli_join_path(
            state_path,
            sizeof(state_path),
            environment->workspace,
            environment->config.state_path)) {
        return 0;
    }
    memset(&state, 0, sizeof(state));
    memset(&record, 0, sizeof(record));
    if (aegis_state_open(&state, state_path) != AEGIS_OK) {
        return 0;
    }
    status = aegis_state_get_session(&state, session_id, &record);
    aegis_state_close(&state);
    if (status != AEGIS_OK) {
        return 0;
    }
    required = strlen(record.task ? record.task : "") +
        strlen(record.final_text ? record.final_text : "") + 64U;
    *history = malloc(required);
    if (*history) {
        snprintf(
            *history,
            required,
            "Previous task: %s\nPrevious result: %s\n",
            record.task ? record.task : "",
            record.final_text ? record.final_text : ""
        );
        *step_count = record.steps;
    }
    aegis_session_record_clear(&record);
    return *history != NULL;
}

int aegis_cli_cmd_chat(const CliOptions *options)
{
    CliEnvironment environment;
    AegisRuntime *runtime;
    char error[AEGIS_CLI_ERROR_MAX];
    char session_id[AEGIS_SESSION_ID_MAX];
    char trace_path[AEGIS_CONFIG_PATH_MAX * 2U];
    char trace_directory[AEGIS_CONFIG_PATH_MAX * 2U];
    char *history = NULL;
    char *line = NULL;
    size_t line_capacity = 0U;
    int step_count = 0;
    int exit_code = AEGIS_CLI_EXIT_SUCCESS;

    if (options->positional_count != 0U) {
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "chat does not accept positional arguments");
    }
    if (options->json || options->no_input) {
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "chat requires interactive human-readable input");
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
    if (options->session) {
        if (!aegis_session_id_is_valid(options->session)) {
            cli_environment_clear(&environment);
            return cli_error(
                options, AEGIS_CLI_EXIT_STATE, "invalid session id");
        }
        memcpy(session_id, options->session, strlen(options->session) + 1U);
        if (!load_existing_chat(
                &environment,
                session_id,
                &step_count,
                &history)) {
            cli_environment_clear(&environment);
            return cli_error(
                options,
                AEGIS_CLI_EXIT_STATE,
                "session not found: %s",
                session_id
            );
        }
    } else if (aegis_session_id_make(
            "chat", session_id, sizeof(session_id)) != AEGIS_OK) {
        cli_environment_clear(&environment);
        return cli_error(
            options, AEGIS_CLI_EXIT_STATE, "failed to create session");
    }
    runtime = aegis_runtime_new_with_config(&environment.config);
    if (!runtime) {
        free(history);
        cli_environment_clear(&environment);
        return cli_error(
            options, AEGIS_CLI_EXIT_GENERAL, "failed to initialize runtime");
    }
    if (options->trace) {
        if (!cli_join_path(
                trace_path,
                sizeof(trace_path),
                environment.workspace,
                options->trace)) {
            aegis_runtime_free(runtime);
            cli_environment_clear(&environment);
            free(history);
            return cli_error(
                options,
                AEGIS_CLI_EXIT_TRACE,
                "trace path is too long"
            );
        }
    } else if (!cli_join_path(
            trace_directory,
            sizeof(trace_directory),
            environment.workspace,
            environment.config.trace_directory) ||
        snprintf(
            trace_path,
            sizeof(trace_path),
            "%s/%s.jsonl",
            trace_directory,
            session_id) < 0 ||
        strlen(trace_directory) + strlen(session_id) + 8U >=
            sizeof(trace_path)) {
        aegis_runtime_free(runtime);
        cli_environment_clear(&environment);
        free(history);
        return cli_error(
            options,
            AEGIS_CLI_EXIT_TRACE,
            "trace path is too long"
        );
    }

    puts("Aegis chat started.");
    puts("Type /help for commands, /exit to quit.");
    for (;;) {
        ssize_t length;

        fputs("aegis> ", stdout);
        fflush(stdout);
        length = getline(&line, &line_capacity, stdin);
        if (length < 0) {
            break;
        }
        while (length > 0 &&
               (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        if (!line[0]) {
            continue;
        }
        if (strcmp(line, "/exit") == 0) {
            break;
        }
        if (strcmp(line, "/help") == 0) {
            puts("/help /exit /session /tools /profile /workspace "
                 "/trace /clear /compact");
            continue;
        }
        if (strcmp(line, "/session") == 0) {
            puts(session_id);
            continue;
        }
        if (strcmp(line, "/profile") == 0) {
            puts(environment.config.active_profile.id);
            continue;
        }
        if (strcmp(line, "/workspace") == 0) {
            puts(environment.workspace);
            continue;
        }
        if (strcmp(line, "/trace") == 0) {
            puts(trace_path);
            continue;
        }
        if (strcmp(line, "/tools") == 0) {
            size_t index;
            for (index = 0U; index < environment.registry.count; ++index) {
                const AegisTool *tool = &environment.registry.tools[index];
                if (aegis_config_tool_is_effective(
                        &environment.config, tool->name)) {
                    puts(tool->name);
                }
            }
            continue;
        }
        if (strcmp(line, "/clear") == 0) {
            fputs("\033[2J\033[H", stdout);
            continue;
        }
        if (strcmp(line, "/compact") == 0) {
            if (history) {
                char *compacted = aegis_strdup(
                    "Previous conversation was compacted by the user.\n");
                if (!compacted) {
                    exit_code = AEGIS_CLI_EXIT_GENERAL;
                    break;
                }
                free(history);
                history = compacted;
            }
            puts("History compacted.");
            continue;
        }
        {
            char *task = conversation_task(history, line);
            AegisMessage message;
            AegisResponse response;
            AegisStatus status;

            if (!task) {
                exit_code = AEGIS_CLI_EXIT_GENERAL;
                break;
            }
            memset(&message, 0, sizeof(message));
            message.channel = "cli";
            message.user_id = "local";
            message.session_id = session_id;
            message.text = task;
            message.workspace = environment.workspace;
            message.profile = environment.config.active_profile.id;
            message.trace_path = options->trace;
            message.auto_approve = options->yes;
            message.no_input = options->approval &&
                strcmp(options->approval, "never") == 0;
            message.initial_step = step_count;
            message.ask_user = cli_ask_user;
            message.send_message = cli_send_message;
            message.is_cancelled = cli_interrupted;
            aegis_response_init(&response);
            status = aegis_runtime_handle_message(runtime, &message, &response);
            free(task);
            if (status != AEGIS_OK) {
                fprintf(stderr, "error: %s\n", aegis_status_string(status));
                aegis_response_free(&response);
                exit_code = cli_status_exit_code(status);
                if (status == AEGIS_ERR_INTERRUPTED) {
                    break;
                }
                continue;
            }
            printf("%s\n", response.text ? response.text : "");
            step_count = response.steps;
            if (!append_conversation(
                    &history,
                    line,
                    response.text ? response.text : "")) {
                aegis_response_free(&response);
                exit_code = AEGIS_CLI_EXIT_GENERAL;
                break;
            }
            aegis_response_free(&response);
        }
    }
    if (cli_interrupted(NULL)) {
        exit_code = AEGIS_CLI_EXIT_INTERRUPTED;
    }
    free(line);
    free(history);
    aegis_runtime_free(runtime);
    cli_environment_clear(&environment);
    return exit_code;
}

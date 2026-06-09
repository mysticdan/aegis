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

static int run_ask_user(
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

static int run_send_message(
    void *userdata,
    const char *message
)
{
    (void)userdata;
    fprintf(stderr, "%s\n", message);
    return 1;
}

int aegis_cli_cmd_run(const CliOptions *options)
{
    CliEnvironment environment;
    CliTask task;
    char error[AEGIS_CLI_ERROR_MAX];
    int exit_code;

    if (!cli_load_task(options, &task, error, sizeof(error))) {
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE, "%s", error);
    }
    exit_code = cli_load_environment(
        options, &environment, error, sizeof(error));
    if (exit_code != AEGIS_CLI_EXIT_SUCCESS) {
        free(task.owned);
        cli_environment_clear(&environment);
        return cli_error(options, exit_code, "%s", error);
    }
    exit_code = cli_confirm_agent_execution(
        options, &environment.config, options->dry_run);
    if (exit_code != AEGIS_CLI_EXIT_SUCCESS) {
        free(task.owned);
        cli_environment_clear(&environment);
        return exit_code;
    }
    if (!options->dry_run) {
        AegisRuntime *runtime;
        AegisMessage message;
        AegisResponse response;
        char session_id[AEGIS_SESSION_ID_MAX];
        AegisStatus status;

        if (options->session) {
            if (!aegis_session_id_is_valid(options->session)) {
                free(task.owned);
                cli_environment_clear(&environment);
                return cli_error(
                    options,
                    AEGIS_CLI_EXIT_USAGE,
                    "invalid session id"
                );
            }
            snprintf(session_id, sizeof(session_id), "%s", options->session);
        } else if (aegis_session_id_make(
                "cli", session_id, sizeof(session_id)) != AEGIS_OK) {
            free(task.owned);
            cli_environment_clear(&environment);
            return cli_error(
                options, AEGIS_CLI_EXIT_STATE,
                "failed to create session id");
        }
        memset(&message, 0, sizeof(message));
        message.channel = "cli";
        message.user_id = "local";
        message.session_id = session_id;
        message.text = task.text;
        message.workspace = environment.workspace;
        message.profile = environment.config.active_profile.id;
        message.trace_path = options->trace;
        message.auto_approve = options->yes;
        message.no_input = options->no_input ||
            (options->approval &&
             strcmp(options->approval, "never") == 0);
        if (!options->no_input) {
            message.ask_user = run_ask_user;
            message.send_message = run_send_message;
        }
        message.is_cancelled = cli_interrupted;
        aegis_response_init(&response);
        runtime = aegis_runtime_new_with_config(&environment.config);
        if (!runtime) {
            free(task.owned);
            cli_environment_clear(&environment);
            return cli_error(
                options, AEGIS_CLI_EXIT_GENERAL,
                "failed to initialize runtime");
        }
        status = aegis_runtime_handle_message(runtime, &message, &response);
        aegis_runtime_free(runtime);
        exit_code = cli_status_exit_code(status);
        if (status == AEGIS_OK) {
            if (options->json) {
                cJSON *root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "status", response.status);
                cJSON_AddStringToObject(root, "command", "run");
                cJSON_AddStringToObject(
                    root, "session_id", response.session_id);
                cJSON_AddNumberToObject(root, "steps", response.steps);
                cJSON_AddStringToObject(
                    root, "final", response.text ? response.text : "");
                cJSON_AddStringToObject(
                    root, "trace", response.trace_path);
                if (!cli_json_print(root)) {
                    exit_code = AEGIS_CLI_EXIT_GENERAL;
                }
                cJSON_Delete(root);
            } else if (options->quiet) {
                puts(response.text ? response.text : "");
            } else {
                printf("Session: %s\n", response.session_id);
                printf("Workspace: %s\n", environment.workspace);
                printf("Profile: %s\n",
                       environment.config.active_profile.id);
                printf("Mode: %s\n\n", environment.config.mode);
                printf("%s\n", response.text ? response.text : "");
                printf("Trace: %s\n", response.trace_path);
            }
        } else {
            exit_code = cli_error(
                options, exit_code, "%s", aegis_status_string(status));
        }
        aegis_response_free(&response);
        free(task.owned);
        cli_environment_clear(&environment);
        return exit_code;
    }

    if (options->json) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "status", "dry_run");
        cJSON_AddStringToObject(root, "command", "run");
        cJSON_AddStringToObject(
            root, "config", environment.config.config_path);
        cJSON_AddStringToObject(
            root, "profile", environment.config.active_profile.id);
        cJSON_AddStringToObject(root, "mode", environment.config.mode);
        cJSON_AddStringToObject(root, "workspace", environment.workspace);
        cJSON_AddNumberToObject(
            root, "max_steps", environment.config.max_steps);
        cJSON_AddNumberToObject(root, "task_bytes", (double)task.length);
        cJSON_AddNumberToObject(
            root,
            "effective_tools",
            (double)cli_effective_tool_count(&environment)
        );
        if (!cli_json_print(root)) {
            exit_code = AEGIS_CLI_EXIT_GENERAL;
        }
        cJSON_Delete(root);
    } else if (options->quiet) {
        puts("Dry run valid.");
    } else {
        puts("Aegis run dry-run");
        printf("Config     : %s\n", environment.config.config_path);
        printf("Workspace  : %s\n", environment.workspace);
        printf("Profile    : %s\n", environment.config.active_profile.id);
        printf("Mode       : %s\n", environment.config.mode);
        printf("Max steps  : %d\n", environment.config.max_steps);
        printf("Task bytes : %zu\n", task.length);
        printf("Tools      : %zu effective\n",
               cli_effective_tool_count(&environment));
        if (options->verbose) {
            printf("Provider   : %s\n", environment.config.provider);
            printf("Model      : %s\n", environment.config.model);
            printf("Task       : %s\n", task.text);
        }
        puts("No model or tool action was executed.");
    }
    free(task.owned);
    cli_environment_clear(&environment);
    return exit_code;
}

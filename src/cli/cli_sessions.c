#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#include "aegis/cli_command.h"
#include "aegis/state.h"

static int state_path_for_environment(
    const CliEnvironment *environment,
    char *path,
    size_t size
)
{
    return environment->config.state_path[0] == '/'
        ? snprintf(path, size, "%s", environment->config.state_path) >= 0 &&
            strlen(environment->config.state_path) < size
        : cli_join_path(
            path,
            size,
            environment->workspace,
            environment->config.state_path
        );
}

static int open_state(
    const CliOptions *options,
    CliEnvironment *environment,
    AegisState *state
)
{
    char error[AEGIS_CLI_ERROR_MAX];
    char path[AEGIS_CONFIG_PATH_MAX * 2U];
    int exit_code = cli_load_environment(
        options, environment, error, sizeof(error));

    if (exit_code != AEGIS_CLI_EXIT_SUCCESS) {
        return cli_error(options, exit_code, "%s", error);
    }
    if (!state_path_for_environment(environment, path, sizeof(path)) ||
        aegis_state_open(state, path) != AEGIS_OK) {
        cli_environment_clear(environment);
        return cli_error(
            options, AEGIS_CLI_EXIT_STATE, "cannot open session state");
    }
    return AEGIS_CLI_EXIT_SUCCESS;
}

static void clear_records(AegisSessionRecord *records, size_t count)
{
    size_t index;

    for (index = 0U; index < count; ++index) {
        aegis_session_record_clear(&records[index]);
    }
    free(records);
}

static long long parse_duration_ms(const char *value)
{
    char *end;
    long long amount;
    long long multiplier;

    if (!value || !value[0]) {
        return -1;
    }
    amount = strtoll(value, &end, 10);
    if (amount <= 0 || !end || !end[0] || end[1]) {
        return -1;
    }
    switch (*end) {
        case 'm': multiplier = 60LL * 1000LL; break;
        case 'h': multiplier = 60LL * 60LL * 1000LL; break;
        case 'd': multiplier = 24LL * 60LL * 60LL * 1000LL; break;
        default: return -1;
    }
    return amount > LLONG_MAX / multiplier ? -1 : amount * multiplier;
}

static int trace_is_inside_workspace(
    const char *workspace,
    const char *trace_path
)
{
    char *workspace_real = realpath(workspace, NULL);
    char *trace_real = realpath(trace_path, NULL);
    size_t length;
    int inside;

    if (!workspace_real || !trace_real) {
        free(workspace_real);
        free(trace_real);
        return 0;
    }
    length = strlen(workspace_real);
    inside = strncmp(workspace_real, trace_real, length) == 0 &&
        trace_real[length] == '/';
    free(workspace_real);
    free(trace_real);
    return inside;
}

int aegis_cli_cmd_sessions(const CliOptions *options)
{
    CliEnvironment environment;
    AegisState state;
    AegisSessionRecord *records = NULL;
    size_t count = 0U;
    const char *subcommand;
    int exit_code;
    size_t index;

    if (!options || options->positional_count < 1U) {
        return cli_error(
            options,
            AEGIS_CLI_EXIT_USAGE,
            "usage: aegis sessions list|show|delete|clean"
        );
    }
    subcommand = options->positionals[0];
    memset(&environment, 0, sizeof(environment));
    memset(&state, 0, sizeof(state));
    exit_code = open_state(options, &environment, &state);
    if (exit_code != AEGIS_CLI_EXIT_SUCCESS) {
        return exit_code;
    }

    if (strcmp(subcommand, "show") == 0) {
        AegisSessionRecord record;
        char *events = NULL;
        AegisStatus status;

        if (options->positional_count != 2U) {
            exit_code = cli_error(
                options, AEGIS_CLI_EXIT_USAGE,
                "usage: aegis sessions show <id>");
            goto done;
        }
        memset(&record, 0, sizeof(record));
        status = aegis_state_get_session(
            &state, options->positionals[1], &record);
        if (status != AEGIS_OK) {
            exit_code = cli_error(
                options, AEGIS_CLI_EXIT_STATE, "session not found");
            goto done;
        }
        (void)aegis_state_session_events_json(
            &state, record.id, &events);
        if (options->json) {
            cJSON *root = cJSON_CreateObject();
            cJSON *parsed_events = events ? cJSON_Parse(events) : NULL;
            cJSON_AddStringToObject(root, "status", "success");
            cJSON_AddStringToObject(root, "command", "sessions");
            cJSON_AddStringToObject(root, "id", record.id);
            cJSON_AddStringToObject(root, "session_status", record.status);
            cJSON_AddStringToObject(root, "profile", record.profile);
            cJSON_AddStringToObject(root, "workspace", record.workspace);
            cJSON_AddStringToObject(root, "trace", record.trace_path);
            cJSON_AddStringToObject(root, "task", record.task);
            cJSON_AddStringToObject(
                root, "final", record.final_text ? record.final_text : "");
            cJSON_AddNumberToObject(root, "steps", record.steps);
            cJSON_AddItemToObject(
                root, "events",
                parsed_events ? parsed_events : cJSON_CreateArray());
            cli_json_print(root);
            cJSON_Delete(root);
        } else {
            printf("Session   : %s\n", record.id);
            printf("Status    : %s\n", record.status);
            printf("Profile   : %s\n", record.profile);
            printf("Workspace : %s\n", record.workspace);
            printf("Steps     : %d\n", record.steps);
            printf("Trace     : %s\n", record.trace_path);
            printf("Task      : %s\n", record.task);
            if (record.final_text) {
                printf("Final     : %s\n", record.final_text);
            }
        }
        free(events);
        aegis_session_record_clear(&record);
        goto done;
    }

    if (aegis_state_list_sessions(&state, &records, &count) != AEGIS_OK) {
        exit_code = cli_error(
            options, AEGIS_CLI_EXIT_STATE, "failed to list sessions");
        goto done;
    }
    if (strcmp(subcommand, "list") == 0) {
        if (options->positional_count != 1U) {
            exit_code = cli_error(
                options, AEGIS_CLI_EXIT_USAGE,
                "usage: aegis sessions list");
            goto done;
        }
        if (options->json) {
            cJSON *root = cJSON_CreateObject();
            cJSON *items = cJSON_CreateArray();
            cJSON_AddStringToObject(root, "status", "success");
            cJSON_AddStringToObject(root, "command", "sessions");
            cJSON_AddItemToObject(root, "sessions", items);
            for (index = 0U; index < count; ++index) {
                cJSON *entry = cJSON_CreateObject();
                cJSON_AddStringToObject(entry, "id", records[index].id);
                cJSON_AddStringToObject(
                    entry, "status", records[index].status);
                cJSON_AddStringToObject(
                    entry, "profile", records[index].profile);
                cJSON_AddNumberToObject(
                    entry, "steps", records[index].steps);
                cJSON_AddNumberToObject(
                    entry, "updated_ms", (double)records[index].updated_ms);
                cJSON_AddItemToArray(items, entry);
            }
            cli_json_print(root);
            cJSON_Delete(root);
        } else {
            printf("%-28s %-14s %-18s %-7s %s\n",
                   "ID", "Status", "Profile", "Steps", "Updated(ms)");
            for (index = 0U; index < count; ++index) {
                printf("%-28s %-14s %-18s %-7d %lld\n",
                       records[index].id,
                       records[index].status,
                       records[index].profile,
                       records[index].steps,
                       records[index].updated_ms);
            }
        }
    } else if (strcmp(subcommand, "delete") == 0) {
        AegisSessionRecord record;

        if (options->positional_count != 2U) {
            exit_code = cli_error(
                options, AEGIS_CLI_EXIT_USAGE,
                "usage: aegis sessions delete <id> [--keep-trace]");
            goto done;
        }
        memset(&record, 0, sizeof(record));
        if (aegis_state_get_session(
                &state, options->positionals[1], &record) != AEGIS_OK) {
            exit_code = cli_error(
                options, AEGIS_CLI_EXIT_STATE, "session not found");
            goto done;
        }
        if (aegis_state_delete_session(&state, record.id) != AEGIS_OK) {
            aegis_session_record_clear(&record);
            exit_code = cli_error(
                options, AEGIS_CLI_EXIT_STATE, "failed to delete session");
            goto done;
        }
        if (!options->keep_trace &&
            trace_is_inside_workspace(
                environment.workspace, record.trace_path)) {
            (void)unlink(record.trace_path);
        }
        if (options->json) {
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "status", "success");
            cJSON_AddStringToObject(root, "command", "sessions");
            cJSON_AddStringToObject(root, "deleted", record.id);
            cli_json_print(root);
            cJSON_Delete(root);
        } else {
            printf("Deleted session %s.\n", record.id);
        }
        aegis_session_record_clear(&record);
    } else if (strcmp(subcommand, "clean") == 0) {
        long long duration = parse_duration_ms(options->older_than);
        long long cutoff =
            (long long)time(NULL) * 1000LL - duration;
        size_t deleted = 0U;

        if (options->positional_count != 1U || duration < 0) {
            exit_code = cli_error(
                options, AEGIS_CLI_EXIT_USAGE,
                "usage: aegis sessions clean --older-than <Nm|Nh|Nd>");
            goto done;
        }
        for (index = 0U; index < count; ++index) {
            if (records[index].updated_ms >= cutoff) {
                continue;
            }
            if (aegis_state_delete_session(
                    &state, records[index].id) == AEGIS_OK) {
                ++deleted;
                if (!options->keep_trace &&
                    trace_is_inside_workspace(
                        environment.workspace,
                        records[index].trace_path)) {
                    (void)unlink(records[index].trace_path);
                }
            }
        }
        if (options->json) {
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "status", "success");
            cJSON_AddStringToObject(root, "command", "sessions");
            cJSON_AddNumberToObject(root, "deleted", (double)deleted);
            cli_json_print(root);
            cJSON_Delete(root);
        } else {
            printf("Deleted %zu session(s).\n", deleted);
        }
    } else {
        exit_code = cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "unknown sessions subcommand: %s", subcommand);
    }

done:
    clear_records(records, count);
    aegis_state_close(&state);
    cli_environment_clear(&environment);
    return exit_code;
}

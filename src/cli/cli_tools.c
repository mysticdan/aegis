#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cjson/cJSON.h>

#include "aegis/cli_command.h"

static AegisStatus json_tool_args(
    cJSON *root,
    AegisToolArgs *args,
    AegisKv **items_out,
    char ***owned_out
)
{
    size_t count;
    size_t index = 0U;
    AegisKv *items;
    char **owned;
    cJSON *entry;

    if (!cJSON_IsObject(root)) {
        return AEGIS_ERR_PARSE;
    }
    count = (size_t)cJSON_GetArraySize(root);
    items = count ? calloc(count, sizeof(*items)) : NULL;
    owned = count ? calloc(count, sizeof(*owned)) : NULL;
    if (count && (!items || !owned)) {
        free(items);
        free(owned);
        return AEGIS_ERR_OOM;
    }
    cJSON_ArrayForEach(entry, root) {
        items[index].key = entry->string;
        if (cJSON_IsString(entry)) {
            items[index].value = entry->valuestring;
        } else {
            owned[index] = cJSON_PrintUnformatted(entry);
            if (!owned[index]) {
                size_t cleanup;
                for (cleanup = 0U; cleanup < index; ++cleanup) {
                    cJSON_free(owned[cleanup]);
                }
                free(items);
                free(owned);
                return AEGIS_ERR_OOM;
            }
            items[index].value = owned[index];
        }
        ++index;
    }
    args->items = items;
    args->count = count;
    *items_out = items;
    *owned_out = owned;
    return AEGIS_OK;
}

static void clear_json_tool_args(
    AegisKv *items,
    char **owned,
    size_t count
)
{
    size_t index;

    for (index = 0U; index < count; ++index) {
        cJSON_free(owned[index]);
    }
    free(owned);
    free(items);
}

static int print_tool_list(
    const CliOptions *options,
    const CliEnvironment *environment
)
{
    size_t index;

    if (options->json) {
        cJSON *root = cJSON_CreateObject();
        cJSON *tools = cJSON_CreateArray();
        cJSON_AddStringToObject(root, "status", "success");
        cJSON_AddStringToObject(root, "command", "tools");
        cJSON_AddStringToObject(
            root, "config", environment->config.config_path);
        cJSON_AddStringToObject(
            root, "profile", environment->config.active_profile.id);
        cJSON_AddNumberToObject(
            root, "effective_count",
            (double)cli_effective_tool_count(environment));
        cJSON_AddItemToObject(root, "tools", tools);
        for (index = 0U; index < environment->registry.count; ++index) {
            const AegisTool *tool = &environment->registry.tools[index];
            cJSON *entry = cJSON_CreateObject();
            cJSON_AddStringToObject(entry, "name", tool->name);
            cJSON_AddStringToObject(
                entry, "risk", aegis_tool_risk_name(tool->risk_level));
            cJSON_AddStringToObject(
                entry, "availability", cli_tool_availability(tool));
            cJSON_AddStringToObject(
                entry, "policy",
                aegis_config_tool_decision(
                    &environment->config, tool->name));
            cJSON_AddStringToObject(
                entry, "state",
                cli_tool_state(&environment->config, tool));
            cJSON_AddBoolToObject(
                entry, "effective",
                aegis_config_tool_is_effective(
                    &environment->config, tool->name));
            cJSON_AddStringToObject(
                entry, "description", tool->description);
            cJSON_AddItemToArray(tools, entry);
        }
        cli_json_print(root);
        cJSON_Delete(root);
    } else if (options->quiet) {
        for (index = 0U; index < environment->registry.count; ++index) {
            const AegisTool *tool = &environment->registry.tools[index];
            if (aegis_config_tool_is_effective(
                    &environment->config,
                    tool->name)) {
                puts(tool->name);
            }
        }
    } else {
        printf("%-18s %-9s %-12s %-18s %s\n",
               "Tool", "Risk", "Availability", "State", "Description");
        for (index = 0U; index < environment->registry.count; ++index) {
            const AegisTool *tool = &environment->registry.tools[index];
            printf("%-18s %-9s %-12s %-18s %s\n",
                   tool->name,
                   aegis_tool_risk_name(tool->risk_level),
                   cli_tool_availability(tool),
                   cli_tool_state(&environment->config, tool),
                   tool->description);
        }
    }
    return AEGIS_CLI_EXIT_SUCCESS;
}

int aegis_cli_cmd_tools(const CliOptions *options)
{
    CliEnvironment environment;
    char error[AEGIS_CLI_ERROR_MAX];
    const char *subcommand;
    const AegisTool *tool;
    int exit_code;

    if (!options || options->positional_count < 1U) {
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "usage: aegis tools list|info|schema|test");
    }
    subcommand = options->positionals[0];
    exit_code = cli_load_environment(
        options, &environment, error, sizeof(error));
    if (exit_code != AEGIS_CLI_EXIT_SUCCESS) {
        cli_environment_clear(&environment);
        return cli_error(options, exit_code, "%s", error);
    }
    if (strcmp(subcommand, "list") == 0) {
        if (options->positional_count != 1U) {
            exit_code = cli_error(
                options, AEGIS_CLI_EXIT_USAGE,
                "usage: aegis tools list");
        } else {
            exit_code = print_tool_list(options, &environment);
        }
        cli_environment_clear(&environment);
        return exit_code;
    }
    if (options->positional_count != 2U) {
        cli_environment_clear(&environment);
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "tools %s requires one tool name", subcommand);
    }
    tool = aegis_tool_registry_find(
        &environment.registry, options->positionals[1]);
    if (!tool) {
        cli_environment_clear(&environment);
        return cli_error(
            options, AEGIS_CLI_EXIT_TOOL, "tool not found");
    }
    if (strcmp(subcommand, "info") == 0) {
        if (options->json) {
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "status", "success");
            cJSON_AddStringToObject(root, "command", "tools");
            cJSON_AddStringToObject(root, "name", tool->name);
            cJSON_AddStringToObject(
                root, "description", tool->description);
            cJSON_AddStringToObject(
                root, "risk", aegis_tool_risk_name(tool->risk_level));
            cJSON_AddStringToObject(
                root, "availability", cli_tool_availability(tool));
            cJSON_AddStringToObject(
                root, "policy",
                aegis_config_tool_decision(
                    &environment.config, tool->name));
            cJSON_AddStringToObject(
                root, "state",
                cli_tool_state(&environment.config, tool));
            cJSON_AddItemToObject(
                root, "schema", cJSON_Parse(tool->schema_json));
            cli_json_print(root);
            cJSON_Delete(root);
        } else {
            printf("Name         : %s\n", tool->name);
            printf("Description  : %s\n", tool->description);
            printf("Risk         : %s\n",
                   aegis_tool_risk_name(tool->risk_level));
            printf("Availability : %s\n", cli_tool_availability(tool));
            printf("Policy       : %s\n",
                   aegis_config_tool_decision(
                       &environment.config, tool->name));
            printf("State        : %s\n",
                   cli_tool_state(&environment.config, tool));
        }
    } else if (strcmp(subcommand, "schema") == 0) {
        cJSON *schema = cJSON_Parse(tool->schema_json);
        if (options->json) {
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "status", "success");
            cJSON_AddStringToObject(root, "command", "tools");
            cJSON_AddStringToObject(root, "tool", tool->name);
            cJSON_AddItemToObject(root, "schema", schema);
            schema = NULL;
            cli_json_print(root);
            cJSON_Delete(root);
        } else {
            char *rendered = cJSON_Print(schema);
            if (rendered) {
                puts(rendered);
                cJSON_free(rendered);
            }
        }
        cJSON_Delete(schema);
    } else if (strcmp(subcommand, "test") == 0) {
        cJSON *arguments = cJSON_Parse(
            options->args_json ? options->args_json : "{}");
        AegisToolArgs args = {0};
        AegisKv *items = NULL;
        char **owned = NULL;
        AegisToolContext context;
        AegisToolResult result;
        AegisStatus status;
        const char *decision = aegis_config_tool_decision(
            &environment.config, tool->name);
        int approved = options->yes &&
            tool->risk_level != AEGIS_RISK_CRITICAL;

        status = json_tool_args(arguments, &args, &items, &owned);
        aegis_tool_result_init(&result);
        if (status == AEGIS_OK) {
            aegis_tool_context_from_config(
                &context,
                &environment.config,
                environment.workspace,
                approved
            );
            status = aegis_tool_registry_execute(
                &environment.registry,
                tool->name,
                &args,
                &context,
                &result
            );
        }
        if (options->json) {
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(
                root, "status", status == AEGIS_OK ? "success" : "error");
            cJSON_AddStringToObject(root, "command", "tools");
            cJSON_AddStringToObject(root, "tool", tool->name);
            cJSON_AddStringToObject(
                root, "policy", decision ? decision : "unknown");
            cJSON_AddBoolToObject(root, "ok", result.ok);
            cJSON_AddNumberToObject(root, "exit_code", result.exit_code);
            cJSON_AddStringToObject(
                root, "stdout", result.stdout_data ? result.stdout_data : "");
            cJSON_AddStringToObject(
                root, "stderr", result.stderr_data ? result.stderr_data : "");
            cJSON_AddStringToObject(
                root, "error",
                result.error_message ? result.error_message : "");
            cli_json_print(root);
            cJSON_Delete(root);
        } else if (status == AEGIS_OK) {
            printf("%s", result.stdout_data ? result.stdout_data : "");
        }
        clear_json_tool_args(items, owned, args.count);
        cJSON_Delete(arguments);
        aegis_tool_result_clear(&result);
        if (status != AEGIS_OK) {
            int result_code = status == AEGIS_ERR_POLICY_DENIED
                ? AEGIS_CLI_EXIT_POLICY
                : AEGIS_CLI_EXIT_TOOL;
            cli_environment_clear(&environment);
            if (options->json) {
                return result_code;
            }
            return cli_error(
                options,
                result_code,
                "%s",
                aegis_status_string(status)
            );
        }
    } else {
        cli_environment_clear(&environment);
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "unknown tools subcommand: %s", subcommand);
    }
    cli_environment_clear(&environment);
    return AEGIS_CLI_EXIT_SUCCESS;
}

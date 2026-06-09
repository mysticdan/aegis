#define _XOPEN_SOURCE 700

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#include "aegis/cli_command.h"
#include "aegis/mcp.h"

static int valid_server_name(const char *name)
{
    const unsigned char *cursor = (const unsigned char *)name;

    if (!name || !name[0]) {
        return 0;
    }
    while (*cursor) {
        if (!( (*cursor >= 'a' && *cursor <= 'z') ||
               (*cursor >= 'A' && *cursor <= 'Z') ||
               (*cursor >= '0' && *cursor <= '9') ||
               *cursor == '_' || *cursor == '-')) {
            return 0;
        }
        ++cursor;
    }
    return 1;
}

static int registry_path(
    const CliEnvironment *environment,
    char *path,
    size_t size
)
{
    return cli_join_path(
        path, size, environment->workspace, ".aegis/config/mcp.json");
}

static cJSON *load_registry(const char *path)
{
    FILE *file = fopen(path, "rb");
    cJSON *root;
    cJSON *servers;
    long length;
    char *data;

    if (!file) {
        root = cJSON_CreateObject();
        if (root) {
            cJSON_AddItemToObject(root, "servers", cJSON_CreateObject());
        }
        return root;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    data = malloc((size_t)length + 1U);
    if (!data) {
        fclose(file);
        return NULL;
    }
    if (length && fread(data, 1U, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    data[length] = '\0';
    root = cJSON_Parse(data);
    free(data);
    servers = root
        ? cJSON_GetObjectItemCaseSensitive(root, "servers")
        : NULL;
    if (!cJSON_IsObject(servers)) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

static int save_registry(const char *path, const cJSON *root)
{
    char temporary[AEGIS_CONFIG_PATH_MAX * 2U];
    char *rendered;
    int descriptor;
    size_t length;
    size_t offset = 0U;

    if (snprintf(
            temporary, sizeof(temporary), "%s.tmp.XXXXXX", path) < 0 ||
        strlen(path) + 12U >= sizeof(temporary)) {
        return 0;
    }
    rendered = cJSON_Print(root);
    if (!rendered) {
        return 0;
    }
    descriptor = mkstemp(temporary);
    if (descriptor < 0) {
        cJSON_free(rendered);
        return 0;
    }
    length = strlen(rendered);
    while (offset < length) {
        ssize_t written = write(
            descriptor, rendered + offset, length - offset);
        if (written > 0) {
            offset += (size_t)written;
        } else if (written < 0 && errno != EINTR) {
            close(descriptor);
            unlink(temporary);
            cJSON_free(rendered);
            return 0;
        }
    }
    cJSON_free(rendered);
    if (write(descriptor, "\n", 1U) != 1 ||
        fsync(descriptor) != 0 ||
        fchmod(descriptor, 0644) != 0 ||
        close(descriptor) != 0 ||
        rename(temporary, path) != 0) {
        unlink(temporary);
        return 0;
    }
    return 1;
}

static const char *server_endpoint(cJSON *server)
{
    cJSON *transport =
        cJSON_GetObjectItemCaseSensitive(server, "transport");
    cJSON *endpoint;

    if (!cJSON_IsString(transport)) {
        return NULL;
    }
    endpoint = cJSON_GetObjectItemCaseSensitive(
        server,
        strcmp(transport->valuestring, "http") == 0 ? "url" : "command"
    );
    return cJSON_IsString(endpoint) ? endpoint->valuestring : NULL;
}

static int invoke_mcp(
    const CliOptions *options,
    const CliEnvironment *environment,
    const char *server_name,
    const char *endpoint,
    const char *method,
    const char *params,
    cJSON *collection
)
{
    AegisToolContext context;
    AegisToolResult result;
    AegisStatus status;

    if (!aegis_config_tool_is_effective(
            &environment->config, AEGIS_TOOL_MCP)) {
        return cli_error(
            options, AEGIS_CLI_EXIT_POLICY,
            "mcp_tool is disabled by config or profile");
    }
    aegis_tool_context_from_config(
        &context,
        &environment->config,
        environment->workspace,
        options->yes
    );
    aegis_tool_result_init(&result);
    status = aegis_mcp_request(
        &context, endpoint, method, params, &result);
    if (collection) {
        cJSON *entry = cJSON_CreateObject();
        cJSON *payload = result.stdout_data
            ? cJSON_Parse(result.stdout_data)
            : NULL;

        cJSON_AddStringToObject(
            entry,
            "server",
            server_name ? server_name : ""
        );
        cJSON_AddStringToObject(
            entry,
            "status",
            status == AEGIS_OK ? "success" : "error"
        );
        cJSON_AddItemToObject(
            entry,
            "response",
            payload ? payload : cJSON_CreateString(
                result.stdout_data ? result.stdout_data : "")
        );
        cJSON_AddItemToArray(collection, entry);
    } else if (options->json) {
        cJSON *root = cJSON_CreateObject();
        cJSON *payload = result.stdout_data
            ? cJSON_Parse(result.stdout_data)
            : NULL;
        cJSON_AddStringToObject(
            root, "status", status == AEGIS_OK ? "success" : "error");
        cJSON_AddStringToObject(root, "command", "mcp");
        cJSON_AddItemToObject(
            root, "response",
            payload ? payload : cJSON_CreateString(
                result.stdout_data ? result.stdout_data : ""));
        cli_json_print(root);
        cJSON_Delete(root);
    } else if (!options->quiet && result.stdout_data) {
        puts(result.stdout_data);
    }
    aegis_tool_result_clear(&result);
    if (status == AEGIS_OK) {
        return AEGIS_CLI_EXIT_SUCCESS;
    }
    if (collection) {
        return status == AEGIS_ERR_POLICY_DENIED
            ? AEGIS_CLI_EXIT_POLICY
            : AEGIS_CLI_EXIT_TOOL;
    }
    return cli_error(
            options,
            status == AEGIS_ERR_POLICY_DENIED
                ? AEGIS_CLI_EXIT_POLICY
                : AEGIS_CLI_EXIT_TOOL,
            "%s",
            aegis_status_string(status)
        );
}

int aegis_cli_cmd_mcp(const CliOptions *options)
{
    CliEnvironment environment;
    char error[AEGIS_CLI_ERROR_MAX];
    char path[AEGIS_CONFIG_PATH_MAX * 2U];
    cJSON *root;
    cJSON *servers;
    const char *subcommand;
    int exit_code;

    if (!options || options->positional_count < 1U) {
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "usage: aegis mcp list|add|remove|tools|call");
    }
    subcommand = options->positionals[0];
    exit_code = cli_load_environment(
        options, &environment, error, sizeof(error));
    if (exit_code != AEGIS_CLI_EXIT_SUCCESS) {
        cli_environment_clear(&environment);
        return cli_error(options, exit_code, "%s", error);
    }
    if (!registry_path(&environment, path, sizeof(path)) ||
        !(root = load_registry(path))) {
        cli_environment_clear(&environment);
        return cli_error(
            options, AEGIS_CLI_EXIT_CONFIG, "invalid MCP registry");
    }
    servers = cJSON_GetObjectItemCaseSensitive(root, "servers");
    if (strcmp(subcommand, "list") == 0) {
        cJSON *server;
        if (options->json) {
            cJSON *output = cJSON_CreateObject();
            cJSON_AddStringToObject(output, "status", "success");
            cJSON_AddStringToObject(output, "command", "mcp");
            cJSON_AddItemToObject(
                output, "servers", cJSON_Duplicate(servers, 1));
            cli_json_print(output);
            cJSON_Delete(output);
        } else {
            cJSON_ArrayForEach(server, servers) {
                cJSON *transport =
                    cJSON_GetObjectItemCaseSensitive(server, "transport");
                printf("%-20s %s\n",
                       server->string,
                       cJSON_IsString(transport)
                           ? transport->valuestring
                           : "unknown");
            }
        }
    } else if (strcmp(subcommand, "add") == 0) {
        cJSON *entry;
        const char *name;
        if (options->positional_count != 2U ||
            ((options->command_value != NULL) == (options->url != NULL))) {
            cJSON_Delete(root);
            cli_environment_clear(&environment);
            return cli_error(
                options, AEGIS_CLI_EXIT_USAGE,
                "usage: aegis mcp add <name> (--cmd <command>|--url <url>)");
        }
        name = options->positionals[1];
        if (!valid_server_name(name)) {
            cJSON_Delete(root);
            cli_environment_clear(&environment);
            return cli_error(
                options, AEGIS_CLI_EXIT_USAGE, "invalid MCP server name");
        }
        if (cJSON_GetObjectItemCaseSensitive(servers, name)) {
            cJSON_Delete(root);
            cli_environment_clear(&environment);
            return cli_error(
                options, AEGIS_CLI_EXIT_CONFIG, "MCP server already exists");
        }
        entry = cJSON_CreateObject();
        cJSON_AddStringToObject(
            entry, "transport", options->url ? "http" : "stdio");
        cJSON_AddStringToObject(
            entry,
            options->url ? "url" : "command",
            options->url ? options->url : options->command_value
        );
        cJSON_AddItemToObject(servers, name, entry);
        if (!save_registry(path, root)) {
            exit_code = cli_error(
                options, AEGIS_CLI_EXIT_CONFIG,
                "failed to save MCP registry");
        } else if (options->json) {
            cJSON *output = cJSON_CreateObject();
            cJSON_AddStringToObject(output, "status", "success");
            cJSON_AddStringToObject(output, "command", "mcp");
            cJSON_AddStringToObject(output, "added", name);
            cli_json_print(output);
            cJSON_Delete(output);
        } else {
            printf("Added MCP server %s.\n", name);
        }
    } else if (strcmp(subcommand, "remove") == 0) {
        if (options->positional_count != 2U ||
            !cJSON_GetObjectItemCaseSensitive(
                servers, options->positionals[1])) {
            cJSON_Delete(root);
            cli_environment_clear(&environment);
            return cli_error(
                options, AEGIS_CLI_EXIT_CONFIG, "MCP server not found");
        }
        cJSON_DeleteItemFromObjectCaseSensitive(
            servers, options->positionals[1]);
        if (!save_registry(path, root)) {
            exit_code = cli_error(
                options, AEGIS_CLI_EXIT_CONFIG,
                "failed to save MCP registry");
        } else if (options->json) {
            cJSON *output = cJSON_CreateObject();
            cJSON_AddStringToObject(output, "status", "success");
            cJSON_AddStringToObject(output, "command", "mcp");
            cJSON_AddStringToObject(
                output, "removed", options->positionals[1]);
            cli_json_print(output);
            cJSON_Delete(output);
        } else {
            printf("Removed MCP server %s.\n", options->positionals[1]);
        }
    } else if (strcmp(subcommand, "tools") == 0) {
        cJSON *server;
        cJSON *output = NULL;
        cJSON *responses = NULL;
        int invoked = 0;

        if (options->json) {
            output = cJSON_CreateObject();
            responses = cJSON_CreateArray();
            if (!output || !responses) {
                cJSON_Delete(output);
                cJSON_Delete(responses);
                cJSON_Delete(root);
                cli_environment_clear(&environment);
                return cli_error(
                    options,
                    AEGIS_CLI_EXIT_GENERAL,
                    "failed to allocate MCP output"
                );
            }
            cJSON_AddStringToObject(output, "status", "success");
            cJSON_AddStringToObject(output, "command", "mcp");
            cJSON_AddItemToObject(output, "servers", responses);
        }
        cJSON_ArrayForEach(server, servers) {
            const char *endpoint = server_endpoint(server);
            if (endpoint) {
                invoked = 1;
                exit_code = invoke_mcp(
                    options,
                    &environment,
                    server->string,
                    endpoint,
                    "tools/list",
                    "{}",
                    responses
                );
                if (exit_code != AEGIS_CLI_EXIT_SUCCESS) {
                    break;
                }
            }
        }
        if (!invoked) {
            cJSON_Delete(output);
            exit_code = cli_error(
                options, AEGIS_CLI_EXIT_CONFIG,
                "no MCP servers are configured");
        } else if (options->json) {
            if (exit_code != AEGIS_CLI_EXIT_SUCCESS) {
                cJSON_ReplaceItemInObjectCaseSensitive(
                    output,
                    "status",
                    cJSON_CreateString("error")
                );
            }
            cli_json_print(output);
            cJSON_Delete(output);
        }
    } else if (strcmp(subcommand, "call") == 0) {
        const char *qualified;
        const char *separator;
        char server_name[AEGIS_CONFIG_NAME_MAX];
        const char *tool_name;
        cJSON *server;
        const char *endpoint;
        cJSON *params;
        cJSON *arguments;
        char *params_text;

        if (options->positional_count != 2U) {
            cJSON_Delete(root);
            cli_environment_clear(&environment);
            return cli_error(
                options, AEGIS_CLI_EXIT_USAGE,
                "usage: aegis mcp call <server/tool> --args <json>");
        }
        qualified = options->positionals[1];
        separator = strchr(qualified, '/');
        if (!separator ||
            (size_t)(separator - qualified) >= sizeof(server_name)) {
            cJSON_Delete(root);
            cli_environment_clear(&environment);
            return cli_error(
                options, AEGIS_CLI_EXIT_USAGE,
                "MCP tool must be qualified as server/tool");
        }
        memcpy(server_name, qualified, (size_t)(separator - qualified));
        server_name[separator - qualified] = '\0';
        tool_name = separator + 1;
        server = cJSON_GetObjectItemCaseSensitive(servers, server_name);
        endpoint = cJSON_IsObject(server) ? server_endpoint(server) : NULL;
        arguments = cJSON_Parse(
            options->args_json ? options->args_json : "{}");
        params = cJSON_CreateObject();
        if (!endpoint || !cJSON_IsObject(arguments) || !params) {
            cJSON_Delete(arguments);
            cJSON_Delete(params);
            cJSON_Delete(root);
            cli_environment_clear(&environment);
            return cli_error(
                options, AEGIS_CLI_EXIT_CONFIG,
                "invalid MCP server or arguments");
        }
        cJSON_AddStringToObject(params, "name", tool_name);
        cJSON_AddItemToObject(params, "arguments", arguments);
        params_text = cJSON_PrintUnformatted(params);
        cJSON_Delete(params);
        exit_code = params_text
            ? invoke_mcp(
                options,
                &environment,
                server_name,
                endpoint,
                "tools/call",
                params_text,
                NULL)
            : AEGIS_CLI_EXIT_GENERAL;
        cJSON_free(params_text);
    } else {
        exit_code = cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "unknown MCP subcommand: %s", subcommand);
    }
    cJSON_Delete(root);
    cli_environment_clear(&environment);
    return exit_code;
}

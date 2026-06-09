#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <cjson/cJSON.h>

#include "aegis/mcp.h"
#include "aegis/tool.h"

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

static char *registered_endpoint(
    const AegisToolContext *context,
    const char *server_name
)
{
    char path[AEGIS_CONFIG_PATH_MAX * 2U];
    char root_real[AEGIS_CONFIG_PATH_MAX];
    char file_real[AEGIS_CONFIG_PATH_MAX * 2U];
    FILE *file;
    struct stat metadata;
    long length;
    size_t root_length;
    char *text;
    cJSON *root;
    cJSON *servers;
    cJSON *server;
    cJSON *transport;
    cJSON *endpoint;
    char *result = NULL;

    if (!context || !context->workspace_root ||
        !valid_server_name(server_name) ||
        !realpath(context->workspace_root, root_real) ||
        snprintf(
            path,
            sizeof(path),
            "%s/.aegis/config/mcp.json",
            root_real) < 0 ||
        strlen(root_real) + strlen("/.aegis/config/mcp.json") >=
            sizeof(path) ||
        lstat(path, &metadata) != 0 ||
        !S_ISREG(metadata.st_mode) ||
        S_ISLNK(metadata.st_mode) ||
        !realpath(path, file_real)) {
        return NULL;
    }
    root_length = strlen(root_real);
    if (strncmp(file_real, root_real, root_length) != 0 ||
        file_real[root_length] != '/') {
        return NULL;
    }
    file = fopen(file_real, "rb");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0 ||
        (length = ftell(file)) < 0 ||
        length > 1024L * 1024L ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    text = malloc((size_t)length + 1U);
    if (!text) {
        fclose(file);
        return NULL;
    }
    if (length > 0 &&
        fread(text, 1U, (size_t)length, file) != (size_t)length) {
        fclose(file);
        free(text);
        return NULL;
    }
    if (fclose(file) != 0) {
        free(text);
        return NULL;
    }
    text[length] = '\0';
    root = cJSON_Parse(text);
    free(text);
    servers = root
        ? cJSON_GetObjectItemCaseSensitive(root, "servers")
        : NULL;
    server = cJSON_IsObject(servers)
        ? cJSON_GetObjectItemCaseSensitive(servers, server_name)
        : NULL;
    transport = cJSON_IsObject(server)
        ? cJSON_GetObjectItemCaseSensitive(server, "transport")
        : NULL;
    endpoint = cJSON_IsString(transport) &&
        (strcmp(transport->valuestring, "http") == 0 ||
         strcmp(transport->valuestring, "stdio") == 0)
        ? cJSON_GetObjectItemCaseSensitive(
            server,
            strcmp(transport->valuestring, "http") == 0
                ? "url"
                : "command")
        : NULL;
    if (cJSON_IsString(endpoint) && endpoint->valuestring) {
        size_t endpoint_length = strlen(endpoint->valuestring);
        result = malloc(endpoint_length + 1U);
        if (result) {
            memcpy(result, endpoint->valuestring, endpoint_length + 1U);
        }
    }
    cJSON_Delete(root);
    return result;
}

static AegisStatus execute_mcp_tool(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    const char *server;
    const char *tool;
    const char *arguments_text;
    char *endpoint;
    cJSON *arguments;
    cJSON *params;
    char *params_text;
    AegisStatus status;

    if (!args || !context || !out) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    server = aegis_tool_args_get(args, "server");
    tool = aegis_tool_args_get(args, "tool");
    arguments_text = aegis_tool_args_get(args, "arguments");
    if (!server || !server[0] || !tool || !tool[0]) {
        aegis_tool_result_set_error(out, "missing MCP server or tool");
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    endpoint = registered_endpoint(context, server);
    if (!endpoint) {
        aegis_tool_result_set_error(out, "MCP server is not registered");
        return AEGIS_ERR_POLICY_DENIED;
    }
    arguments = arguments_text && arguments_text[0]
        ? cJSON_Parse(arguments_text)
        : cJSON_CreateObject();
    params = cJSON_CreateObject();
    if (!cJSON_IsObject(arguments) || !params) {
        cJSON_Delete(arguments);
        cJSON_Delete(params);
        free(endpoint);
        aegis_tool_result_set_error(out, "invalid MCP arguments");
        return AEGIS_ERR_PARSE;
    }
    cJSON_AddStringToObject(params, "name", tool);
    cJSON_AddItemToObject(params, "arguments", arguments);
    params_text = cJSON_PrintUnformatted(params);
    cJSON_Delete(params);
    if (!params_text) {
        free(endpoint);
        return AEGIS_ERR_OOM;
    }
    status = aegis_mcp_request(
        context, endpoint, "tools/call", params_text, out);
    cJSON_free(params_text);
    free(endpoint);
    return status;
}

AegisTool aegis_tool_mcp_tool(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_MCP,
        .description = "Call a tool exposed by an approved MCP server.",
        .schema_json =
            "{\"type\":\"object\",\"required\":[\"server\",\"tool\"],"
            "\"properties\":{\"server\":{\"type\":\"string\",\"minLength\":1},"
            "\"tool\":{\"type\":\"string\",\"minLength\":1},"
            "\"arguments\":{\"type\":\"object\"}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_HIGH,
        .availability = AEGIS_TOOL_READY,
        .execute = execute_mcp_tool
    };
}

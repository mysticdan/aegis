#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aegis/tool.h"
#include "aegis/tool_path.h"

static AegisStatus execute_grep_log(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    const char *path;
    const char *query;
    char resolved[AEGIS_CONFIG_PATH_MAX];
    FILE *file;
    char *line = NULL;
    size_t line_capacity = 0U;
    char *output;
    size_t used = 0U;
    size_t maximum;
    ssize_t length;
    AegisStatus status;

    if (!args || !context || !out) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    path = aegis_tool_args_get(args, "path");
    query = aegis_tool_args_get(args, "query");
    if (!path || !query || !query[0]) {
        aegis_tool_result_set_error(out, "missing path or query");
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    status = aegis_tool_resolve_path(
        context, path, 0, resolved, sizeof(resolved));
    if (status != AEGIS_OK) {
        return status;
    }
    maximum = context->max_output_bytes ? context->max_output_bytes : 65536U;
    output = calloc(maximum + 1U, 1U);
    if (!output) {
        return AEGIS_ERR_OOM;
    }
    file = fopen(resolved, "rb");
    if (!file) {
        free(output);
        return AEGIS_ERR_IO;
    }
    while ((length = getline(&line, &line_capacity, file)) >= 0) {
        if (strstr(line, query)) {
            if ((size_t)length > maximum - used) {
                free(line);
                free(output);
                fclose(file);
                aegis_tool_result_set_error(out, "log matches exceed limit");
                return AEGIS_ERR_RUNTIME;
            }
            memcpy(output + used, line, (size_t)length);
            used += (size_t)length;
        }
    }
    free(line);
    fclose(file);
    output[used] = '\0';
    status = aegis_tool_result_set_stdout(out, output);
    free(output);
    return status;
}

AegisTool aegis_tool_grep_log(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_GREP_LOG,
        .description = "Search an approved log file.",
        .schema_json =
            "{\"type\":\"object\",\"required\":[\"path\",\"query\"],"
            "\"properties\":{\"path\":{\"type\":\"string\",\"minLength\":1},"
            "\"query\":{\"type\":\"string\",\"minLength\":1}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_LOW,
        .availability = AEGIS_TOOL_READY,
        .execute = execute_grep_log
    };
}

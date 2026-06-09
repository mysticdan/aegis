#include <stdio.h>
#include <stdlib.h>

#include "aegis/tool.h"
#include "aegis/tool_path.h"

static AegisStatus execute_read_log(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    const char *path;
    char resolved[AEGIS_CONFIG_PATH_MAX];
    FILE *file;
    long length;
    char *buffer;
    AegisStatus status;

    if (!args || !context || !out) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    path = aegis_tool_args_get(args, "path");
    if (!path) {
        aegis_tool_result_set_error(out, "missing path");
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    status = aegis_tool_resolve_path(
        context, path, 0, resolved, sizeof(resolved));
    if (status != AEGIS_OK) {
        aegis_tool_result_set_error(out, "log path is not accessible");
        return status;
    }
    file = fopen(resolved, "rb");
    if (!file) {
        return AEGIS_ERR_IO;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return AEGIS_ERR_IO;
    }
    if ((size_t)length > context->max_output_bytes) {
        if (fseek(file, length - (long)context->max_output_bytes, SEEK_SET) != 0) {
            fclose(file);
            return AEGIS_ERR_IO;
        }
        length = (long)context->max_output_bytes;
    }
    buffer = malloc((size_t)length + 1U);
    if (!buffer) {
        fclose(file);
        return AEGIS_ERR_OOM;
    }
    if (length && fread(buffer, 1U, (size_t)length, file) != (size_t)length) {
        free(buffer);
        fclose(file);
        return AEGIS_ERR_IO;
    }
    fclose(file);
    buffer[length] = '\0';
    status = aegis_tool_result_set_stdout(out, buffer);
    free(buffer);
    return status;
}

AegisTool aegis_tool_read_log(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_READ_LOG,
        .description = "Read an approved log file.",
        .schema_json =
            "{\"type\":\"object\",\"required\":[\"path\"],"
            "\"properties\":{\"path\":{\"type\":\"string\",\"minLength\":1}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_LOW,
        .availability = AEGIS_TOOL_READY,
        .execute = execute_read_log
    };
}

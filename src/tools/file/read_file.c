#include <stdio.h>
#include <stdlib.h>

#include "aegis/tool.h"
#include "../tool_path.h"

static AegisStatus execute_read_file(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    const char *path;
    char full_path[AEGIS_CONFIG_PATH_MAX];
    FILE *file;
    long size;
    size_t limit;
    size_t bytes_read;
    char *buffer;
    AegisStatus status;

    if (!args || !context || !context->config || !out) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }

    path = aegis_tool_args_get(args, "path");
    if (!path) {
        aegis_tool_result_set_error(out, "missing path");
        return AEGIS_ERR_INVALID_ARGUMENT;
    }

    status = aegis_tool_resolve_path(
        context,
        path,
        0,
        full_path,
        sizeof(full_path)
    );
    if (status != AEGIS_OK) {
        aegis_tool_result_set_error(out, "path is not readable");
        return status;
    }

    file = fopen(full_path, "rb");
    if (!file) {
        aegis_tool_result_set_error(out, "failed to open file");
        return AEGIS_ERR_IO;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        aegis_tool_result_set_error(out, "failed to inspect file");
        return AEGIS_ERR_IO;
    }

    limit = (size_t)context->config->max_file_bytes;
    if (context->max_output_bytes > 0 && context->max_output_bytes < limit) {
        limit = context->max_output_bytes;
    }
    if ((size_t)size > limit) {
        fclose(file);
        aegis_tool_result_set_error(out, "file exceeds configured limit");
        return AEGIS_ERR_RUNTIME;
    }

    buffer = malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return AEGIS_ERR_OOM;
    }
    bytes_read = fread(buffer, 1, (size_t)size, file);
    if (bytes_read != (size_t)size || ferror(file)) {
        free(buffer);
        fclose(file);
        aegis_tool_result_set_error(out, "failed to read file");
        return AEGIS_ERR_IO;
    }
    buffer[bytes_read] = '\0';
    fclose(file);

    status = aegis_tool_result_set_stdout(out, buffer);
    free(buffer);
    return status;
}

AegisTool aegis_tool_read_file(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_READ_FILE,
        .description = "Read a text file inside the configured workspace.",
        .schema_json =
            "{\"type\":\"object\",\"required\":[\"path\"],"
            "\"properties\":{\"path\":{\"type\":\"string\",\"minLength\":1}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_LOW,
        .availability = AEGIS_TOOL_READY,
        .execute = execute_read_file
    };
}

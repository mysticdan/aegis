#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "aegis/tool.h"
#include "../tool_path.h"

static AegisStatus execute_append_file(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    const char *path;
    const char *content;
    char full_path[AEGIS_CONFIG_PATH_MAX];
    struct stat info;
    FILE *file;
    size_t current_size = 0;
    size_t length;
    AegisStatus status;

    if (!args || !context || !context->config || !out) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    if (!context->allow_write) {
        aegis_tool_result_set_error(out, "append_file disabled by context");
        return AEGIS_ERR_POLICY_DENIED;
    }

    path = aegis_tool_args_get(args, "path");
    content = aegis_tool_args_get(args, "content");
    if (!path || !content) {
        aegis_tool_result_set_error(out, "missing path or content");
        return AEGIS_ERR_INVALID_ARGUMENT;
    }

    status = aegis_tool_resolve_path(
        context,
        path,
        1,
        full_path,
        sizeof(full_path)
    );
    if (status != AEGIS_OK) {
        aegis_tool_result_set_error(out, "path is not appendable");
        return status;
    }

    if (stat(full_path, &info) == 0) {
        if (info.st_size < 0) {
            return AEGIS_ERR_IO;
        }
        current_size = (size_t)info.st_size;
    }
    length = strlen(content);
    if (current_size > (size_t)context->config->max_file_bytes ||
        length > (size_t)context->config->max_file_bytes - current_size) {
        aegis_tool_result_set_error(out, "resulting file exceeds configured limit");
        return AEGIS_ERR_RUNTIME;
    }

    file = fopen(full_path, "ab");
    if (!file) {
        aegis_tool_result_set_error(out, "failed to open file for appending");
        return AEGIS_ERR_IO;
    }
    if (length > 0 && fwrite(content, 1, length, file) != length) {
        fclose(file);
        aegis_tool_result_set_error(out, "failed to append file");
        return AEGIS_ERR_IO;
    }
    if (fclose(file) != 0) {
        aegis_tool_result_set_error(out, "failed to close appended file");
        return AEGIS_ERR_IO;
    }

    return aegis_tool_result_set_stdout(out, "ok");
}

AegisTool aegis_tool_append_file(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_APPEND_FILE,
        .description = "Append content to a file inside the workspace.",
        .schema_json =
            "{\"type\":\"object\",\"required\":[\"path\",\"content\"],"
            "\"properties\":{\"path\":{\"type\":\"string\",\"minLength\":1},"
            "\"content\":{\"type\":\"string\"}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_MEDIUM,
        .availability = AEGIS_TOOL_READY,
        .execute = execute_append_file
    };
}

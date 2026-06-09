#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aegis/tool.h"
#include "aegis/tool_path.h"

static AegisStatus append_text(
    char **buffer,
    size_t *length,
    size_t *capacity,
    const char *text,
    size_t limit
)
{
    size_t text_length = strlen(text);
    size_t required = *length + text_length + 1;
    size_t next_capacity;
    char *resized;

    if (limit > 0 && required - 1 > limit) {
        return AEGIS_ERR_RUNTIME;
    }
    if (required <= *capacity) {
        memcpy(*buffer + *length, text, text_length + 1);
        *length += text_length;
        return AEGIS_OK;
    }

    next_capacity = *capacity ? *capacity : 256;
    while (next_capacity < required) {
        if (next_capacity > SIZE_MAX / 2) {
            return AEGIS_ERR_OOM;
        }
        next_capacity *= 2;
    }

    resized = realloc(*buffer, next_capacity);
    if (!resized) {
        return AEGIS_ERR_OOM;
    }
    *buffer = resized;
    *capacity = next_capacity;
    memcpy(*buffer + *length, text, text_length + 1);
    *length += text_length;
    return AEGIS_OK;
}

static AegisStatus execute_list_dir(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    const char *path = args ? aegis_tool_args_get(args, "path") : NULL;
    char full_path[AEGIS_CONFIG_PATH_MAX];
    DIR *directory;
    struct dirent *entry;
    char entry_path[AEGIS_CONFIG_PATH_MAX];
    char visible_path[AEGIS_CONFIG_PATH_MAX];
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    AegisStatus status;
    int written;

    if (!context || !context->config || !out) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    if (!path) {
        path = ".";
    }

    status = aegis_tool_resolve_path(
        context,
        path,
        0,
        full_path,
        sizeof(full_path)
    );
    if (status != AEGIS_OK) {
        aegis_tool_result_set_error(out, "directory path is not accessible");
        return status;
    }

    directory = opendir(full_path);
    if (!directory) {
        aegis_tool_result_set_error(out, "failed to open directory");
        return AEGIS_ERR_IO;
    }

    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!context->config->allow_hidden_files &&
            entry->d_name[0] == '.') {
            continue;
        }
        if (strcmp(path, ".") == 0) {
            written = snprintf(
                entry_path,
                sizeof(entry_path),
                "%s",
                entry->d_name
            );
            if (written < 0 || (size_t)written >= sizeof(entry_path)) {
                continue;
            }
        } else {
            written = snprintf(
                entry_path,
                sizeof(entry_path),
                "%s/%s",
                path,
                entry->d_name
            );
            if (written < 0 || (size_t)written >= sizeof(entry_path)) {
                continue;
            }
        }
        if (aegis_tool_resolve_path(
                context,
                entry_path,
                0,
                visible_path,
                sizeof(visible_path)) != AEGIS_OK) {
            continue;
        }

        status = append_text(
            &buffer,
            &length,
            &capacity,
            entry->d_name,
            context->max_output_bytes
        );
        if (status == AEGIS_OK) {
            status = append_text(
                &buffer,
                &length,
                &capacity,
                "\n",
                context->max_output_bytes
            );
        }
        if (status != AEGIS_OK) {
            free(buffer);
            closedir(directory);
            aegis_tool_result_set_error(out, "directory output exceeds limit");
            return status;
        }
    }
    closedir(directory);

    status = aegis_tool_result_set_stdout(out, buffer ? buffer : "");
    free(buffer);
    return status;
}

AegisTool aegis_tool_list_dir(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_LIST_DIR,
        .description = "List entries inside a workspace directory.",
        .schema_json =
            "{\"type\":\"object\",\"properties\":{"
            "\"path\":{\"type\":\"string\",\"minLength\":1}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_LOW,
        .availability = AEGIS_TOOL_READY,
        .execute = execute_list_dir
    };
}

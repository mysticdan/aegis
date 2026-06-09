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

static int compare_names(const void *left, const void *right)
{
    const char *const *left_name = left;
    const char *const *right_name = right;

    return strcmp(*left_name, *right_name);
}

static void free_names(char **names, size_t count)
{
    size_t index;

    for (index = 0U; index < count; ++index) {
        free(names[index]);
    }
    free(names);
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
    char **names = NULL;
    size_t name_count = 0U;
    size_t name_capacity = 0U;
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
        if (name_count == name_capacity) {
            size_t next = name_capacity ? name_capacity * 2U : 16U;
            char **resized = realloc(names, next * sizeof(*names));

            if (!resized) {
                free_names(names, name_count);
                closedir(directory);
                return AEGIS_ERR_OOM;
            }
            names = resized;
            name_capacity = next;
        }
        names[name_count] = malloc(strlen(entry->d_name) + 1U);
        if (!names[name_count]) {
            free_names(names, name_count);
            closedir(directory);
            return AEGIS_ERR_OOM;
        }
        memcpy(
            names[name_count],
            entry->d_name,
            strlen(entry->d_name) + 1U
        );
        ++name_count;
    }
    closedir(directory);

    qsort(names, name_count, sizeof(*names), compare_names);
    for (size_t index = 0U; index < name_count; ++index) {
        status = append_text(
            &buffer,
            &length,
            &capacity,
            names[index],
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
            free_names(names, name_count);
            free(buffer);
            aegis_tool_result_set_error(out, "directory output exceeds limit");
            return status;
        }
    }
    free_names(names, name_count);
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

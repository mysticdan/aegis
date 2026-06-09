#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "aegis/tool.h"
#include "aegis/tool_path.h"

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} SearchBuffer;

static AegisStatus append_match(
    SearchBuffer *buffer,
    const char *path,
    size_t line_number,
    const char *line,
    size_t maximum
)
{
    int prefix_length;
    size_t line_length = strlen(line);
    size_t required;
    size_t capacity;
    char *resized;

    prefix_length = snprintf(NULL, 0, "%s:%zu:", path, line_number);
    if (prefix_length < 0) {
        return AEGIS_ERR_RUNTIME;
    }
    required = buffer->length + (size_t)prefix_length + line_length + 2U;
    if (required - 1U > maximum) {
        return AEGIS_ERR_RUNTIME;
    }
    if (required > buffer->capacity) {
        capacity = buffer->capacity ? buffer->capacity : 4096U;
        while (capacity < required) {
            capacity *= 2U;
        }
        resized = realloc(buffer->data, capacity);
        if (!resized) {
            return AEGIS_ERR_OOM;
        }
        buffer->data = resized;
        buffer->capacity = capacity;
    }
    snprintf(
        buffer->data + buffer->length,
        buffer->capacity - buffer->length,
        "%s:%zu:%s%s",
        path,
        line_number,
        line,
        line_length && line[line_length - 1U] == '\n' ? "" : "\n"
    );
    buffer->length = strlen(buffer->data);
    return AEGIS_OK;
}

static AegisStatus search_regular_file(
    const AegisToolContext *context,
    const char *relative,
    const char *query,
    SearchBuffer *buffer
)
{
    char resolved[AEGIS_CONFIG_PATH_MAX];
    FILE *file;
    char *line = NULL;
    size_t capacity = 0U;
    size_t line_number = 0U;
    ssize_t length;
    AegisStatus status;

    status = aegis_tool_resolve_path(
        context, relative, 0, resolved, sizeof(resolved));
    if (status != AEGIS_OK) {
        return AEGIS_OK;
    }
    file = fopen(resolved, "rb");
    if (!file) {
        return AEGIS_OK;
    }
    while ((length = getline(&line, &capacity, file)) >= 0) {
        ++line_number;
        if ((size_t)length > (size_t)context->config->max_file_bytes ||
            memchr(line, '\0', (size_t)length)) {
            break;
        }
        if (strstr(line, query)) {
            status = append_match(
                buffer,
                relative,
                line_number,
                line,
                context->max_output_bytes
                    ? context->max_output_bytes
                    : 65536U
            );
            if (status != AEGIS_OK) {
                free(line);
                fclose(file);
                return status;
            }
        }
    }
    free(line);
    fclose(file);
    return AEGIS_OK;
}

static AegisStatus search_tree(
    const AegisToolContext *context,
    const char *relative,
    const char *query,
    SearchBuffer *buffer,
    unsigned int depth
)
{
    char resolved[AEGIS_CONFIG_PATH_MAX];
    struct stat metadata;
    DIR *directory;
    struct dirent *entry;
    AegisStatus status;

    if (depth > 64U) {
        return AEGIS_ERR_RUNTIME;
    }
    status = aegis_tool_resolve_path(
        context, relative, 0, resolved, sizeof(resolved));
    if (status != AEGIS_OK) {
        return depth == 0U ? status : AEGIS_OK;
    }
    if (lstat(resolved, &metadata) != 0) {
        if (depth > 0U) {
            return AEGIS_OK;
        }
        return errno == ENOENT ? AEGIS_ERR_NOT_FOUND : AEGIS_ERR_IO;
    }
    if (S_ISREG(metadata.st_mode)) {
        return search_regular_file(context, relative, query, buffer);
    }
    if (!S_ISDIR(metadata.st_mode)) {
        return AEGIS_OK;
    }
    directory = opendir(resolved);
    if (!directory) {
        return AEGIS_ERR_IO;
    }
    while ((entry = readdir(directory)) != NULL) {
        char child[AEGIS_CONFIG_PATH_MAX];
        int written;

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0 ||
            (!context->config->allow_hidden_files &&
             entry->d_name[0] == '.')) {
            continue;
        }
        written = strcmp(relative, ".") == 0
            ? snprintf(child, sizeof(child), "%s", entry->d_name)
            : snprintf(
                child, sizeof(child), "%s/%s", relative, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(child)) {
            continue;
        }
        status = search_tree(context, child, query, buffer, depth + 1U);
        if (status != AEGIS_OK) {
            closedir(directory);
            return status;
        }
    }
    closedir(directory);
    return AEGIS_OK;
}

static AegisStatus execute_search_file(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    const char *query;
    const char *path;
    SearchBuffer buffer = {0};
    AegisStatus status;

    if (!args || !context || !out) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    query = aegis_tool_args_get(args, "query");
    path = aegis_tool_args_get(args, "path");
    if (!query || !query[0]) {
        aegis_tool_result_set_error(out, "missing query");
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    status = search_tree(
        context, path && path[0] ? path : ".", query, &buffer, 0U);
    if (status == AEGIS_OK) {
        status = aegis_tool_result_set_stdout(
            out, buffer.data ? buffer.data : "");
    } else {
        aegis_tool_result_set_error(out, "search failed or exceeded limit");
    }
    free(buffer.data);
    return status;
}

AegisTool aegis_tool_search_file(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_SEARCH_FILE,
        .description = "Search text inside workspace files.",
        .schema_json =
            "{\"type\":\"object\",\"required\":[\"query\"],"
            "\"properties\":{\"query\":{\"type\":\"string\",\"minLength\":1},"
            "\"path\":{\"type\":\"string\",\"minLength\":1}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_LOW,
        .availability = AEGIS_TOOL_READY,
        .execute = execute_search_file
    };
}

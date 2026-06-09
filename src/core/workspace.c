#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aegis/workspace.h"

static int path_is_within(const char *root, const char *path)
{
    size_t length = strlen(root);

    if (length == 1U && root[0] == '/') {
        return path[0] == '/';
    }
    return strncmp(root, path, length) == 0 &&
        (path[length] == '\0' || path[length] == '/');
}

int aegis_workspace_is_safe_relative_path(const char *path)
{
    const char *cursor;

    if (!path || !path[0] || path[0] == '/') {
        return 0;
    }
    cursor = path;
    while (*cursor) {
        const char *end;
        size_t length;

        while (*cursor == '/') {
            ++cursor;
        }
        end = strchr(cursor, '/');
        length = end ? (size_t)(end - cursor) : strlen(cursor);
        if (length == 2U &&
            cursor[0] == '.' && cursor[1] == '.') {
            return 0;
        }
        cursor = end ? end : cursor + length;
    }
    return 1;
}

AegisStatus aegis_workspace_init(AegisWorkspace *workspace, const char *root)
{
    char *resolved;
    size_t length;

    if (!workspace || !root || !root[0]) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    resolved = realpath(root, NULL);
    if (!resolved) {
        return AEGIS_ERR_NOT_FOUND;
    }
    length = strlen(resolved);
    if (length >= sizeof(workspace->root)) {
        free(resolved);
        return AEGIS_ERR_PATH_ESCAPE;
    }
    memcpy(workspace->root, resolved, length + 1U);
    free(resolved);
    return AEGIS_OK;
}

AegisStatus aegis_workspace_resolve(
    const AegisWorkspace *workspace,
    const char *requested,
    char *output,
    size_t output_size
)
{
    char candidate[AEGIS_PATH_MAX * 2U];
    char *resolved;
    int written;
    size_t length;

    if (!workspace || !workspace->root[0] ||
        !aegis_workspace_is_safe_relative_path(requested) ||
        !output || output_size == 0U) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    written = snprintf(
        candidate,
        sizeof(candidate),
        "%s/%s",
        workspace->root,
        requested
    );
    if (written < 0 || (size_t)written >= sizeof(candidate)) {
        return AEGIS_ERR_PATH_ESCAPE;
    }
    resolved = realpath(candidate, NULL);
    if (!resolved) {
        return AEGIS_ERR_NOT_FOUND;
    }
    if (!path_is_within(workspace->root, resolved)) {
        free(resolved);
        return AEGIS_ERR_PATH_ESCAPE;
    }
    length = strlen(resolved);
    if (length >= output_size) {
        free(resolved);
        return AEGIS_ERR_PATH_ESCAPE;
    }
    memcpy(output, resolved, length + 1U);
    free(resolved);
    return AEGIS_OK;
}

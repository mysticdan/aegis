#define _XOPEN_SOURCE 700

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "aegis/tool_path.h"

static int copy_text(char *destination, size_t size, const char *source)
{
    size_t length;

    if (!destination || size == 0 || !source) {
        return 0;
    }

    length = strlen(source);
    if (length >= size) {
        return 0;
    }

    memcpy(destination, source, length + 1);
    return 1;
}

static int path_is_within(const char *root, const char *path)
{
    size_t root_length;

    root_length = strlen(root);
    return strncmp(root, path, root_length) == 0 &&
        (path[root_length] == '\0' || path[root_length] == '/');
}

static int path_has_segment(const char *path, const char *segment)
{
    const char *cursor = path;
    size_t segment_length = strlen(segment);

    while (*cursor) {
        const char *end = strchr(cursor, '/');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);

        if (length == segment_length &&
            strncmp(cursor, segment, length) == 0) {
            return 1;
        }
        if (!end) {
            break;
        }
        cursor = end + 1;
    }

    return 0;
}

static int path_is_blocked(
    const AegisConfigStringList *blocked_paths,
    const char *normalized
)
{
    size_t index;

    for (index = 0; index < blocked_paths->count; ++index) {
        const char *blocked = blocked_paths->items[index];
        size_t length = strlen(blocked);

        if (strchr(blocked, '/')) {
            if (strncmp(normalized, blocked, length) == 0 &&
                (normalized[length] == '\0' ||
                 normalized[length] == '/')) {
                return 1;
            }
        } else if (path_has_segment(normalized, blocked)) {
            return 1;
        }
    }

    return 0;
}

static AegisStatus normalize_relative_path(
    const AegisConfig *config,
    const char *path,
    char *normalized,
    size_t normalized_size
)
{
    char copy[AEGIS_CONFIG_PATH_MAX];
    char *save = NULL;
    char *segment;
    size_t used = 0;

    if (!path || path[0] == '\0' || path[0] == '/' ||
        !copy_text(copy, sizeof(copy), path)) {
        return AEGIS_ERR_PATH_ESCAPE;
    }

    normalized[0] = '\0';
    segment = strtok_r(copy, "/", &save);
    while (segment) {
        size_t length = strlen(segment);

        if (strcmp(segment, "..") == 0) {
            return AEGIS_ERR_PATH_ESCAPE;
        }
        if (strcmp(segment, ".") != 0) {
            if (!config->allow_hidden_files && segment[0] == '.') {
                return AEGIS_ERR_PATH_ESCAPE;
            }
            if (used + (used ? 1U : 0U) + length >= normalized_size) {
                return AEGIS_ERR_PATH_ESCAPE;
            }
            if (used) {
                normalized[used++] = '/';
            }
            memcpy(normalized + used, segment, length);
            used += length;
            normalized[used] = '\0';
        }
        segment = strtok_r(NULL, "/", &save);
    }

    if (used == 0) {
        if (!copy_text(normalized, normalized_size, ".")) {
            return AEGIS_ERR_PATH_ESCAPE;
        }
    }
    if (path_is_blocked(&config->blocked_paths, normalized)) {
        return AEGIS_ERR_PATH_ESCAPE;
    }

    return AEGIS_OK;
}

static AegisStatus reject_symlink_components(
    const char *root,
    const char *normalized,
    int allow_missing_leaf
)
{
    char copy[AEGIS_CONFIG_PATH_MAX];
    char current[AEGIS_CONFIG_PATH_MAX];
    char *save = NULL;
    char *segment;
    struct stat info;
    int written;
    size_t current_length;
    size_t remaining;

    if (!copy_text(copy, sizeof(copy), normalized) ||
        !copy_text(current, sizeof(current), root)) {
        return AEGIS_ERR_PATH_ESCAPE;
    }

    segment = strtok_r(copy, "/", &save);
    while (segment) {
        current_length = strlen(current);
        remaining = sizeof(current) - current_length;
        written = snprintf(
            current + current_length,
            remaining,
            "/%s",
            segment
        );
        if (written < 0 || (size_t)written >= remaining) {
            return AEGIS_ERR_PATH_ESCAPE;
        }

        if (lstat(current, &info) != 0) {
            if (errno == ENOENT && allow_missing_leaf &&
                strtok_r(NULL, "/", &save) == NULL) {
                return AEGIS_OK;
            }
            return errno == ENOENT ? AEGIS_ERR_NOT_FOUND : AEGIS_ERR_IO;
        }
        if (S_ISLNK(info.st_mode)) {
            return AEGIS_ERR_PATH_ESCAPE;
        }

        segment = strtok_r(NULL, "/", &save);
    }

    return AEGIS_OK;
}

AegisStatus aegis_tool_resolve_path(
    const AegisToolContext *context,
    const char *path,
    int allow_missing_leaf,
    char *resolved,
    size_t resolved_size
)
{
    const AegisConfig *config;
    const char *workspace_root;
    char root_real[AEGIS_CONFIG_PATH_MAX];
    char normalized[AEGIS_CONFIG_PATH_MAX];
    char candidate[AEGIS_CONFIG_PATH_MAX];
    char target_real[AEGIS_CONFIG_PATH_MAX];
    char parent[AEGIS_CONFIG_PATH_MAX];
    char *slash;
    struct stat info;
    AegisStatus status;
    int written;

    if (!context || !context->config || !resolved || resolved_size == 0) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }

    config = context->config;
    workspace_root = context->workspace_root
        ? context->workspace_root
        : config->workspace_root;
    if (!realpath(workspace_root, root_real)) {
        return AEGIS_ERR_IO;
    }

    status = normalize_relative_path(
        config,
        path,
        normalized,
        sizeof(normalized)
    );
    if (status != AEGIS_OK) {
        return status;
    }

    written = snprintf(
        candidate,
        sizeof(candidate),
        "%s/%s",
        root_real,
        normalized
    );
    if (written < 0 || (size_t)written >= sizeof(candidate)) {
        return AEGIS_ERR_PATH_ESCAPE;
    }

    if (!config->follow_symlinks) {
        status = reject_symlink_components(
            root_real,
            normalized,
            allow_missing_leaf
        );
        if (status != AEGIS_OK) {
            return status;
        }
    }

    if (lstat(candidate, &info) == 0) {
        if (!realpath(candidate, target_real) ||
            !path_is_within(root_real, target_real)) {
            return AEGIS_ERR_PATH_ESCAPE;
        }
        return copy_text(resolved, resolved_size, target_real)
            ? AEGIS_OK
            : AEGIS_ERR_PATH_ESCAPE;
    }
    if (errno != ENOENT || !allow_missing_leaf) {
        return errno == ENOENT ? AEGIS_ERR_NOT_FOUND : AEGIS_ERR_IO;
    }

    if (!copy_text(parent, sizeof(parent), candidate)) {
        return AEGIS_ERR_PATH_ESCAPE;
    }
    slash = strrchr(parent, '/');
    if (!slash || slash == parent) {
        return AEGIS_ERR_PATH_ESCAPE;
    }
    *slash = '\0';
    if (!realpath(parent, target_real) ||
        !path_is_within(root_real, target_real)) {
        return AEGIS_ERR_PATH_ESCAPE;
    }

    return copy_text(resolved, resolved_size, candidate)
        ? AEGIS_OK
        : AEGIS_ERR_PATH_ESCAPE;
}

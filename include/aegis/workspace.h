#ifndef AEGIS_WORKSPACE_H
#define AEGIS_WORKSPACE_H

#include <stddef.h>
#include "aegis/error.h"

#ifndef AEGIS_PATH_MAX
#define AEGIS_PATH_MAX 4096
#endif

typedef struct {
    char root[AEGIS_PATH_MAX];
} AegisWorkspace;

AegisStatus aegis_workspace_init(AegisWorkspace *ws, const char *root);
AegisStatus aegis_workspace_resolve(
    const AegisWorkspace *ws,
    const char *requested,
    char *out,
    size_t out_size
);
int aegis_workspace_is_safe_relative_path(const char *path);

#endif

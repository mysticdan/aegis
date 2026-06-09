#ifndef AEGIS_TOOL_PATH_H
#define AEGIS_TOOL_PATH_H

#include <stddef.h>

#include "aegis/tool.h"

AegisStatus aegis_tool_resolve_path(
    const AegisToolContext *context,
    const char *path,
    int allow_missing_leaf,
    char *resolved,
    size_t resolved_size
);

#endif

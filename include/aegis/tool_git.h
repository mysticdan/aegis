#ifndef AEGIS_TOOL_GIT_H
#define AEGIS_TOOL_GIT_H

#include "aegis/tool.h"

AegisStatus aegis_tool_run_git(
    const AegisToolContext *context,
    const char *const arguments[],
    const char *stdin_data,
    AegisToolResult *result
);

#endif

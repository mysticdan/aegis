#ifndef AEGIS_TOOL_PROCESS_H
#define AEGIS_TOOL_PROCESS_H

#include "aegis/tool.h"

int aegis_command_policy_is_blocked(const char *command);
int aegis_command_policy_allows(
    const AegisConfig *config,
    const char *command
);
AegisStatus aegis_tool_run_process(
    const AegisToolContext *context,
    const char *const argv[],
    const char *stdin_data,
    int timeout_ms,
    AegisToolResult *result
);
AegisStatus aegis_tool_run_shell_command(
    const AegisToolContext *context,
    const char *command,
    AegisToolResult *result
);

#endif

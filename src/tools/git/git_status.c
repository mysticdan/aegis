#include "aegis/tool.h"
#include "aegis/tool_git.h"

static AegisStatus execute_git_status(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    const char *arguments[] = {"status", "--short", "--branch", NULL};

    (void)args;
    return aegis_tool_run_git(context, arguments, NULL, out);
}

AegisTool aegis_tool_git_status(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_GIT_STATUS,
        .description = "Show concise repository status.",
        .schema_json =
            "{\"type\":\"object\",\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_LOW,
        .availability = AEGIS_TOOL_READY,
        .execute = execute_git_status
    };
}

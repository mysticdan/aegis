#include "aegis/tool.h"

static AegisStatus execute_git_status(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    (void)args;
    (void)context;
    aegis_tool_result_set_error(out, "git_status is not implemented");
    return AEGIS_ERR_NOT_IMPLEMENTED;
}

AegisTool aegis_tool_git_status(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_GIT_STATUS,
        .description = "Show concise repository status.",
        .schema_json =
            "{\"type\":\"object\",\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_LOW,
        .availability = AEGIS_TOOL_STUB,
        .execute = execute_git_status
    };
}

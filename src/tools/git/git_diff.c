#include "aegis/tool.h"

static AegisStatus execute_git_diff(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    (void)args;
    (void)context;
    aegis_tool_result_set_error(out, "git_diff is not implemented");
    return AEGIS_ERR_NOT_IMPLEMENTED;
}

AegisTool aegis_tool_git_diff(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_GIT_DIFF,
        .description = "Show repository changes.",
        .schema_json =
            "{\"type\":\"object\",\"properties\":{"
            "\"path\":{\"type\":\"string\"}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_LOW,
        .availability = AEGIS_TOOL_STUB,
        .execute = execute_git_diff
    };
}

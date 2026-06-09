#include "aegis/tool.h"

static AegisStatus execute_git_log(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    (void)args;
    (void)context;
    aegis_tool_result_set_error(out, "git_log is not implemented");
    return AEGIS_ERR_NOT_IMPLEMENTED;
}

AegisTool aegis_tool_git_log(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_GIT_LOG,
        .description = "Show recent repository history.",
        .schema_json =
            "{\"type\":\"object\",\"properties\":{"
            "\"limit\":{\"type\":\"integer\",\"minimum\":1}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_LOW,
        .availability = AEGIS_TOOL_STUB,
        .execute = execute_git_log
    };
}

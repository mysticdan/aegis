#include "aegis/tool.h"

static AegisStatus execute_git_apply_patch(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    (void)args;
    (void)context;
    aegis_tool_result_set_error(out, "git_apply_patch is not implemented");
    return AEGIS_ERR_NOT_IMPLEMENTED;
}

AegisTool aegis_tool_git_apply_patch(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_GIT_APPLY_PATCH,
        .description = "Apply a patch to the workspace.",
        .schema_json =
            "{\"type\":\"object\",\"required\":[\"patch\"],"
            "\"properties\":{\"patch\":{\"type\":\"string\",\"minLength\":1}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_MEDIUM,
        .availability = AEGIS_TOOL_STUB,
        .execute = execute_git_apply_patch
    };
}

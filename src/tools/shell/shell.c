#include "aegis/tool.h"

static AegisStatus execute_shell(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    (void)args;
    (void)context;
    aegis_tool_result_set_error(out, "shell is not implemented");
    return AEGIS_ERR_NOT_IMPLEMENTED;
}

AegisTool aegis_tool_shell(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_SHELL,
        .description = "Run a policy-approved command inside the workspace.",
        .schema_json =
            "{\"type\":\"object\",\"required\":[\"command\"],"
            "\"properties\":{\"command\":{\"type\":\"string\",\"minLength\":1}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_HIGH,
        .availability = AEGIS_TOOL_STUB,
        .execute = execute_shell
    };
}

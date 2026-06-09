#include "aegis/tool.h"

static AegisStatus execute_ask_user(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    (void)args;
    (void)context;
    aegis_tool_result_set_error(out, "ask_user is not implemented");
    return AEGIS_ERR_NOT_IMPLEMENTED;
}

AegisTool aegis_tool_ask_user(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_ASK_USER,
        .description = "Ask the user for input.",
        .schema_json =
            "{\"type\":\"object\",\"required\":[\"question\"],"
            "\"properties\":{\"question\":{\"type\":\"string\",\"minLength\":1}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_LOW,
        .availability = AEGIS_TOOL_STUB,
        .execute = execute_ask_user
    };
}

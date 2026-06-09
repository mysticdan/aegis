#include "aegis/tool.h"

static AegisStatus execute_send_message(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    (void)args;
    (void)context;
    aegis_tool_result_set_error(out, "send_message is not implemented");
    return AEGIS_ERR_NOT_IMPLEMENTED;
}

AegisTool aegis_tool_send_message(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_SEND_MESSAGE,
        .description = "Send a message through the active adapter.",
        .schema_json =
            "{\"type\":\"object\",\"required\":[\"message\"],"
            "\"properties\":{\"message\":{\"type\":\"string\",\"minLength\":1}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_CRITICAL,
        .availability = AEGIS_TOOL_STUB,
        .execute = execute_send_message
    };
}

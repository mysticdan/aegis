#include "aegis/tool.h"

static AegisStatus execute_send_message(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    const char *message;

    if (!args || !context || !out) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    message = aegis_tool_args_get(args, "message");
    if (!message || !message[0]) {
        aegis_tool_result_set_error(out, "missing message");
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    if (!context->send_message ||
        !context->send_message(context->adapter_userdata, message)) {
        aegis_tool_result_set_error(out, "active adapter cannot send messages");
        return AEGIS_ERR_RUNTIME;
    }
    return aegis_tool_result_set_stdout(out, "sent");
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
        .availability = AEGIS_TOOL_READY,
        .execute = execute_send_message
    };
}

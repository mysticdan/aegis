#include "aegis/tool.h"

static AegisStatus execute_reminder(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    (void)args;
    (void)context;
    aegis_tool_result_set_error(out, "reminder is not implemented");
    return AEGIS_ERR_NOT_IMPLEMENTED;
}

AegisTool aegis_tool_reminder(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_REMINDER,
        .description = "Create a reminder.",
        .schema_json =
            "{\"type\":\"object\",\"required\":[\"message\"],"
            "\"properties\":{\"message\":{\"type\":\"string\",\"minLength\":1},"
            "\"due\":{\"type\":\"string\"}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_MEDIUM,
        .availability = AEGIS_TOOL_STUB,
        .execute = execute_reminder
    };
}

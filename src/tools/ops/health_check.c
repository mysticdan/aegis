#include "aegis/tool.h"

static AegisStatus execute_health_check(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    (void)args;
    (void)context;
    aegis_tool_result_set_error(out, "health_check is not implemented");
    return AEGIS_ERR_NOT_IMPLEMENTED;
}

AegisTool aegis_tool_health_check(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_HEALTH_CHECK,
        .description = "Check the health of an approved service endpoint.",
        .schema_json =
            "{\"type\":\"object\",\"required\":[\"url\"],"
            "\"properties\":{\"url\":{\"type\":\"string\",\"format\":\"uri\"}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_MEDIUM,
        .availability = AEGIS_TOOL_STUB,
        .execute = execute_health_check
    };
}

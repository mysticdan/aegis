#include "aegis/tool.h"

static AegisStatus execute_http_get(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    (void)args;
    (void)context;
    aegis_tool_result_set_error(out, "http_get is not implemented");
    return AEGIS_ERR_NOT_IMPLEMENTED;
}

AegisTool aegis_tool_http_get(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_HTTP_GET,
        .description = "Perform an HTTP GET request.",
        .schema_json =
            "{\"type\":\"object\",\"required\":[\"url\"],"
            "\"properties\":{\"url\":{\"type\":\"string\",\"format\":\"uri\"}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_HIGH,
        .availability = AEGIS_TOOL_STUB,
        .execute = execute_http_get
    };
}

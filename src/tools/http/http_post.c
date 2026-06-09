#include "aegis/tool.h"

static AegisStatus execute_http_post(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    (void)args;
    (void)context;
    aegis_tool_result_set_error(out, "http_post is not implemented");
    return AEGIS_ERR_NOT_IMPLEMENTED;
}

AegisTool aegis_tool_http_post(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_HTTP_POST,
        .description = "Perform an HTTP POST request.",
        .schema_json =
            "{\"type\":\"object\",\"required\":[\"url\"],"
            "\"properties\":{\"url\":{\"type\":\"string\",\"format\":\"uri\"},"
            "\"body\":{\"type\":\"string\"}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_CRITICAL,
        .availability = AEGIS_TOOL_STUB,
        .execute = execute_http_post
    };
}

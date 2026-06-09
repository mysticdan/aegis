#include "aegis/tool.h"
#include "aegis/tool_http.h"

static AegisStatus execute_http_post(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    const char *url;
    const char *body;

    if (!args || !context || !out) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    url = aegis_tool_args_get(args, "url");
    body = aegis_tool_args_get(args, "body");
    if (!url) {
        aegis_tool_result_set_error(out, "missing url");
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    return aegis_tool_http_request(context, "POST", url, body, out);
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
        .availability = AEGIS_TOOL_READY,
        .execute = execute_http_post
    };
}

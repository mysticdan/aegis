#include "aegis/tool.h"
#include "aegis/tool_http.h"

static AegisStatus execute_health_check(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    const char *url;

    if (!args || !context || !out) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    url = aegis_tool_args_get(args, "url");
    if (!url) {
        aegis_tool_result_set_error(out, "missing url");
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    return aegis_tool_http_request(context, "GET", url, NULL, out);
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
        .availability = AEGIS_TOOL_READY,
        .execute = execute_health_check
    };
}

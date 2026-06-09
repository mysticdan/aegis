#include "aegis/tool.h"

static AegisStatus execute_mcp_tool(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    (void)args;
    (void)context;
    aegis_tool_result_set_error(out, "mcp_tool is not implemented");
    return AEGIS_ERR_NOT_IMPLEMENTED;
}

AegisTool aegis_tool_mcp_tool(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_MCP,
        .description = "Call a tool exposed by an approved MCP server.",
        .schema_json =
            "{\"type\":\"object\",\"required\":[\"server\",\"tool\"],"
            "\"properties\":{\"server\":{\"type\":\"string\",\"minLength\":1},"
            "\"tool\":{\"type\":\"string\",\"minLength\":1},"
            "\"arguments\":{\"type\":\"string\"}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_HIGH,
        .availability = AEGIS_TOOL_STUB,
        .execute = execute_mcp_tool
    };
}

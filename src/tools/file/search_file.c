#include "aegis/tool.h"

static AegisStatus execute_search_file(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    (void)args;
    (void)context;
    aegis_tool_result_set_error(out, "search_file is not implemented");
    return AEGIS_ERR_NOT_IMPLEMENTED;
}

AegisTool aegis_tool_search_file(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_SEARCH_FILE,
        .description = "Search text inside workspace files.",
        .schema_json =
            "{\"type\":\"object\",\"required\":[\"query\"],"
            "\"properties\":{\"query\":{\"type\":\"string\",\"minLength\":1},"
            "\"path\":{\"type\":\"string\",\"minLength\":1}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_LOW,
        .availability = AEGIS_TOOL_STUB,
        .execute = execute_search_file
    };
}

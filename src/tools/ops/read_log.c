#include "aegis/tool.h"

static AegisStatus execute_read_log(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    (void)args;
    (void)context;
    aegis_tool_result_set_error(out, "read_log is not implemented");
    return AEGIS_ERR_NOT_IMPLEMENTED;
}

AegisTool aegis_tool_read_log(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_READ_LOG,
        .description = "Read an approved log file.",
        .schema_json =
            "{\"type\":\"object\",\"required\":[\"path\"],"
            "\"properties\":{\"path\":{\"type\":\"string\",\"minLength\":1}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_LOW,
        .availability = AEGIS_TOOL_STUB,
        .execute = execute_read_log
    };
}

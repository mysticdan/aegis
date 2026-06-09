#include "aegis/tool.h"

static AegisStatus execute_run_tests(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    (void)args;
    (void)context;
    aegis_tool_result_set_error(out, "run_tests is not implemented");
    return AEGIS_ERR_NOT_IMPLEMENTED;
}

AegisTool aegis_tool_run_tests(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_RUN_TESTS,
        .description = "Run the configured project test command.",
        .schema_json =
            "{\"type\":\"object\",\"properties\":{"
            "\"target\":{\"type\":\"string\"}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_MEDIUM,
        .availability = AEGIS_TOOL_STUB,
        .execute = execute_run_tests
    };
}

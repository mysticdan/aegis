#include "aegis/tool.h"
#include "aegis/tool_process.h"

static AegisStatus execute_run_tests(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    const char *target = args ? aegis_tool_args_get(args, "target") : NULL;
    const char *command = target && target[0]
        ? target
        : "ctest --test-dir build --output-on-failure";

    if (!context || !out) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    return aegis_tool_run_shell_command(context, command, out);
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
        .availability = AEGIS_TOOL_READY,
        .execute = execute_run_tests
    };
}

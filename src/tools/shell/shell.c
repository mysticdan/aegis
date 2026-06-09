#include "aegis/tool.h"
#include "aegis/tool_process.h"

static AegisStatus execute_shell(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    const char *command;

    if (!args || !context || !out) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    command = aegis_tool_args_get(args, "command");
    if (!command || !command[0]) {
        aegis_tool_result_set_error(out, "missing command");
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    if (!context->allow_shell) {
        aegis_tool_result_set_error(out, "shell is disabled");
        return AEGIS_ERR_POLICY_DENIED;
    }
    return aegis_tool_run_shell_command(context, command, out);
}

AegisTool aegis_tool_shell(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_SHELL,
        .description = "Run a policy-approved command inside the workspace.",
        .schema_json =
            "{\"type\":\"object\",\"required\":[\"command\"],"
            "\"properties\":{\"command\":{\"type\":\"string\",\"minLength\":1}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_HIGH,
        .availability = AEGIS_TOOL_READY,
        .execute = execute_shell
    };
}

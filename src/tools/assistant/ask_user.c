#include "aegis/tool.h"

#include <stdlib.h>

static AegisStatus execute_ask_user(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    const char *question;
    char *answer = NULL;
    AegisStatus status;

    if (!args || !context || !out) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    question = aegis_tool_args_get(args, "question");
    if (!question || !question[0]) {
        aegis_tool_result_set_error(out, "missing question");
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    if (!context->ask_user ||
        !context->ask_user(context->adapter_userdata, question, &answer)) {
        aegis_tool_result_set_error(out, "interactive input is unavailable");
        return AEGIS_ERR_APPROVAL_REJECTED;
    }
    status = aegis_tool_result_set_stdout(out, answer ? answer : "");
    free(answer);
    return status;
}

AegisTool aegis_tool_ask_user(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_ASK_USER,
        .description = "Ask the user for input.",
        .schema_json =
            "{\"type\":\"object\",\"required\":[\"question\"],"
            "\"properties\":{\"question\":{\"type\":\"string\",\"minLength\":1}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_LOW,
        .availability = AEGIS_TOOL_READY,
        .execute = execute_ask_user
    };
}

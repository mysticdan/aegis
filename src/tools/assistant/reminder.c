#include <stdlib.h>

#include <cjson/cJSON.h>

#include "aegis/tool.h"

static AegisStatus execute_reminder(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    const char *message;
    const char *due;
    cJSON *output;
    char *rendered;
    AegisStatus status;

    if (!args || !context || !out) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    message = aegis_tool_args_get(args, "message");
    due = aegis_tool_args_get(args, "due");
    if (!message || !message[0]) {
        aegis_tool_result_set_error(out, "missing reminder message");
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    if (!context->persist_reminder || !context->session_id) {
        aegis_tool_result_set_error(out, "state storage is unavailable");
        return AEGIS_ERR_STATE;
    }
    status = context->persist_reminder(
        context->state_userdata,
        context->session_id,
        message,
        due
    );
    if (status != AEGIS_OK) {
        aegis_tool_result_set_error(out, "failed to persist reminder");
        return status;
    }
    output = cJSON_CreateObject();
    if (!output) {
        return AEGIS_ERR_OOM;
    }
    cJSON_AddStringToObject(output, "status", "scheduled");
    cJSON_AddStringToObject(output, "due", due ? due : "");
    cJSON_AddStringToObject(output, "message", message);
    rendered = cJSON_PrintUnformatted(output);
    cJSON_Delete(output);
    if (!rendered) {
        return AEGIS_ERR_OOM;
    }
    status = aegis_tool_result_set_stdout(out, rendered);
    cJSON_free(rendered);
    return status;
}

AegisTool aegis_tool_reminder(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_REMINDER,
        .description = "Create a reminder.",
        .schema_json =
            "{\"type\":\"object\",\"required\":[\"message\"],"
            "\"properties\":{\"message\":{\"type\":\"string\",\"minLength\":1},"
            "\"due\":{\"type\":\"string\"}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_MEDIUM,
        .availability = AEGIS_TOOL_READY,
        .execute = execute_reminder
    };
}

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "aegis/tool.h"
#include "aegis/tool_git.h"

static AegisStatus execute_git_log(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    const char *limit_text = args ? aegis_tool_args_get(args, "limit") : NULL;
    const char *arguments[5] = {
        "log", "--oneline", "--decorate", "-n20", NULL
    };
    char option[32];

    if (limit_text && limit_text[0]) {
        char *end;
        long limit;

        errno = 0;
        limit = strtol(limit_text, &end, 10);
        if (errno || *end || limit < 1 || limit > 1000) {
            aegis_tool_result_set_error(out, "invalid git log limit");
            return AEGIS_ERR_INVALID_ARGUMENT;
        }
        snprintf(option, sizeof(option), "-n%ld", limit);
        arguments[3] = option;
    }
    return aegis_tool_run_git(context, arguments, NULL, out);
}

AegisTool aegis_tool_git_log(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_GIT_LOG,
        .description = "Show recent repository history.",
        .schema_json =
            "{\"type\":\"object\",\"properties\":{"
            "\"limit\":{\"type\":\"integer\",\"minimum\":1}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_LOW,
        .availability = AEGIS_TOOL_READY,
        .execute = execute_git_log
    };
}

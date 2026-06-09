#include "aegis/tool.h"
#include "aegis/tool_git.h"
#include "aegis/tool_path.h"

static AegisStatus execute_git_diff(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    const char *path = args ? aegis_tool_args_get(args, "path") : NULL;
    const char *arguments[5] = {"diff", "--no-ext-diff", NULL, NULL, NULL};
    char resolved[AEGIS_CONFIG_PATH_MAX];

    if (path && path[0]) {
        AegisStatus status = aegis_tool_resolve_path(
            context, path, 0, resolved, sizeof(resolved));
        if (status != AEGIS_OK) {
            aegis_tool_result_set_error(out, "invalid git path");
            return status;
        }
        arguments[2] = "--";
        arguments[3] = path;
    }
    return aegis_tool_run_git(context, arguments, NULL, out);
}

AegisTool aegis_tool_git_diff(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_GIT_DIFF,
        .description = "Show repository changes.",
        .schema_json =
            "{\"type\":\"object\",\"properties\":{"
            "\"path\":{\"type\":\"string\"}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_LOW,
        .availability = AEGIS_TOOL_READY,
        .execute = execute_git_diff
    };
}

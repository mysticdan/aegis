#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>

#include "aegis/tool.h"
#include "aegis/tool_git.h"
#include "aegis/tool_path.h"

static int patch_paths_are_safe(
    const AegisToolContext *context,
    const char *patch
)
{
    char *copy = malloc(strlen(patch) + 1U);
    char *save = NULL;
    char *line;
    int valid = 1;

    if (!copy) {
        return 0;
    }
    memcpy(copy, patch, strlen(patch) + 1U);
    line = strtok_r(copy, "\n", &save);
    while (line) {
        if (strncmp(line, "+++ ", 4U) == 0 ||
            strncmp(line, "--- ", 4U) == 0) {
            char *path = line + 4U;
            char *end = strpbrk(path, "\t\r");
            char resolved[AEGIS_CONFIG_PATH_MAX];
            int allow_missing =
                strncmp(line, "+++ ", 4U) == 0;

            if (end) {
                *end = '\0';
            }
            if (strcmp(path, "/dev/null") != 0) {
                if ((strncmp(path, "a/", 2U) == 0 ||
                     strncmp(path, "b/", 2U) == 0)) {
                    path += 2U;
                }
                if (aegis_tool_resolve_path(
                        context,
                        path,
                        allow_missing,
                        resolved,
                        sizeof(resolved)) != AEGIS_OK) {
                    valid = 0;
                    break;
                }
            }
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(copy);
    return valid;
}

static AegisStatus execute_git_apply_patch(
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    const char *patch;
    const char *arguments[] = {
        "apply", "--whitespace=nowarn", "--recount", "-", NULL
    };

    if (!args || !context || !out) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    if (!context->allow_write) {
        aegis_tool_result_set_error(out, "git_apply_patch requires write access");
        return AEGIS_ERR_POLICY_DENIED;
    }
    patch = aegis_tool_args_get(args, "patch");
    if (!patch || !patch[0]) {
        aegis_tool_result_set_error(out, "missing patch");
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    if (!patch_paths_are_safe(context, patch)) {
        aegis_tool_result_set_error(out, "patch contains a blocked path");
        return AEGIS_ERR_PATH_ESCAPE;
    }
    return aegis_tool_run_git(context, arguments, patch, out);
}

AegisTool aegis_tool_git_apply_patch(void)
{
    return (AegisTool) {
        .name = AEGIS_TOOL_GIT_APPLY_PATCH,
        .description = "Apply a patch to the workspace.",
        .schema_json =
            "{\"type\":\"object\",\"required\":[\"patch\"],"
            "\"properties\":{\"patch\":{\"type\":\"string\",\"minLength\":1}},"
            "\"additionalProperties\":false}",
        .risk_level = AEGIS_RISK_MEDIUM,
        .availability = AEGIS_TOOL_READY,
        .execute = execute_git_apply_patch
    };
}

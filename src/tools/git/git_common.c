#include "aegis/tool_git.h"
#include "aegis/tool_process.h"

AegisStatus aegis_tool_run_git(
    const AegisToolContext *context,
    const char *const arguments[],
    const char *stdin_data,
    AegisToolResult *result
)
{
    const char *argv[16];
    size_t index = 0U;

    if (!context || !arguments || !result) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    argv[index++] = "git";
    while (*arguments && index + 1U < sizeof(argv) / sizeof(argv[0])) {
        argv[index++] = *arguments++;
    }
    if (*arguments) {
        aegis_tool_result_set_error(result, "too many git arguments");
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    argv[index] = NULL;
    return aegis_tool_run_process(
        context,
        argv,
        stdin_data,
        context->config->shell_timeout_ms,
        result
    );
}

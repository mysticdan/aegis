#include <stdio.h>
#include <string.h>

#include "aegis/cli_command.h"

static const char *const commands =
    "init run chat resume sessions inspect replay eval tools config "
    "profiles mcp doctor version help completion";

int aegis_cli_cmd_completion(const CliOptions *options)
{
    const char *shell;

    if (options->positional_count != 1U) {
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "usage: aegis completion bash|zsh|fish");
    }
    shell = options->positionals[0];
    if (strcmp(shell, "bash") == 0) {
        printf(
            "_aegis_complete() {\n"
            "  if [ \"$COMP_CWORD\" -eq 1 ]; then\n"
            "    COMPREPLY=( $(compgen -W '%s' -- \"${COMP_WORDS[1]}\") )\n"
            "  fi\n"
            "}\n"
            "complete -F _aegis_complete aegis\n",
            commands
        );
    } else if (strcmp(shell, "zsh") == 0) {
        printf(
            "#compdef aegis\n"
            "_arguments '1:command:(%s)' '*::argument:->args'\n",
            commands
        );
    } else if (strcmp(shell, "fish") == 0) {
        const char *cursor = commands;
        char command[32];

        while (sscanf(cursor, "%31s", command) == 1) {
            printf("complete -c aegis -n '__fish_use_subcommand' -a %s\n",
                   command);
            cursor += strcspn(cursor, " ");
            cursor += strspn(cursor, " ");
        }
    } else {
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "unsupported shell: %s", shell);
    }
    return AEGIS_CLI_EXIT_SUCCESS;
}

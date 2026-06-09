#include <stdio.h>
#include <string.h>

#include "aegis/cli_command.h"

int aegis_cli_cmd_help(const CliOptions *options)
{
    const char *command = options->positional_count
        ? options->positionals[0]
        : NULL;

    if (options->positional_count > 1U) {
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "help accepts at most one command");
    }
    if (!command) {
        puts("Usage:\n  aegis <command> [options]\n\n"
             "Commands:\n"
             "  init        Initialize Aegis workspace\n"
             "  run         Run one task\n"
             "  chat        Start interactive chat\n"
             "  resume      Resume a session\n"
             "  sessions    Manage sessions\n"
             "  inspect     Inspect a session or trace\n"
             "  replay      Replay a trace\n"
             "  eval        Run an evaluation suite\n"
             "  tools       List or test tools\n"
             "  config      Manage configuration\n"
             "  profiles    Manage profiles\n"
             "  mcp         Manage MCP integrations\n"
             "  doctor      Check installation health\n"
             "  completion  Generate shell completion\n"
             "  version     Show version\n"
             "  help        Show help");
        return AEGIS_CLI_EXIT_SUCCESS;
    }
    if (strcmp(command, "run") == 0) {
        puts("Usage: aegis run (--task <text>|--task-file <path>|<task>|-) "
             "[--dry-run] [--json] [--no-input] [--yes]");
    } else if (strcmp(command, "init") == 0) {
        puts("Usage: aegis init [--workspace <path>] [--mode <preset>] "
             "[--profile <id>] [--force]");
    } else if (strcmp(command, "chat") == 0) {
        puts("Usage: aegis chat [--workspace <path>] [--profile <id>]");
    } else if (strcmp(command, "resume") == 0) {
        puts("Usage: aegis resume <session-id> [--max-steps <n>]");
    } else if (strcmp(command, "sessions") == 0) {
        puts("Usage: aegis sessions list|show|delete|clean");
    } else if (strcmp(command, "inspect") == 0) {
        puts("Usage: aegis inspect (--session <id>|--trace <path>) "
             "[--step <n>] [--tools] [--policy]");
    } else if (strcmp(command, "replay") == 0) {
        puts("Usage: aegis replay --trace <path> "
             "[--mode timeline|dry-run|compare|tools-only|policy-only]");
    } else if (strcmp(command, "eval") == 0) {
        puts("Usage: aegis eval --suite <path> "
             "[--workspace-root <path>] [--fail-fast]");
    } else if (strcmp(command, "tools") == 0) {
        puts("Usage: aegis tools list|info|schema|test");
    } else if (strcmp(command, "config") == 0) {
        puts("Usage: aegis config check|show|path|get|set");
    } else if (strcmp(command, "profiles") == 0) {
        puts("Usage: aegis profiles list|show|validate|new");
    } else if (strcmp(command, "mcp") == 0) {
        puts("Usage: aegis mcp list|add|remove|tools|call");
    } else if (strcmp(command, "doctor") == 0) {
        puts("Usage: aegis doctor [--verbose] [--json]");
    } else if (strcmp(command, "completion") == 0) {
        puts("Usage: aegis completion bash|zsh|fish");
    } else if (strcmp(command, "version") == 0) {
        puts("Usage: aegis version [--verbose] [--json]");
    } else if (strcmp(command, "help") == 0) {
        puts("Usage: aegis help [command]");
    } else {
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "unknown help command: %s", command);
    }
    return AEGIS_CLI_EXIT_SUCCESS;
}

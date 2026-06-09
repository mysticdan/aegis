#include <string.h>

#include "aegis/tool_process.h"

int aegis_command_policy_is_blocked(const char *cmd)
{
    static const char *const blocked[] = {
        "rm -rf /", "sudo", " su ", "mkfs", "dd if=", "mount ",
        "umount ", "chmod 777", "chown ", "curl | sh", "wget | sh",
        "shutdown", "reboot", "iptables", "nft "
    };
    size_t index;

    if (!cmd) {
        return 1;
    }
    for (index = 0U; index < sizeof(blocked) / sizeof(blocked[0]); ++index) {
        if (strstr(cmd, blocked[index])) {
            return 1;
        }
    }
    return 0;
}

static int command_has_shell_operators(const char *command)
{
    return strchr(command, ';') != NULL ||
        strchr(command, '\n') != NULL ||
        strchr(command, '\r') != NULL ||
        strchr(command, '`') != NULL ||
        strstr(command, "&&") != NULL ||
        strstr(command, "||") != NULL ||
        strstr(command, "$(") != NULL ||
        strchr(command, '|') != NULL ||
        strchr(command, '>') != NULL ||
        strchr(command, '<') != NULL;
}

static int command_matches(const char *command, const char *allowed)
{
    size_t length = strlen(allowed);

    return strncmp(command, allowed, length) == 0 &&
        (command[length] == '\0' ||
         command[length] == ' ' ||
         command[length] == '\t');
}

int aegis_command_policy_allows(
    const AegisConfig *config,
    const char *command
)
{
    const char *trimmed = command;
    size_t index;

    if (!config || !command || !command[0] ||
        aegis_command_policy_is_blocked(command) ||
        command_has_shell_operators(command)) {
        return 0;
    }
    while (*trimmed == ' ' || *trimmed == '\t') {
        ++trimmed;
    }
    for (index = 0U;
         index < config->shell_blocked_commands.count;
         ++index) {
        if (strstr(
                trimmed,
                config->shell_blocked_commands.items[index]) != NULL) {
            return 0;
        }
    }
    for (index = 0U;
         index < config->shell_allowed_commands.count;
         ++index) {
        if (command_matches(
                trimmed,
                config->shell_allowed_commands.items[index])) {
            return 1;
        }
    }
    return 0;
}

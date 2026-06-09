#include <string.h>

int aegis_command_policy_is_blocked(const char *cmd)
{
    if (!cmd) return 1;
    return strstr(cmd, "rm -rf /") || strstr(cmd, "sudo") || strstr(cmd, "mkfs") || strstr(cmd, "dd if=");
}

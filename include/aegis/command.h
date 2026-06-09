#ifndef AEGIS_COMMAND_H
#define AEGIS_COMMAND_H

typedef enum {
    AEGIS_CMD_UNKNOWN = 0,
    AEGIS_CMD_HELP,
    AEGIS_CMD_VERSION,
    AEGIS_CMD_INIT,
    AEGIS_CMD_RUN,
    AEGIS_CMD_CHAT,
    AEGIS_CMD_RESUME,
    AEGIS_CMD_SESSIONS,
    AEGIS_CMD_REPLAY,
    AEGIS_CMD_INSPECT,
    AEGIS_CMD_EVAL,
    AEGIS_CMD_TOOLS,
    AEGIS_CMD_CONFIG,
    AEGIS_CMD_PROFILES,
    AEGIS_CMD_MCP,
    AEGIS_CMD_DOCTOR,
    AEGIS_CMD_COMPLETION
} AegisCommand;

AegisCommand aegis_command_from_string(const char *str);
const char *aegis_command_name(AegisCommand cmd);

#endif

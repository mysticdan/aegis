#include <string.h>
#include "aegis/command.h"

AegisCommand aegis_command_from_string(const char *str) {
    if (strcmp(str, "run") == 0) {
        return AEGIS_CMD_RUN;
    }
    if (strcmp(str, "replay") == 0) {
        return AEGIS_CMD_REPLAY;
    }
    if (strcmp(str, "inspect") == 0) {
        return AEGIS_CMD_INSPECT;
    }
    if (strcmp(str, "eval") == 0) {
        return AEGIS_CMD_EVAL;
    }
    if (strcmp(str, "tools") == 0) {
        return AEGIS_CMD_TOOLS;
    }
    if (strcmp(str, "config") == 0) {
        return AEGIS_CMD_CONFIG;
    }
    return AEGIS_CMD_UNKNOWN;
}

const char *aegis_command_name(AegisCommand cmd) {
    switch (cmd) {
        case AEGIS_CMD_RUN: return "run";
        case AEGIS_CMD_REPLAY: return "replay";
        case AEGIS_CMD_INSPECT: return "inspect";
        case AEGIS_CMD_EVAL: return "eval";
        case AEGIS_CMD_TOOLS: return "tools";
        case AEGIS_CMD_CONFIG: return "config";
        default: return "unknown";
    }
}
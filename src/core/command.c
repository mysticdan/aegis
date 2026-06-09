#include <string.h>

#include "aegis/command.h"

AegisCommand aegis_command_from_string(const char *str)
{
    if (!str) {
        return AEGIS_CMD_UNKNOWN;
    }
    if (strcmp(str, "help") == 0) {
        return AEGIS_CMD_HELP;
    }
    if (strcmp(str, "version") == 0) {
        return AEGIS_CMD_VERSION;
    }
    if (strcmp(str, "init") == 0) {
        return AEGIS_CMD_INIT;
    }
    if (strcmp(str, "run") == 0) {
        return AEGIS_CMD_RUN;
    }
    if (strcmp(str, "chat") == 0) {
        return AEGIS_CMD_CHAT;
    }
    if (strcmp(str, "resume") == 0) {
        return AEGIS_CMD_RESUME;
    }
    if (strcmp(str, "sessions") == 0) {
        return AEGIS_CMD_SESSIONS;
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
    if (strcmp(str, "profiles") == 0) {
        return AEGIS_CMD_PROFILES;
    }
    if (strcmp(str, "mcp") == 0) {
        return AEGIS_CMD_MCP;
    }
    if (strcmp(str, "doctor") == 0) {
        return AEGIS_CMD_DOCTOR;
    }
    if (strcmp(str, "completion") == 0) {
        return AEGIS_CMD_COMPLETION;
    }
    return AEGIS_CMD_UNKNOWN;
}

const char *aegis_command_name(AegisCommand cmd)
{
    switch (cmd) {
        case AEGIS_CMD_HELP: return "help";
        case AEGIS_CMD_VERSION: return "version";
        case AEGIS_CMD_INIT: return "init";
        case AEGIS_CMD_RUN: return "run";
        case AEGIS_CMD_CHAT: return "chat";
        case AEGIS_CMD_RESUME: return "resume";
        case AEGIS_CMD_SESSIONS: return "sessions";
        case AEGIS_CMD_REPLAY: return "replay";
        case AEGIS_CMD_INSPECT: return "inspect";
        case AEGIS_CMD_EVAL: return "eval";
        case AEGIS_CMD_TOOLS: return "tools";
        case AEGIS_CMD_CONFIG: return "config";
        case AEGIS_CMD_PROFILES: return "profiles";
        case AEGIS_CMD_MCP: return "mcp";
        case AEGIS_CMD_DOCTOR: return "doctor";
        case AEGIS_CMD_COMPLETION: return "completion";
        default: return "unknown";
    }
}

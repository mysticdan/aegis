#include <string.h>
#include "aegis/error.h"

const char *aegis_status_string(AegisStatus status) {
    switch (status) {
        case AEGIS_OK: return "OK";
        case AEGIS_ERR_INVALID_ARGUMENT: return "Invalid argument";
        case AEGIS_ERR_OOM: return "Out of memory";
        case AEGIS_ERR_NOT_IMPLEMENTED: return "Not implemented";
        case AEGIS_ERR_NOT_FOUND: return "Not found";
        case AEGIS_ERR_IO: return "I/O error";
        case AEGIS_ERR_POLICY_DENIED: return "Policy denied";
        case AEGIS_ERR_PATH_ESCAPE: return "Path escape detected";
        case AEGIS_ERR_PARSE: return "Parse error";
        case AEGIS_ERR_RUNTIME: return "Runtime error";
        case AEGIS_ERR_PROVIDER: return "Provider error";
        case AEGIS_ERR_TOOL: return "Tool error";
        case AEGIS_ERR_APPROVAL_REJECTED: return "Approval rejected";
        case AEGIS_ERR_MAX_STEPS: return "Maximum steps reached";
        case AEGIS_ERR_STATE: return "State error";
        case AEGIS_ERR_INTERRUPTED: return "Interrupted";
        default: return "Unknown error";
    }
}

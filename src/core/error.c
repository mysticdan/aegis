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
        default: return "Unknown error";
    }
}
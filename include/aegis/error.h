#ifndef AEGIS_ERROR_H
#define AEGIS_ERROR_H

typedef enum {
    AEGIS_OK = 0,
    AEGIS_ERR_INVALID_ARGUMENT = 1,
    AEGIS_ERR_OOM = 2,
    AEGIS_ERR_NOT_IMPLEMENTED = 3,
    AEGIS_ERR_NOT_FOUND = 4,
    AEGIS_ERR_IO = 5,
    AEGIS_ERR_POLICY_DENIED = 6,
    AEGIS_ERR_PATH_ESCAPE = 7,
    AEGIS_ERR_PARSE = 8,
    AEGIS_ERR_RUNTIME = 9
} AegisStatus;

const char *aegis_status_string(AegisStatus status);

#endif
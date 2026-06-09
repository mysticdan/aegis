#ifndef AEGIS_CLI_INIT_H
#define AEGIS_CLI_INIT_H

#include <stddef.h>

#include "aegis/config.h"

#define AEGIS_CLI_INIT_MAX_CHANGES 32

typedef struct {
    const char *workspace;
    const char *mode;
    const char *profile_id;
    int force;
} AegisCliInitRequest;

typedef struct {
    char workspace[AEGIS_CONFIG_PATH_MAX];
    char root[AEGIS_CONFIG_PATH_MAX];
    char mode[AEGIS_CONFIG_NAME_MAX];
    char profile[AEGIS_CONFIG_NAME_MAX];
    int already_initialized;
    size_t created_count;
    char created[AEGIS_CLI_INIT_MAX_CHANGES][AEGIS_CONFIG_PATH_MAX];
    size_t updated_count;
    char updated[AEGIS_CLI_INIT_MAX_CHANGES][AEGIS_CONFIG_PATH_MAX];
} AegisCliInitResult;

const char *aegis_cli_resource_directory(void);
int aegis_cli_cmd_init(const CliOptions *options);

int aegis_cli_init_execute(
    const AegisCliInitRequest *request,
    AegisCliInitResult *result,
    char *error,
    size_t error_size
);

#endif

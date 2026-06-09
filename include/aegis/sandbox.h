#ifndef AEGIS_SANDBOX_H
#define AEGIS_SANDBOX_H

#include "aegis/error.h"

typedef struct {
    const char *workspace_root;
    long timeout_ms;
    unsigned long max_output_bytes;
} AegisSandboxConfig;

#endif

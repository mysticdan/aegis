#ifndef AEGIS_TRACE_H
#define AEGIS_TRACE_H

#include <stdio.h>

#include "aegis/error.h"

typedef struct {
    char path[4096];
    FILE *file;
    unsigned long sequence;
    int redact_secrets;
    char secret[512];
} AegisTrace;

AegisStatus aegis_trace_open(AegisTrace *trace, const char *path);
void aegis_trace_set_redaction(
    AegisTrace *trace,
    int enabled,
    const char *secret
);
AegisStatus aegis_trace_event(
    AegisTrace *trace,
    const char *session_id,
    int step,
    const char *type,
    const char *payload_json
);
void aegis_trace_close(AegisTrace *trace);

#endif

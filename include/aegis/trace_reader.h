#ifndef AEGIS_TRACE_READER_H
#define AEGIS_TRACE_READER_H

#include <stddef.h>

#include <cjson/cJSON.h>

#include "aegis/error.h"

typedef struct {
    cJSON **events;
    size_t count;
} AegisTraceDocument;

void aegis_trace_document_clear(AegisTraceDocument *document);
AegisStatus aegis_trace_document_load(
    const char *path,
    AegisTraceDocument *document
);

#endif

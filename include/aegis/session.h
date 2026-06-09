#ifndef AEGIS_SESSION_H
#define AEGIS_SESSION_H

#include <stddef.h>

#include "aegis/error.h"

#define AEGIS_SESSION_ID_MAX 96

int aegis_session_id_is_valid(const char *session_id);
AegisStatus aegis_session_id_make(
    const char *prefix,
    char *out,
    size_t out_size
);

#endif

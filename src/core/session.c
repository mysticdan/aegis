#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "aegis/session.h"

AegisStatus aegis_session_id_make(
    const char *prefix,
    char *out,
    size_t out_size
)
{
    struct timespec now;
    int written;

    if (!out || out_size == 0U || clock_gettime(CLOCK_REALTIME, &now) != 0) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    written = snprintf(
        out,
        (size_t)out_size,
        "%s_%lld_%ld_%ld",
        prefix && prefix[0] ? prefix : "s",
        (long long)now.tv_sec,
        now.tv_nsec / 1000000L,
        (long)getpid()
    );
    return written >= 0 && (size_t)written < out_size
        ? AEGIS_OK
        : AEGIS_ERR_RUNTIME;
}

int aegis_session_id_is_valid(const char *session_id)
{
    const unsigned char *cursor =
        (const unsigned char *)session_id;

    if (!session_id || !session_id[0] ||
        strlen(session_id) >= AEGIS_SESSION_ID_MAX) {
        return 0;
    }
    while (*cursor) {
        if (!isalnum(*cursor) && *cursor != '_' && *cursor != '-') {
            return 0;
        }
        ++cursor;
    }
    return 1;
}

#ifndef AEGIS_STATE_H
#define AEGIS_STATE_H

#include <stddef.h>

#include <sqlite3.h>

#include "aegis/error.h"
#include "aegis/session.h"

typedef struct AegisState {
    sqlite3 *database;
    char path[4096];
} AegisState;

typedef struct {
    char id[AEGIS_SESSION_ID_MAX];
    char status[32];
    char profile[64];
    char workspace[4096];
    char trace_path[4096];
    char *task;
    char *final_text;
    int steps;
    long long created_ms;
    long long updated_ms;
} AegisSessionRecord;

void aegis_session_record_clear(AegisSessionRecord *record);
AegisStatus aegis_state_open(AegisState *state, const char *path);
void aegis_state_close(AegisState *state);
AegisStatus aegis_state_upsert_session(
    AegisState *state,
    const AegisSessionRecord *record
);
AegisStatus aegis_state_get_session(
    AegisState *state,
    const char *session_id,
    AegisSessionRecord *record
);
AegisStatus aegis_state_add_event(
    AegisState *state,
    const char *session_id,
    int step,
    const char *kind,
    const char *payload_json
);
AegisStatus aegis_state_list_sessions(
    AegisState *state,
    AegisSessionRecord **records,
    size_t *count
);
AegisStatus aegis_state_delete_session(
    AegisState *state,
    const char *session_id
);
AegisStatus aegis_state_session_events_json(
    AegisState *state,
    const char *session_id,
    char **events_json
);
AegisStatus aegis_state_add_reminder(
    AegisState *state,
    const char *session_id,
    const char *message,
    const char *due
);

#endif

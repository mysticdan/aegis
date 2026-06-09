#include <stdlib.h>
#include <string.h>

#include <cjson/cJSON.h>

#include "aegis/state.h"
#include "aegis/str.h"

static const char *const schema_sql =
    "PRAGMA journal_mode=WAL;"
    "PRAGMA foreign_keys=ON;"
    "CREATE TABLE IF NOT EXISTS sessions("
    "id TEXT PRIMARY KEY,status TEXT NOT NULL,profile TEXT NOT NULL,"
    "workspace TEXT NOT NULL,trace_path TEXT NOT NULL,task TEXT NOT NULL,"
    "final_text TEXT,steps INTEGER NOT NULL DEFAULT 0,"
    "created_ms INTEGER NOT NULL,updated_ms INTEGER NOT NULL);"
    "CREATE TABLE IF NOT EXISTS events("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "session_id TEXT NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,"
    "step INTEGER NOT NULL,kind TEXT NOT NULL,payload_json TEXT NOT NULL);"
    "CREATE INDEX IF NOT EXISTS events_session_step "
    "ON events(session_id,step,id);"
    "CREATE TABLE IF NOT EXISTS reminders("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "session_id TEXT NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,"
    "message TEXT NOT NULL,due TEXT NOT NULL DEFAULT '',"
    "status TEXT NOT NULL DEFAULT 'scheduled',created_ms INTEGER NOT NULL);";

void aegis_session_record_clear(AegisSessionRecord *record)
{
    if (!record) {
        return;
    }
    free(record->task);
    free(record->final_text);
    memset(record, 0, sizeof(*record));
}

AegisStatus aegis_state_open(AegisState *state, const char *path)
{
    char *error = NULL;

    if (!state || !path || !path[0] || strlen(path) >= sizeof(state->path)) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    memset(state, 0, sizeof(*state));
    if (sqlite3_open_v2(
            path,
            &state->database,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
            NULL) != SQLITE_OK) {
        aegis_state_close(state);
        return AEGIS_ERR_IO;
    }
    memcpy(state->path, path, strlen(path) + 1U);
    if (sqlite3_exec(state->database, schema_sql, NULL, NULL, &error) !=
        SQLITE_OK) {
        sqlite3_free(error);
        aegis_state_close(state);
        return AEGIS_ERR_IO;
    }
    return AEGIS_OK;
}

void aegis_state_close(AegisState *state)
{
    if (!state) {
        return;
    }
    if (state->database) {
        sqlite3_close(state->database);
    }
    memset(state, 0, sizeof(*state));
}

AegisStatus aegis_state_upsert_session(
    AegisState *state,
    const AegisSessionRecord *record
)
{
    static const char sql[] =
        "INSERT INTO sessions(id,status,profile,workspace,trace_path,task,"
        "final_text,steps,created_ms,updated_ms) VALUES(?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET status=excluded.status,"
        "profile=excluded.profile,workspace=excluded.workspace,"
        "trace_path=excluded.trace_path,task=excluded.task,"
        "final_text=excluded.final_text,steps=excluded.steps,"
        "updated_ms=excluded.updated_ms";
    sqlite3_stmt *statement;
    int result;

    if (!state || !state->database || !record || !record->id[0] ||
        !record->task) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    if (sqlite3_prepare_v2(state->database, sql, -1, &statement, NULL) !=
        SQLITE_OK) {
        return AEGIS_ERR_IO;
    }
    sqlite3_bind_text(statement, 1, record->id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, record->status, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, record->profile, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, record->workspace, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 5, record->trace_path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 6, record->task, -1, SQLITE_TRANSIENT);
    if (record->final_text) {
        sqlite3_bind_text(
            statement, 7, record->final_text, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(statement, 7);
    }
    sqlite3_bind_int(statement, 8, record->steps);
    sqlite3_bind_int64(statement, 9, record->created_ms);
    sqlite3_bind_int64(statement, 10, record->updated_ms);
    result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    return result == SQLITE_DONE ? AEGIS_OK : AEGIS_ERR_IO;
}

AegisStatus aegis_state_get_session(
    AegisState *state,
    const char *session_id,
    AegisSessionRecord *record
)
{
    static const char sql[] =
        "SELECT id,status,profile,workspace,trace_path,task,final_text,steps,"
        "created_ms,updated_ms FROM sessions WHERE id=?";
    sqlite3_stmt *statement;
    int result;
    int has_final_text;

    if (!state || !state->database || !session_id || !record) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    memset(record, 0, sizeof(*record));
    if (sqlite3_prepare_v2(state->database, sql, -1, &statement, NULL) !=
        SQLITE_OK) {
        return AEGIS_ERR_IO;
    }
    sqlite3_bind_text(statement, 1, session_id, -1, SQLITE_TRANSIENT);
    result = sqlite3_step(statement);
    if (result != SQLITE_ROW) {
        sqlite3_finalize(statement);
        return result == SQLITE_DONE ? AEGIS_ERR_NOT_FOUND : AEGIS_ERR_IO;
    }
#define COPY_COLUMN(field, column) do { \
    const unsigned char *text = sqlite3_column_text(statement, (column)); \
    if (!text || strlen((const char *)text) >= sizeof(record->field)) { \
        sqlite3_finalize(statement); \
        return AEGIS_ERR_PARSE; \
    } \
    memcpy(record->field, text, strlen((const char *)text) + 1U); \
} while (0)
    COPY_COLUMN(id, 0);
    COPY_COLUMN(status, 1);
    COPY_COLUMN(profile, 2);
    COPY_COLUMN(workspace, 3);
    COPY_COLUMN(trace_path, 4);
#undef COPY_COLUMN
    record->task = aegis_strdup(
        (const char *)sqlite3_column_text(statement, 5));
    has_final_text = sqlite3_column_type(statement, 6) != SQLITE_NULL;
    if (has_final_text) {
        record->final_text = aegis_strdup(
            (const char *)sqlite3_column_text(statement, 6));
    }
    record->steps = sqlite3_column_int(statement, 7);
    record->created_ms = sqlite3_column_int64(statement, 8);
    record->updated_ms = sqlite3_column_int64(statement, 9);
    sqlite3_finalize(statement);
    if (!record->task ||
        (has_final_text && !record->final_text)) {
        aegis_session_record_clear(record);
        return AEGIS_ERR_OOM;
    }
    return AEGIS_OK;
}

AegisStatus aegis_state_add_event(
    AegisState *state,
    const char *session_id,
    int step,
    const char *kind,
    const char *payload_json
)
{
    static const char sql[] =
        "INSERT INTO events(session_id,step,kind,payload_json) VALUES(?,?,?,?)";
    sqlite3_stmt *statement;
    int result;

    if (!state || !state->database || !session_id || !kind || !payload_json) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    if (sqlite3_prepare_v2(state->database, sql, -1, &statement, NULL) !=
        SQLITE_OK) {
        return AEGIS_ERR_IO;
    }
    sqlite3_bind_text(statement, 1, session_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 2, step);
    sqlite3_bind_text(statement, 3, kind, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, payload_json, -1, SQLITE_TRANSIENT);
    result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    return result == SQLITE_DONE ? AEGIS_OK : AEGIS_ERR_IO;
}

AegisStatus aegis_state_list_sessions(
    AegisState *state,
    AegisSessionRecord **records,
    size_t *count
)
{
    static const char sql[] =
        "SELECT id,status,profile,workspace,trace_path,task,final_text,steps,"
        "created_ms,updated_ms FROM sessions ORDER BY updated_ms DESC";
    sqlite3_stmt *statement;
    AegisSessionRecord *items = NULL;
    size_t used = 0U;
    size_t capacity = 0U;
    int result;

    if (!state || !state->database || !records || !count) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    *records = NULL;
    *count = 0U;
    if (sqlite3_prepare_v2(state->database, sql, -1, &statement, NULL) !=
        SQLITE_OK) {
        return AEGIS_ERR_IO;
    }
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        AegisSessionRecord *record;
        const unsigned char *text;

        if (used == capacity) {
            size_t next = capacity ? capacity * 2U : 16U;
            AegisSessionRecord *resized =
                realloc(items, next * sizeof(*items));
            if (!resized) {
                size_t index;
                for (index = 0U; index < used; ++index) {
                    aegis_session_record_clear(&items[index]);
                }
                free(items);
                sqlite3_finalize(statement);
                return AEGIS_ERR_OOM;
            }
            items = resized;
            capacity = next;
        }
        record = &items[used];
        memset(record, 0, sizeof(*record));
#define COPY_LIST_COLUMN(field, column) do { \
    text = sqlite3_column_text(statement, (column)); \
    if (!text || strlen((const char *)text) >= sizeof(record->field)) { \
        result = SQLITE_CORRUPT; \
        break; \
    } \
    memcpy(record->field, text, strlen((const char *)text) + 1U); \
} while (0)
        COPY_LIST_COLUMN(id, 0);
        COPY_LIST_COLUMN(status, 1);
        COPY_LIST_COLUMN(profile, 2);
        COPY_LIST_COLUMN(workspace, 3);
        COPY_LIST_COLUMN(trace_path, 4);
#undef COPY_LIST_COLUMN
        if (result == SQLITE_CORRUPT) {
            break;
        }
        record->task = aegis_strdup(
            (const char *)sqlite3_column_text(statement, 5));
        if (sqlite3_column_type(statement, 6) != SQLITE_NULL) {
            record->final_text = aegis_strdup(
                (const char *)sqlite3_column_text(statement, 6));
        }
        record->steps = sqlite3_column_int(statement, 7);
        record->created_ms = sqlite3_column_int64(statement, 8);
        record->updated_ms = sqlite3_column_int64(statement, 9);
        if (!record->task) {
            result = SQLITE_NOMEM;
            break;
        }
        ++used;
    }
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) {
        size_t index;
        for (index = 0U; index < used; ++index) {
            aegis_session_record_clear(&items[index]);
        }
        free(items);
        return result == SQLITE_NOMEM ? AEGIS_ERR_OOM : AEGIS_ERR_STATE;
    }
    *records = items;
    *count = used;
    return AEGIS_OK;
}

AegisStatus aegis_state_delete_session(
    AegisState *state,
    const char *session_id
)
{
    sqlite3_stmt *statement;
    int result;

    if (!state || !state->database || !session_id) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    if (sqlite3_prepare_v2(
            state->database,
            "DELETE FROM sessions WHERE id=?",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        return AEGIS_ERR_IO;
    }
    sqlite3_bind_text(statement, 1, session_id, -1, SQLITE_TRANSIENT);
    result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    return result == SQLITE_DONE ? AEGIS_OK : AEGIS_ERR_IO;
}

AegisStatus aegis_state_session_events_json(
    AegisState *state,
    const char *session_id,
    char **events_json
)
{
    sqlite3_stmt *statement;
    cJSON *events;
    char *rendered;
    int result;

    if (!state || !state->database || !session_id || !events_json) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    *events_json = NULL;
    if (sqlite3_prepare_v2(
            state->database,
            "SELECT step,kind,payload_json FROM events "
            "WHERE session_id=? ORDER BY id",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        return AEGIS_ERR_IO;
    }
    sqlite3_bind_text(statement, 1, session_id, -1, SQLITE_TRANSIENT);
    events = cJSON_CreateArray();
    if (!events) {
        sqlite3_finalize(statement);
        return AEGIS_ERR_OOM;
    }
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        cJSON *entry = cJSON_CreateObject();
        const char *kind =
            (const char *)sqlite3_column_text(statement, 1);
        const char *payload_text =
            (const char *)sqlite3_column_text(statement, 2);
        cJSON *payload = payload_text ? cJSON_Parse(payload_text) : NULL;

        if (!entry) {
            cJSON_Delete(events);
            sqlite3_finalize(statement);
            return AEGIS_ERR_OOM;
        }
        cJSON_AddNumberToObject(
            entry, "step", sqlite3_column_int(statement, 0));
        cJSON_AddStringToObject(entry, "kind", kind ? kind : "");
        cJSON_AddItemToObject(
            entry,
            "payload",
            payload ? payload : cJSON_CreateString(payload_text ? payload_text : "")
        );
        cJSON_AddItemToArray(events, entry);
    }
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) {
        cJSON_Delete(events);
        return AEGIS_ERR_IO;
    }
    rendered = cJSON_PrintUnformatted(events);
    cJSON_Delete(events);
    if (!rendered) {
        return AEGIS_ERR_OOM;
    }
    *events_json = aegis_strdup(rendered);
    cJSON_free(rendered);
    return *events_json ? AEGIS_OK : AEGIS_ERR_OOM;
}

AegisStatus aegis_state_add_reminder(
    AegisState *state,
    const char *session_id,
    const char *message,
    const char *due
)
{
    static const char sql[] =
        "INSERT INTO reminders(session_id,message,due,created_ms) "
        "VALUES(?,?,?,CAST(strftime('%s','now') AS INTEGER)*1000)";
    sqlite3_stmt *statement;
    int result;

    if (!state || !state->database || !session_id || !session_id[0] ||
        !message || !message[0]) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    if (sqlite3_prepare_v2(
            state->database,
            sql,
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        return AEGIS_ERR_IO;
    }
    sqlite3_bind_text(statement, 1, session_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, message, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(
        statement,
        3,
        due ? due : "",
        -1,
        SQLITE_TRANSIENT
    );
    result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    return result == SQLITE_DONE ? AEGIS_OK : AEGIS_ERR_IO;
}

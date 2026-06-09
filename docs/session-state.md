# Aegis Session and State

## 1. Purpose

A session represents one logical Aegis execution or conversation. SQLite
state makes sessions queryable and resumable. JSONL trace makes execution
auditable and replayable.

They are related but intentionally different:

```text
State = current/queryable records
Trace = append-only event history
```

## 2. Session Identity

Session IDs are limited to `AEGIS_SESSION_ID_MAX` bytes and validated before
runtime use.

The CLI generates prefixes by command:

```text
run   -> cli_...
chat  -> chat_...
eval  -> eval_...
```

An adapter may provide its own valid ID when it needs stable external
conversation mapping.

## 3. Session Record

The public representation is:

```c
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
```

`task` and `final_text` are owned strings after a read/list operation. Release
them with `aegis_session_record_clear()`.

## 4. SQLite Schema

The state backend opens SQLite in read/write/create mode, enables foreign
keys, enables WAL journal mode, and creates three tables.

### 4.1 `sessions`

```sql
CREATE TABLE sessions (
    id          TEXT PRIMARY KEY,
    status      TEXT NOT NULL,
    profile     TEXT NOT NULL,
    workspace   TEXT NOT NULL,
    trace_path  TEXT NOT NULL,
    task        TEXT NOT NULL,
    final_text  TEXT,
    steps       INTEGER NOT NULL DEFAULT 0,
    created_ms  INTEGER NOT NULL,
    updated_ms  INTEGER NOT NULL
);
```

### 4.2 `events`

```sql
CREATE TABLE events (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id   TEXT NOT NULL
                 REFERENCES sessions(id) ON DELETE CASCADE,
    step         INTEGER NOT NULL,
    kind         TEXT NOT NULL,
    payload_json TEXT NOT NULL
);

CREATE INDEX events_session_step
ON events(session_id, step, id);
```

Events store the same logical kinds written to trace, but use a generic JSON
payload rather than separate normalized message/tool tables.

### 4.3 `reminders`

```sql
CREATE TABLE reminders (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id  TEXT NOT NULL
                REFERENCES sessions(id) ON DELETE CASCADE,
    message     TEXT NOT NULL,
    due         TEXT NOT NULL DEFAULT '',
    status      TEXT NOT NULL DEFAULT 'scheduled',
    created_ms  INTEGER NOT NULL
);
```

The reminder tool stores records. The current project does not contain a
scheduler that later delivers them.

## 5. State Paths

Built-in resource configs use paths such as:

```text
state/aegis.db
state/aegis-safe.db
state/aegis-dev.db
state/aegis-dangerous.db
```

After `aegis init`, these are normalized below `.aegis/`, for example:

```text
.aegis/state/aegis.db
```

Relative state paths are resolved against the selected workspace, not the
process launch directory.

`--state-dir <path>` changes the in-memory path to:

```text
<path>/state.db
```

## 6. Lifecycle

### 6.1 Start

Before the model loop, runtime:

1. Opens state if enabled.
2. Upserts a session with status `running`.
3. Stores config active profile, workspace, trace path, task, and timestamps.
4. Records `session_start` in trace.

### 6.2 During execution

Agent events are inserted into `events`:

```text
model_request
model_response
model_error
tool_call
approval
tool_result
final
```

### 6.3 End

Runtime updates:

- `final_text`
- `steps`
- `updated_ms`
- final status

It also appends `session_end` to trace.

## 7. Status Values

Current runtime statuses:

| Status | Meaning |
|---|---|
| `running` | Runtime has started and not yet stored a terminal status |
| `success` | Agent returned a valid final action |
| `waiting_approval` | Required approval was rejected or unavailable |
| `max_steps` | Step or tool-call bound was reached |
| `cancelled` | Cancellation propagated to runtime |
| `failed` | Other terminal error |

The name `waiting_approval` currently describes the stored outcome after
approval rejection. There is no separate daemon waiting asynchronously for a
later approval.

## 8. Upsert Semantics

Session writes use `INSERT ... ON CONFLICT(id) DO UPDATE`.

On update:

- Status, profile, workspace, trace, task, result, steps, and update time are
  replaced.
- Original `created_ms` remains unchanged.

For resume with `initial_step > 0`, runtime loads the prior record and
preserves its original task and creation time.

## 9. Session Commands

```bash
aegis sessions list
aegis sessions show <id>
aegis sessions delete <id>
aegis sessions clean --older-than 30d
```

`list` orders by `updated_ms DESC`.

`show` returns metadata and all stored events.

`delete` removes the session. Foreign-key cascade removes events and
reminders. Unless `--keep-trace` is used, the CLI also removes a trace that
resolves inside the current workspace.

`clean` accepts positive minutes, hours, or days:

```text
30m
24h
90d
```

## 10. Resume Behavior

```bash
aegis resume <session-id>
```

The current resume algorithm:

1. Loads the session from the active state database.
2. Restores the stored profile unless explicitly overridden.
3. Serializes all stored generic events to JSON.
4. Constructs a synthetic user task:

```text
Resume the previous task.
Original task: ...
Previous execution events: [...]
Continue from the latest state.
```

5. Reuses the trace path.
6. Sets `initial_step` to the stored step count.
7. Runs a new bounded model loop.

This is safe from automatic side-effect replay, but it is less precise than a
normalized message history. The provider receives a summary-like synthetic
message rather than exact original role/tool-call structures.

## 11. Chat Persistence

Within one process, chat maintains a textual history:

```text
User: ...
Assistant: ...
```

Each turn calls runtime with the same session ID and increasing initial step.

When `chat --session <id>` loads a previous process, it seeds history from
the stored original task and final result. It does not currently reconstruct
all prior event messages.

`/compact` replaces current in-memory history with a marker:

```text
Previous conversation was compacted by the user.
```

## 12. Consistency with Trace

State and trace writes are not part of one database transaction:

- State uses SQLite.
- Trace appends and flushes a JSON line.

A crash can therefore leave one side slightly ahead of the other. The shared
session ID, step, kind, and payload make manual reconciliation possible.

Future crash recovery should detect stale `running` sessions and reconcile
the last valid trace event.

## 13. Concurrency

SQLite WAL mode improves concurrent reader/writer behavior, but the current
state wrapper:

- Opens one connection per command/runtime instance.
- Does not configure a busy timeout.
- Does not expose explicit transaction APIs.
- Does not coordinate two writers using the same session ID.

Adapters serving concurrent requests should serialize access per session or
add stronger transaction and busy-retry behavior.

## 14. Privacy

State may contain:

- User tasks.
- Model responses.
- Tool arguments.
- Tool output.
- Workspace paths.
- External content.
- Reminder text.

Unlike trace writing, SQLite event insertion does not run a separate
state-specific redaction pass. The agent generally inserts the same payload
that trace receives, but trace redaction occurs inside the trace writer after
that point. A secret may therefore be redacted in trace while remaining in
state.

Protect the state database with filesystem permissions and retention policy.
Do not assume configured trace redaction sanitizes SQLite.

## 15. State API

Public operations:

```c
aegis_state_open()
aegis_state_close()
aegis_state_upsert_session()
aegis_state_get_session()
aegis_state_add_event()
aegis_state_list_sessions()
aegis_state_delete_session()
aegis_state_session_events_json()
aegis_state_add_reminder()
```

Callers must initialize `AegisState` and session output records to zero before
use, as the CLI and runtime do.

## 16. Future State Work

High-value improvements:

- Normalized messages, tool calls, results, and approvals.
- Schema migration version table.
- Busy timeout and transaction boundaries.
- Config/provider/model snapshot per session.
- Crash recovery.
- State redaction or optional encryption.
- Reminder query and scheduler APIs.
- Session ownership fields for multi-user adapters.


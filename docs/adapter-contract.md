# Aegis Adapter Contract

## 1. Purpose

An adapter converts an external channel into the provider-neutral Aegis
runtime contract:

```text
External input
  -> AegisMessage
  -> aegis_runtime_handle_message()
  -> AegisResponse
  -> External output
```

The CLI is the only complete adapter in the current tree. This document
defines how future adapters should integrate without copying the agent loop,
provider logic, policy, or tools.

## 2. Adapter Responsibilities

An adapter is responsible for:

- Authenticating the external caller where applicable.
- Limiting and validating raw input.
- Mapping channel identity to `channel`, `user_id`, and `session_id`.
- Resolving or selecting a workspace and profile through trusted deployment
  policy.
- Loading config before runtime construction.
- Providing optional user-interaction, message-send, and cancellation
  callbacks.
- Calling the runtime.
- Rendering the response for the channel.
- Mapping `AegisStatus` to channel-specific error behavior.

An adapter must not:

- Parse model output itself.
- Execute model-selected tools directly.
- Change a denied policy decision to allow.
- Bypass workspace path resolution.
- Write fake runtime trace events.
- Put provider credentials into user-visible messages.

## 3. Message Contract

The public structure is declared in `include/aegis/message.h`:

```c
typedef int (*AegisAskUserFn)(
    void *userdata,
    const char *question,
    char **answer
);

typedef int (*AegisSendMessageFn)(
    void *userdata,
    const char *message
);

typedef int (*AegisCancelFn)(void *userdata);

typedef struct {
    const char *channel;
    const char *user_id;
    const char *session_id;
    const char *text;
    const char *workspace;
    const char *profile;
    const char *trace_path;
    int auto_approve;
    int no_input;
    int initial_step;
    AegisAskUserFn ask_user;
    AegisSendMessageFn send_message;
    AegisCancelFn is_cancelled;
    void *adapter_userdata;
} AegisMessage;
```

### 3.1 Required fields

The current validator requires non-empty:

- `channel`
- `user_id`
- `session_id`
- `text`

The runtime separately validates the session ID.

### 3.2 Optional and operational fields

| Field | Meaning |
|---|---|
| `workspace` | Runtime workspace. When absent, config workspace root is used |
| `profile` | Adapter-visible profile identity |
| `trace_path` | Optional trace path relative to the workspace |
| `auto_approve` | Allow automatic approval for non-critical tools |
| `no_input` | Prevent interactive approval |
| `initial_step` | Continue step numbering for resume/chat |
| `ask_user` | Adapter callback used by `ask_user` |
| `send_message` | Adapter callback used by `send_message` |
| `is_cancelled` | Cooperative cancellation callback |
| `adapter_userdata` | Opaque pointer passed to callbacks |

The runtime uses the already loaded `AegisConfig` as the active profile and
policy source. It does not load a profile from `message.profile`. Adapters
should therefore select the profile in config first, then set
`message.profile` to the matching ID for consistency.

## 4. Message Ownership

All pointers in `AegisMessage` are borrowed. They must remain valid until
`aegis_runtime_handle_message()` returns.

The runtime and context builder deep-copy content they need to own. An
adapter may release its input buffers after the call returns.

Do not pass pointers to stack buffers that expire while an asynchronous
callback is still active. The current runtime is synchronous, but adapter
design should not rely on undocumented callback lifetime tricks.

## 5. Session IDs

Use:

```c
AegisStatus aegis_session_id_make(
    const char *prefix,
    char *out,
    size_t out_size
);
```

or validate an externally mapped value with:

```c
int aegis_session_id_is_valid(const char *session_id);
```

An adapter should map external conversation identity deterministically when
resume semantics matter. Public network identifiers should be hashed or
encoded rather than copied blindly into local path-related data.

## 6. Response Contract

The public response is:

```c
typedef struct {
    char *text;
    char *error_message;
    int exit_code;
    char session_id[96];
    char trace_path[4096];
    char status[32];
    int steps;
} AegisResponse;
```

Lifecycle:

```c
AegisResponse response;

aegis_response_init(&response);
status = aegis_runtime_handle_message(runtime, &message, &response);
/* render response */
aegis_response_free(&response);
```

`text` and `error_message` are owned by the response. The fixed-size fields
are stored inline.

Current core behavior fills:

- `text` after a successful `final` action.
- `error_message` on provider failure (HTTP status, auth error, rate limit,
  model not found, etc.).
- `session_id`.
- `trace_path`.
- `status`.
- `steps`.

The CLI maps the returned `AegisStatus` itself. The `exit_code` member is
currently reserved and should not be treated as the authoritative status.

## 7. Runtime Construction

An adapter may construct from a path:

```c
AegisRuntime *runtime = aegis_runtime_new(config_path);
```

or from a config it has already selected and validated:

```c
AegisRuntime *runtime =
    aegis_runtime_new_with_config(&selected_config);
```

The second form is appropriate when the adapter supports profile, provider,
model, or per-request limit selection.

The runtime owns a shallow value copy of `AegisConfig`. The config structure
contains fixed arrays rather than heap-owned nested pointers, so the caller
may release its stack variable after construction.

## 8. Callback Contracts

### 8.1 Ask user

```c
int ask_user(
    void *userdata,
    const char *question,
    char **answer
);
```

On success:

- Return nonzero.
- Allocate `*answer` with `malloc()`-compatible ownership.
- The tool frees the answer after use.

On failure or cancellation, return zero and leave no owned answer.

### 8.2 Send message

```c
int send_message(void *userdata, const char *message);
```

Return nonzero only when the adapter successfully accepted the message.
Channel delivery may still be asynchronous, but that distinction should be
documented by the adapter.

### 8.3 Cancellation

```c
int is_cancelled(void *userdata);
```

Return nonzero after the external request has been cancelled or disconnected.
The callback must be cheap, non-blocking, and safe to invoke frequently.

Cancellation propagates to providers, subprocesses, HTTP tools, MCP, and the
agent loop.

## 9. Approval Semantics

`auto_approve` is intentionally limited:

- It can approve a `require_approval` decision for low, medium, or high risk.
- It does not auto-approve a critical-risk tool.

`no_input` means a required approval cannot be obtained interactively. The
runtime returns `AEGIS_ERR_APPROVAL_REJECTED`.

The current core approval prompt itself is CLI-oriented when input is a TTY.
A non-CLI adapter should either:

- Supply an adapter-specific approval workflow before invoking the runtime,
  or
- Keep `no_input` enabled and require pre-authorized policy.

Future work should separate approval prompting from terminal assumptions more
cleanly.

## 10. Minimal Adapter Example

```c
#include "aegis/message.h"
#include "aegis/response.h"
#include "aegis/runtime.h"
#include "aegis/session.h"

int handle_request(
    const AegisConfig *config,
    const char *user_id,
    const char *text,
    const char *workspace
)
{
    char session_id[AEGIS_SESSION_ID_MAX];
    AegisRuntime *runtime;
    AegisMessage message = {0};
    AegisResponse response;
    AegisStatus status;

    if (aegis_session_id_make(
            "adapter", session_id, sizeof(session_id)) != AEGIS_OK) {
        return 0;
    }

    runtime = aegis_runtime_new_with_config(config);
    if (!runtime) {
        return 0;
    }

    message.channel = "example";
    message.user_id = user_id;
    message.session_id = session_id;
    message.text = text;
    message.workspace = workspace;
    message.profile = config->active_profile.id;
    message.no_input = 1;

    aegis_response_init(&response);
    status = aegis_runtime_handle_message(runtime, &message, &response);

    /* Convert status and response into the external channel format here. */

    aegis_response_free(&response);
    aegis_runtime_free(runtime);
    return status == AEGIS_OK;
}
```

Production adapters need authentication, request limits, cancellation,
logging, and error mapping around this core.

## 11. HTTP Adapter Guidance

A future HTTP adapter should:

- Bind to an explicitly configured address.
- Require authentication.
- Enforce body and concurrent-request limits.
- Map disconnects to `is_cancelled`.
- Use server-side workspace/profile allowlists, not arbitrary client paths.
- Return a versioned JSON envelope.
- Avoid exposing raw local trace paths to untrusted callers.
- Rate-limit by principal and source.

Suggested input:

```json
{
  "session_id": "optional-safe-id",
  "message": "Inspect the project",
  "profile": "coding_agent"
}
```

The server should derive `user_id` from authentication and derive workspace
from server policy.

## 12. Messaging Adapter Guidance

Telegram, Discord, Slack, and similar adapters should map:

```text
platform name                  -> channel
authenticated account ID      -> user_id
conversation/thread identity  -> session_id
message text                  -> text
server-selected project       -> workspace
server-selected role          -> profile
```

Do not accept a workspace path directly from an untrusted chat message.

`send_message` should target the current conversation only unless policy
explicitly allows another destination.

## 13. Scheduled Adapter Guidance

A scheduled adapter should treat a job as a normal message:

```text
channel     = "cron"
user_id     = configured owner
session_id  = generated from job and run identity
text        = configured task
no_input    = 1
```

Scheduled tasks must use policy that never depends on an interactive prompt.

## 14. Adapter Tests

Every adapter should test:

- Required field normalization.
- Stable session mapping.
- Invalid and oversized input.
- Authentication failure.
- Workspace/profile selection rules.
- Approval behavior.
- Cancellation.
- Runtime error mapping.
- Secret-safe logs.
- Correct response ownership and cleanup.

The adapter test should use the mock provider for deterministic model output
and should exercise at least one complete model/tool/final loop.

## 15. Compatibility Boundary

The public adapter boundary consists primarily of:

- `AegisMessage`
- `AegisResponse`
- `AegisRuntime`
- `AegisStatus`
- Callback signatures

Changes to these structures affect every adapter and should be accompanied by
version notes, migration guidance, and integration tests.


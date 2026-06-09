# Aegis Tool Contract

## 1. Purpose

A tool is a controlled capability exposed to the model. It is not merely a C
function. Each Aegis tool has:

- A stable name.
- A human-readable description.
- A JSON Schema for model-facing arguments.
- A canonical risk level.
- An availability status.
- An execution function.
- Config, profile, policy, approval, workspace, and output controls.

All model-selected tools must execute through `AegisToolRegistry`.

## 2. Public Structures

```c
typedef enum {
    AEGIS_RISK_LOW = 0,
    AEGIS_RISK_MEDIUM = 1,
    AEGIS_RISK_HIGH = 2,
    AEGIS_RISK_CRITICAL = 3
} AegisRiskLevel;

typedef enum {
    AEGIS_TOOL_READY = 0,
    AEGIS_TOOL_STUB = 1
} AegisToolAvailability;

typedef struct {
    const char *name;
    const char *description;
    const char *schema_json;
    AegisRiskLevel risk_level;
    AegisToolAvailability availability;
    AegisToolExecuteFn execute;
} AegisTool;
```

The current default registry contains exactly 20 tools and all 20 are
`AEGIS_TOOL_READY`.

## 3. Registry Order

Registry order is deterministic:

```text
 1 list_dir
 2 read_file
 3 write_file
 4 append_file
 5 search_file
 6 shell
 7 run_tests
 8 git_status
 9 git_diff
10 git_log
11 git_apply_patch
12 http_get
13 http_post
14 ask_user
15 send_message
16 reminder
17 read_log
18 grep_log
19 health_check
20 mcp_tool
```

The context builder preserves this order when exposing effective tools to a
provider.

## 4. Tool Effectiveness

A registered tool is model-visible and executable only when all conditions
hold:

```text
registry contains tool
AND availability == READY
AND config.tools.enabled contains tool
AND active profile tools.requested contains tool
AND policy decision is allow or require_approval
AND config risk matches registry risk
```

If the decision is `require_approval`, execution additionally requires
approval in `AegisToolContext`.

The config is authoritative. A profile requests capability but cannot grant
it.

## 5. Tool Arguments

The action protocol requires `arguments` to be a JSON object. The agent
converts each top-level member into an `AegisKv` pair:

```c
typedef struct {
    const char *key;
    const char *value;
} AegisKv;

typedef struct {
    const AegisKv *items;
    size_t count;
} AegisToolArgs;
```

Strings are passed directly. Other JSON values are serialized to compact JSON
text before execution.

Important current limitation: JSON schemas are validated for syntactic
correctness by tests and sent to the model, but the registry does not run a
generic JSON Schema validator before execution. Each implementation manually
validates the fields it uses. Unknown fields may therefore be ignored by an
implementation even when its schema has `additionalProperties: false`.

New tools should still define a strict schema. A future generic validator can
then enforce the existing contract without changing schemas.

## 6. Tool Context

```c
typedef struct {
    const AegisConfig *config;
    const char *workspace_root;
    int approval_granted;
    int allow_write;
    int allow_shell;
    int allow_network;
    size_t max_output_bytes;
    AegisAskUserFn ask_user;
    AegisSendMessageFn send_message;
    void *adapter_userdata;
    AegisPersistReminderFn persist_reminder;
    AegisToolCancelledFn is_cancelled;
    void *state_userdata;
    const char *session_id;
} AegisToolContext;
```

Use `aegis_tool_context_from_config()` to initialize derived permission
fields. The agent then installs adapter and state callbacks.

Tool implementations must treat the context as authoritative. They should not
read unrelated global configuration or infer permission from the tool name.

## 7. Tool Result

```c
typedef struct {
    int ok;
    int exit_code;
    char *stdout_data;
    char *stderr_data;
    char *error_message;
    size_t output_bytes;
    long duration_ms;
} AegisToolResult;
```

Lifecycle:

```c
AegisToolResult result;

aegis_tool_result_init(&result);
status = aegis_tool_registry_execute(..., &result);
/* consume result */
aegis_tool_result_clear(&result);
```

The result owns all three string pointers. A tool should return both a core
`AegisStatus` and meaningful result fields.

## 8. Canonical Catalog

| Tool | Risk | Required arguments | Optional arguments |
|---|---|---|---|
| `list_dir` | low | none | `path` |
| `read_file` | low | `path` | none |
| `write_file` | medium | `path`, `content` | none |
| `append_file` | medium | `path`, `content` | none |
| `search_file` | low | `query` | `path` |
| `shell` | high | `command` | none |
| `run_tests` | medium | none | `target` |
| `git_status` | low | none | none |
| `git_diff` | low | none | `path` |
| `git_log` | low | none | `limit` |
| `git_apply_patch` | medium | `patch` | none |
| `http_get` | high | `url` | none |
| `http_post` | critical | `url` | `body` |
| `ask_user` | low | `question` | none |
| `send_message` | critical | `message` | none |
| `reminder` | medium | `message` | `due` |
| `read_log` | low | `path` | none |
| `grep_log` | low | `path`, `query` | none |
| `health_check` | medium | `url` | none |
| `mcp_tool` | high | `server`, `tool` | `arguments` |

All schemas use a top-level object and disallow additional properties.

## 9. File Tools

### 9.1 `list_dir`

```json
{"path":"src"}
```

If `path` is omitted, `"."` is used.

Behavior:

- Resolves the directory inside the workspace.
- Omits `.` and `..`.
- Omits hidden entries when hidden files are disabled.
- Skips entries that fail path policy.
- Sorts names lexicographically.
- Returns one name per line.
- Fails rather than silently truncating when output exceeds the limit.

### 9.2 `read_file`

```json
{"path":"src/main.c"}
```

Behavior:

- Requires an existing policy-approved path.
- Reads the complete file.
- Enforces the smaller of `workspace.max_file_bytes` and tool output limit.
- Is intended for text files. Embedded NUL bytes are not a supported content
  model because result strings are C strings.

### 9.3 `write_file`

```json
{"path":"src/generated.c","content":"int value = 1;\n"}
```

Behavior:

- Requires write permission and any policy approval.
- Enforces `workspace.max_file_bytes`.
- Allows a missing leaf file.
- Requires the parent directory to exist.
- Overwrites with `fopen(..., "wb")`.
- Returns `ok`.

This write is not currently an atomic temporary-file rename.

### 9.4 `append_file`

```json
{"path":"CHANGELOG.md","content":"New entry\n"}
```

Behavior:

- Uses the same path and write controls as `write_file`.
- Ensures existing size plus appended content fits the file limit.
- Creates a missing file only when its parent exists.

### 9.5 `search_file`

```json
{"query":"aegis_runtime","path":"src"}
```

Behavior:

- Defaults `path` to `"."`.
- Recurses to a maximum depth of 64.
- Uses literal case-sensitive substring matching.
- Returns `path:line-number:line`.
- Skips inaccessible child paths and unsupported file types.
- Stops scanning a file on an embedded NUL byte or an oversized line.
- Fails when accumulated matches exceed the output limit.

It is not a regular-expression engine.

## 10. Shell and Test Tools

### 10.1 `shell`

```json
{"command":"cmake --build build -j2"}
```

Execution requires:

- Tool effectiveness.
- `allow_shell`.
- An allowed command prefix.
- No built-in or config blocked pattern.
- No shell operators such as `;`, pipes, redirects, command substitution,
  `&&`, or `||`.
- Approval when policy requires it.

The command still runs through `/bin/sh -c`, but the policy intentionally
rejects shell composition operators before execution.

### 10.2 `run_tests`

```json
{}
```

Default command:

```text
ctest --test-dir build --output-on-failure
```

Custom command:

```json
{"target":"make test"}
```

The selected string passes through the same command policy as `shell`.
`target` is a command string, despite its historical name.

## 11. Git Tools

Git tools execute `git` directly with argument vectors and the workspace as
the current directory.

### `git_status`

Runs:

```text
git status --short --branch
```

### `git_diff`

Runs:

```text
git diff --no-ext-diff
```

or limits to a validated workspace path:

```json
{"path":"src/main.c"}
```

### `git_log`

Defaults to 20 entries:

```json
{"limit":50}
```

The implementation accepts 1 through 1000.

### `git_apply_patch`

```json
{"patch":"diff --git a/a.c b/a.c\n..."}
```

Before `git apply`, every `---` and `+++` path is checked against workspace
policy. `/dev/null` is accepted for create/delete patch markers. The patch is
sent on stdin to:

```text
git apply --whitespace=nowarn --recount -
```

## 12. HTTP Tools

### `http_get`

```json
{"url":"https://example.com/status"}
```

### `http_post`

```json
{"url":"https://example.com/api","body":"{\"key\":\"value\"}"}
```

HTTP controls include:

- Network and HTTP tool enablement.
- Allowed schemes.
- Private, loopback, link-local, multicast, documentation, reserved, and
  other blocked address ranges unless private networks are explicitly
  enabled.
- DNS-resolved socket address rechecking.
- No redirects.
- TLS verification.
- Request timeout.
- Response size limit.
- Cancellation.

`http_post` sends `Content-Type: application/json`. Its body is a string and
is not automatically validated as JSON.

## 13. Adapter Tools

### `ask_user`

Calls the adapter's `ask_user` callback and returns the allocated answer.
Without a callback it returns approval rejected.

### `send_message`

Calls the adapter's `send_message` callback. It is critical risk because it
creates an externally visible side effect.

### `reminder`

Persists a reminder in SQLite through the runtime state callback:

```json
{"message":"Review the deployment","due":"2026-06-12T09:00:00Z"}
```

The current implementation stores the due string but does not include a
scheduler that later delivers the reminder.

## 14. Operations Tools

### `read_log`

Reads a workspace-approved log path. If the file is larger than the output
limit, it returns the tail of the file.

### `grep_log`

Performs literal case-sensitive substring matching and returns matching lines.
It fails if matches exceed the output limit.

### `health_check`

Performs the same controlled HTTP GET operation as `http_get`, with medium
canonical risk.

## 15. MCP Tool

```json
{
  "server": "filesystem",
  "tool": "read_resource",
  "arguments": {"path":"README.md"}
}
```

`server` is a registered server name, not a raw command or URL. The tool
loads `.aegis/config/mcp.json`, rejects symlinked or escaped registry files,
resolves the endpoint, and sends `tools/call`.

MCP policy is applied both to the native `mcp_tool` and to the transport
effects. See [MCP Client](mcp-client.md).

## 16. Process Execution Contract

The shared process runner:

- Changes directory to the workspace.
- Creates a new process group.
- Redirects stdin, stdout, and stderr.
- Applies configured CPU, address-space, and file-size limits.
- Rebuilds the child environment from an allowlist.
- Applies a timeout.
- Captures bounded output.
- Kills the process group on timeout, output overflow, or cancellation.
- Reports normal exit status or `128 + signal`.

Timeout uses exit code `124`. Cancellation uses `130`.

## 17. Adding a Tool

To add a native tool:

1. Add a stable name constant in `include/aegis/tool.h`.
2. Add its constructor declaration.
3. Add a public helper header if the implementation exposes shared APIs.
4. Implement the constructor and execute function under `src/tools/`.
5. Define strict JSON Schema.
6. Assign one canonical risk.
7. Register it in deterministic order.
8. Increase `AEGIS_TOOL_COUNT`.
9. Add it exactly once to `enabled` or `disabled` in every config.
10. Add policy and risk entries to every config.
11. Add it to relevant profiles and prompts.
12. Add unit, policy, path/network/process, and synchronization tests.
13. Update this document.

Duplicate registry names are rejected.

## 18. Tool Test Expectations

Tests should cover:

- Constructor metadata and schema parsing.
- Registry uniqueness.
- Config/profile synchronization.
- Deny and approval behavior.
- Argument validation.
- Success and failure result ownership.
- Output limits.
- Cancellation and process cleanup.
- Path traversal, absolute path, hidden path, blocked path, and symlink escape.
- HTTP private-address controls.
- No secrets inherited by subprocesses outside the environment allowlist.


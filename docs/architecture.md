# Aegis Architecture

## 1. Purpose

Aegis is a channel-neutral agent runtime written in C. Its job is to take a
normalized message, run a bounded model/tool loop under explicit policy,
persist the session, write an auditable trace, and return a normalized
response.

The CLI is the first adapter. The runtime is designed so a future HTTP,
messaging, webhook, or scheduled adapter can use the same core contracts.

## 2. System Flow

```text
External input
  |
  v
Adapter
  | constructs AegisMessage
  v
AegisRuntime
  | opens state and trace
  v
AegisAgent
  | repeats a bounded loop
  +-> AegisContext builder
  +-> AegisProvider
  +-> AegisAction parser
  +-> Policy and approval
  +-> AegisToolRegistry
  +-> Tool implementation
  |
  v
AegisResponse
  |
  v
Adapter output
```

The concrete source path is:

```text
src/main.c
  -> src/cli/cli.c
  -> src/cli/cli_<command>.c
  -> src/core/runtime.c
  -> src/core/agent.c
  -> context/provider/action/tool subsystems
  -> state and trace
```

## 3. Source Layout

| Path | Responsibility |
|---|---|
| `include/aegis/` | Public C structures, enums, constants, and function declarations |
| `src/cli/` | CLI parsing, environment resolution, output formatting, and command handlers |
| `src/core/` | Runtime orchestration, agent loop, context, state, trace, sessions, and shared core behavior |
| `src/providers/` | Provider selection plus provider-specific request/response conversion |
| `src/tools/` | The 20 native tools and common path, process, HTTP, Git, and MCP helpers |
| `config/` | Built-in config presets copied into the runtime resource bundle |
| `profiles/` | Built-in agent profiles |
| `prompts/` | System prompts referenced by profiles |
| `tests/` | C unit tests and Python integration/synchronization tests |

## 4. Build-Time Components

CMake builds four static libraries:

```text
aegis_config
  JSON config and profile loading

aegis_tools
  Tool metadata, registry, workspace path controls, process execution,
  HTTP, Git, assistant, ops, and MCP tools

aegis_context
  Context ownership, prompt loading, message validation, workspace helpers

aegis_runtime
  Action parser, agent loop, runtime, provider adapters, state, trace
```

The `aegis` executable links `aegis_runtime`, which brings in the lower
layers transitively.

CMake also copies config, profile, and prompt resources into
`<build>/aegis-resources/`. The compiled CLI receives that path through
`AEGIS_DEFAULT_RESOURCE_DIR`. A deployment can override it at runtime with
`AEGIS_RESOURCE_DIR`.

## 5. Core Contracts

### 5.1 `AegisMessage`

`AegisMessage` is borrowed input owned by the adapter for the duration of
`aegis_runtime_handle_message()`:

```c
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

The validator requires non-empty `channel`, `user_id`, `session_id`, and
`text`. Runtime callers must also provide a syntactically valid session ID.

### 5.2 `AegisResponse`

`AegisResponse` owns its `text` string:

```c
typedef struct {
    char *text;
    int exit_code;
    char session_id[96];
    char trace_path[4096];
    char status[32];
    int steps;
} AegisResponse;
```

Callers initialize it with `aegis_response_init()` and release it with
`aegis_response_free()`.

### 5.3 `AegisRuntime`

`AegisRuntime` owns a copy of `AegisConfig`. A runtime can be created from a
path or an already loaded config:

```c
AegisRuntime *aegis_runtime_new(const char *config_path);
AegisRuntime *aegis_runtime_new_with_config(const AegisConfig *config);
```

The runtime:

1. Validates the message and session ID.
2. Resolves state and trace paths against the workspace.
3. Creates parent directories where required.
4. Opens SQLite state and the JSONL trace when enabled.
5. Upserts the session as `running`.
6. invokes `aegis_agent_run()`.
7. Maps the result to a persistent session status.
8. Appends `session_end`.
9. Closes state and trace resources.

## 6. Agent Loop

The agent loop is bounded by:

- `runtime.max_steps`
- `runtime.max_tool_calls`
- `runtime.max_wall_time_ms`
- Provider connection and request timeouts
- Tool-specific process or HTTP timeouts
- Adapter cancellation

One iteration performs:

1. Build a provider-neutral `AegisContext`.
2. Record `model_request`.
3. Call the selected provider, with configured retry behavior.
4. Record `model_response` or `model_error`.
5. Strictly parse one `json_action_v1` object.
6. Repair invalid JSON up to `invalid_json_repair_attempts`.
7. Return immediately for a valid `final` action.
8. For `tool_call`, find the tool and policy decision.
9. Request approval when required.
10. Execute through `AegisToolRegistry`.
11. Record `tool_call`, optional `approval`, and `tool_result`.
12. Add the result as the next model observation.

The action parser accepts only:

```json
{"type":"final","message":"Done."}
```

or:

```json
{
  "type": "tool_call",
  "tool": "read_file",
  "arguments": {"path": "src/main.c"}
}
```

Unknown fields are rejected because the parser checks the exact object size.

## 7. Context Builder

The context builder is provider-neutral. It owns deep copies of all generated
messages and tool definitions.

Message order is deterministic:

1. Profile system prompt, when enabled.
2. Workspace summary, when enabled.
3. Optional caller-provided history summary.
4. Retained history in chronological order.
5. Current user message.

The builder:

- Loads the prompt relative to the project root inferred from `config_path`.
- Rejects absolute prompt paths, traversal, missing files, and oversized
  prompt content.
- Filters observations and file reads through config/profile flags.
- Enforces event count and byte limits.
- Truncates UTF-8 only at a valid byte boundary.
- Drops oldest history first when the context is too large.
- Includes only effective, non-denied, `READY` tools.

Provider adapters render this neutral context into their own wire format.

## 8. Provider Layer

`aegis_provider_create()` selects:

| Config selector | Implementation style |
|---|---|
| `openrouter` | OpenAI chat completions |
| `openai_compat` or `openai` | OpenAI chat completions |
| `ollama` | Ollama `/api/chat` |
| `anthropic` | Anthropic Messages API |
| `gemini` | Gemini `generateContent` |
| `mock` | Environment-controlled local response |

The HTTP implementation uses libcurl with:

- TLS verification enabled.
- Redirects disabled.
- Connect and total request timeouts.
- Bounded response memory.
- Cancellation through the libcurl progress callback.
- Provider-specific authentication headers.

Native provider tool calls are normalized back into `json_action_v1` before
the action parser sees them.

## 9. Tool and Policy Layers

The registry is the only supported execution path for agent-selected tools.
It validates:

1. Tool existence.
2. `READY` availability.
3. Config/profile intersection.
4. Registry risk matching config risk.
5. Policy decision.
6. Approval status.
7. Final output limit.

Tool policy is config-authoritative. Profiles request tools but cannot enable
or allow them. See [Tool Contract](tool-contract.md) and
[Policy Model](policy-model.md).

## 10. State and Trace

SQLite state is queryable and mutable. JSONL trace is append-only.

State currently stores:

- Session records.
- Generic ordered execution events.
- Reminders.

Trace events have a strict envelope:

```json
{
  "schema_version": 1,
  "sequence": 1,
  "timestamp_ms": 1781040000000,
  "session_id": "cli_...",
  "step": 0,
  "type": "session_start",
  "payload": {}
}
```

State and trace intentionally share the session ID and event kind, but they
serve different purposes. See [Session and State](session-state.md) and
[Replay and Inspect](replay-inspect.md).

## 11. Configuration Resolution

The CLI resolves:

```text
workspace:
  --workspace
  AEGIS_WORKSPACE
  current directory

config:
  --config
  AEGIS_CONFIG
  <workspace>/.aegis/config/aegis.json
  AEGIS_RESOURCE_DIR or compiled resource config
```

`--mode` uses a local `.aegis/config/<mode>.json` when present, otherwise the
built-in preset.

The config loader then resolves the default profile relative to the config
project root. Profile aliases are a CLI concern, not a core config concern.

## 12. Error and Cancellation Flow

Core code uses `AegisStatus`. CLI code maps it to stable process exit codes.
Provider, process, HTTP, MCP, and agent loops all check cancellation.

The CLI installs a `SIGINT` handler. On cancellation:

- Provider transfers abort.
- Running subprocess groups receive `SIGKILL`.
- MCP stdio subprocesses are terminated.
- Runtime stores session status `cancelled`.
- CLI returns exit code `130`.

## 13. Architectural Invariants

These are current design invariants:

1. The adapter does not execute model-selected tools directly.
2. The model response is untrusted until strict action parsing succeeds.
3. Every agent tool call passes through the registry.
4. Config policy is stronger than profile intent.
5. File tools resolve paths against a real workspace root.
6. State and trace paths are constrained to the runtime workspace.
7. Context output owns its memory.
8. Provider wire formats do not leak into the context API.
9. A model/tool loop has step, call, wall-clock, output, and cancellation
   bounds.

## 14. Current Boundaries

Implemented:

- CLI adapter.
- Runtime and model/tool loop.
- Six provider selectors.
- Twenty native tools.
- SQLite state.
- JSONL trace.
- Replay, inspect, eval, and MCP client commands.

Not implemented as complete subsystems:

- HTTP server adapter.
- Telegram, Discord, Slack, WhatsApp, webhook, or cron adapters.
- Streaming token output.
- Kernel-level sandboxing.
- A normalized persisted message history model.
- Full MCP Streamable HTTP session management.

These boundaries matter when extending the architecture. New work should add
capabilities behind the existing contracts rather than embedding them inside
CLI command handlers.

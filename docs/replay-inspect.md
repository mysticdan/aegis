# Aegis Replay and Inspect

## 1. Purpose

Trace replay and inspection answer different questions:

```text
Replay:  What events were recorded, in what order?
Inspect: What are the session summary and selected event details?
```

Both commands are read-only. They do not contact a provider, execute a tool,
ask for approval, or modify the workspace.

## 2. Trace Location

Runtime writes one JSONL file per session by default:

```text
<trace.directory>/<session-id>.jsonl
```

Built-in config examples:

```text
traces/<session-id>.jsonl
traces/safe/<session-id>.jsonl
traces/dev/<session-id>.jsonl
traces/dangerous/<session-id>.jsonl
```

After `aegis init`, directories are normalized below `.aegis/traces/`.

## 3. Trace Event Schema

Every line is one JSON object:

```json
{
  "schema_version": 1,
  "sequence": 4,
  "timestamp_ms": 1781040000123,
  "session_id": "cli_1781040000_123_456",
  "step": 2,
  "type": "tool_call",
  "payload": {
    "tool": "read_file",
    "arguments": {"path":"README.md"},
    "decision": "allow",
    "risk": "low"
  }
}
```

Field rules:

| Field | Rule |
|---|---|
| `schema_version` | Number and exactly `1` |
| `sequence` | Positive integer, strictly increasing |
| `timestamp_ms` | Number |
| `session_id` | Non-empty string |
| `step` | Non-negative number |
| `type` | Non-empty string |
| `payload` | Any JSON value |

The writer appends and flushes each line. When reopening an existing trace it
counts newline characters to continue sequence numbering.

## 4. Current Event Types

### `session_start`

Written by runtime before entering the agent. The current payload is the task
as a JSON string.

### `model_request`

```json
{"status":"sent"}
```

The current trace intentionally does not serialize the complete provider
request or context.

### `model_response`

```json
{"content":"{\"type\":\"tool_call\",...}"}
```

### `model_error`

```json
{"error":"provider error text"}
```

### `tool_call`

```json
{
  "tool":"write_file",
  "arguments":{"path":"file.txt","content":"..."},
  "decision":"require_approval",
  "risk":"medium"
}
```

### `approval`

```json
{
  "tool":"write_file",
  "granted":true,
  "automatic":false
}
```

### `tool_result`

```json
{
  "tool":"read_file",
  "ok":true,
  "exit_code":0,
  "output_bytes":120,
  "stdout":"..."
}
```

Failure payloads may also contain `stderr`, `error`, and a core `status`.

### `final`

```json
{"message":"Task completed."}
```

### `session_end`

```json
{"status":"success","result":"OK"}
```

## 5. Trace Redaction

When enabled, trace writing:

- Replaces string values whose key contains `secret`, `token`, `password`,
  `api_key`, or `authorization`, case-insensitively.
- Replaces exact occurrences of the active provider key value with
  `[REDACTED]`.
- Traverses nested JSON objects and arrays.

Limitations:

- It knows the active provider key value, not every secret in the process.
- It does not perform entropy detection.
- It does not guarantee redaction of secrets embedded in unstructured text.
- It does not sanitize the SQLite state event separately.

Trace files must still be treated as potentially sensitive.

## 6. Strict Trace Reader

`aegis_trace_document_load()`:

- Rejects missing files.
- Rejects lines over 1 MiB.
- Rejects malformed JSON.
- Rejects trailing non-whitespace after an event.
- Validates all required envelope fields.
- Rejects non-increasing sequence values.
- Rejects empty traces.
- Clears every partially loaded event on error.

The current behavior is fail-fast. It does not return a partial document up
to the corrupt line.

## 7. Replay

Basic usage:

```bash
aegis replay --trace .aegis/traces/cli_123.jsonl
aegis replay .aegis/traces/cli_123.jsonl
```

Human output prints:

```text
[2] tool_call
    {"tool":"read_file",...}
```

JSON output:

```bash
aegis replay --trace trace.jsonl --json
```

returns:

```json
{
  "status":"success",
  "command":"replay",
  "mode":"timeline",
  "trace":"trace.jsonl",
  "events":[]
}
```

## 8. Replay Modes

Select with:

```bash
--replay-mode <mode>
```

| Mode | Behavior |
|---|---|
| `timeline` | Include all selected events |
| `dry-run` | Same read-only timeline behavior |
| `tools-only` | Event type contains `tool` |
| `policy-only` | Event type contains `policy` or `approval`, plus `tool_call` |
| `compare` | Compare with `--against` |

`dry-run` does not simulate the runtime. It is currently another explicit
name for read-only playback.

## 9. Replay Step Range

```bash
aegis replay --trace trace.jsonl --from-step 2 --to-step 5
```

The bounds are inclusive and apply to the event envelope `step`.

## 10. Trace Comparison

```bash
aegis replay \
  --trace first.jsonl \
  --replay-mode compare \
  --against second.jsonl
```

Events are compared by array position. Before comparison, these fields are
removed:

```text
sequence
timestamp_ms
session_id
```

All remaining event content must serialize identically.

Output:

- Left event count.
- Right event count.
- Count delta.
- Number of changed or unmatched events.

The comparison does not align events by type or step and does not perform
semantic JSON normalization beyond cJSON serialization.

## 11. Inspect by Trace

```bash
aegis inspect --trace trace.jsonl
```

The summary includes:

- Session ID.
- Final session status, when a `session_end` exists.
- Trace path.
- Maximum step.
- Total event count.
- Count of event types containing `tool`.
- Count of policy/approval-related events.

It then prints selected event payloads.

## 12. Inspect by Session

```bash
aegis inspect --session cli_123
```

This:

1. Loads the active config and state database.
2. Finds the session record.
3. Reads the stored trace path.
4. Loads and analyzes that trace.

It does not inspect SQLite events directly after resolving the trace.
`aegis sessions show <id>` is the command for viewing stored state events.

## 13. Inspect Filters

```bash
aegis inspect --trace trace.jsonl --step 3
aegis inspect --trace trace.jsonl --tools
aegis inspect --trace trace.jsonl --policy
```

Filters can be combined.

Selection rules:

- `--step` requires exact step equality.
- `--tools` selects event types containing `tool`.
- `--policy` selects types containing `policy` or `approval`, plus
  `tool_call`.

There is no current `--errors` filter.

## 14. JSON Inspection Output

```json
{
  "status":"success",
  "command":"inspect",
  "session_id":"cli_123",
  "session_status":"success",
  "trace":"trace.jsonl",
  "steps":2,
  "event_count":8,
  "tool_events":2,
  "policy_events":1,
  "events":[]
}
```

The `events` array contains only selected events, while summary counts are
calculated over the complete document.

## 15. Replay and Inspect Safety

Both commands parse trace content as data. They never execute a command found
inside a payload.

However, rendering a trace may expose:

- File contents.
- Commands.
- HTTP responses.
- Model responses.
- User tasks.
- Local paths.

Do not publish trace output without review.

## 16. Troubleshooting

`invalid trace` can mean:

- File missing.
- Empty file.
- A malformed JSON line.
- A line over 1 MiB.
- Missing required envelope field.
- Unsupported schema version.
- Duplicate or decreasing sequence number.

Use a JSON-aware line inspection tool on a copy of the trace. Do not "fix"
the original audit record without preserving it.

## 17. Compatibility Guidance

Trace schema version `1` is the current reader/writer contract. New event
types are naturally forward-compatible because the reader validates the
envelope but does not restrict `type`.

Envelope changes require a new schema version and explicit reader behavior.
Replay should never silently reinterpret an incompatible trace.


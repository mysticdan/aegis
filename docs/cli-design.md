# Aegis CLI Design and Reference

## 1. Role of the CLI

The CLI is the current user-facing adapter for Aegis. It parses terminal
input, resolves configuration, constructs `AegisMessage`, invokes
`AegisRuntime`, renders `AegisResponse`, and maps core statuses to process
exit codes.

The CLI must not become a second agent runtime. Model calls, action parsing,
tool execution, state writes, and trace events belong to shared core layers.

The implementation is intentionally modular:

```text
src/cli/cli.c              parser, common resolution, dispatch
src/cli/cli_init.c         init
src/cli/cli_run.c          run
src/cli/cli_chat.c         chat
src/cli/cli_resume.c       resume
src/cli/cli_sessions.c     sessions
src/cli/cli_inspect.c      inspect
src/cli/cli_replay.c       replay
src/cli/cli_eval.c         eval
src/cli/cli_tools.c        tools
src/cli/cli_config.c       config
src/cli/cli_profiles.c     profiles
src/cli/cli_mcp.c          mcp
src/cli/cli_doctor.c       doctor
src/cli/cli_completion.c   completion
src/cli/cli_version.c      version
src/cli/cli_help.c         help
```

## 2. Command Shape

```text
aegis <command> [options]
```

Top-level commands:

| Command | Purpose |
|---|---|
| `init` | Install workspace-local Aegis resources |
| `run` | Run one task or validate it with `--dry-run` |
| `chat` | Run an interactive multi-turn terminal session |
| `resume` | Continue a persisted session |
| `sessions` | List, show, delete, or clean sessions |
| `inspect` | Analyze a trace or a session's trace |
| `replay` | Print, filter, or compare trace events |
| `eval` | Run an evaluation suite |
| `tools` | List, inspect, view schemas, or manually test tools |
| `config` | Check, display, read, or update config |
| `profiles` | List, show, validate, or create profiles |
| `mcp` | Manage and call registered MCP servers |
| `doctor` | Check the active installation |
| `completion` | Print top-level shell completion |
| `version` | Print version and feature information |
| `help` | Print general or command-specific help |

Use:

```bash
aegis help
aegis help run
aegis run --help
aegis --version
```

## 3. Shared Options

The parser recognizes these options:

| Option | Meaning |
|---|---|
| `--config <path>` | Select one config file |
| `--mode <safe|dev|dangerous>` | Select a named preset |
| `--profile <id|alias>` | Override the active profile |
| `--workspace <path>` | Select the workspace |
| `--workspace-root <path>` | Alias of `--workspace` |
| `--state-dir <path>` | Override state directory, producing `<path>/state.db` |
| `--provider <name>` | Select a configured provider |
| `--model <name>` | Override model name |
| `--session <id>` | Select or name a session where supported |
| `--trace <path>` | Select a trace path where supported |
| `--approval <mode>` | Override approval behavior for allowed tools |
| `--max-steps <n>` | Tighten, never increase, the configured limit |
| `--max-output-bytes <n>` | Tighten the tool output limit |
| `--yes` | Auto-approve non-critical approval gates |
| `--no-input` | Disable interactive input |
| `--json` | Emit one machine-readable JSON object |
| `--quiet` | Minimize human-readable output |
| `--verbose` | Add diagnostics |
| `--no-color` | Accepted for script compatibility |

`--quiet` and `--verbose` cannot be combined. `--mode` and `--config` cannot
be combined.

Approval override values:

```text
never
on_write
on_shell
on_risky_action
always
```

An override can make an existing `allow` decision stricter. It does not turn
a config-level `deny` into `allow`.

## 4. Environment Resolution

### 4.1 Workspace

```text
1. --workspace or --workspace-root
2. AEGIS_WORKSPACE
3. current directory
```

The selected value must resolve with `realpath()` to an existing directory.

### 4.2 Config

Without `--mode`:

```text
1. --config
2. AEGIS_CONFIG
3. <workspace>/.aegis/config/aegis.json
4. <resource-dir>/config/aegis.json
```

With `--mode`:

```text
1. <workspace>/.aegis/config/<mode>.json
2. <resource-dir>/config/<mode>.json
```

Workspace-local configs must be regular files, not symlinks.

`<resource-dir>` is `AEGIS_RESOURCE_DIR` when that environment variable is
set, otherwise the resource directory compiled by CMake.

### 4.3 Profile

The config default is used unless `--profile` is present. Aliases are:

| Alias | Profile ID |
|---|---|
| `coding` | `coding_agent` |
| `minimal` | `minimal_agent` |
| `security` | `security_agent` |
| `ops` | `ops_agent` |
| `assistant` | `assistant_agent` |

## 5. Output Rules

Human-readable output goes to stdout. Errors and interactive approval prompts
go to stderr.

With `--json`, the command writes one JSON object to stdout. Error objects use:

```json
{
  "status": "error",
  "command": "run",
  "exit_code": 5,
  "error": "Provider error"
}
```

Scripts should use both the process exit code and the JSON `status`.

## 6. Exit Codes

| Code | Constant | Meaning |
|---:|---|---|
| 0 | `AEGIS_CLI_EXIT_SUCCESS` | Success |
| 1 | `AEGIS_CLI_EXIT_GENERAL` | General runtime or unclassified error |
| 2 | `AEGIS_CLI_EXIT_USAGE` | Invalid command, option, or argument |
| 3 | `AEGIS_CLI_EXIT_CONFIG` | Config, resource, or config mutation failure |
| 4 | `AEGIS_CLI_EXIT_PROFILE` | Profile failure |
| 5 | `AEGIS_CLI_EXIT_PROVIDER` | Provider failure |
| 6 | `AEGIS_CLI_EXIT_TOOL` | Tool execution failure |
| 7 | `AEGIS_CLI_EXIT_POLICY` | Policy denial |
| 8 | `AEGIS_CLI_EXIT_APPROVAL` | Required approval was not granted |
| 9 | `AEGIS_CLI_EXIT_WORKSPACE` | Invalid workspace or path escape |
| 10 | `AEGIS_CLI_EXIT_MAX_STEPS` | Step or tool-call bound reached |
| 11 | `AEGIS_CLI_EXIT_STATE` | SQLite or session failure |
| 12 | `AEGIS_CLI_EXIT_TRACE` | Trace missing or invalid |
| 13 | `AEGIS_CLI_EXIT_EVAL` | One or more eval cases failed |
| 130 | `AEGIS_CLI_EXIT_INTERRUPTED` | Interrupted by `SIGINT` |

## 7. `aegis init`

```bash
aegis init [--workspace <path>] [--mode <mode>] \
  [--profile <id|alias>] [--force]
```

Behavior:

- Requires an existing real workspace directory.
- Rejects managed files or directories that are symlinks.
- Copies four configs, five profiles, and five prompts.
- Creates state and trace directories.
- Rewrites active config state, trace, and log paths under `.aegis/`.
- Makes `.aegis/config/aegis.json` the active config.
- Selects `aegis` resources by default or the requested mode.
- Rewrites both config default-profile fields when `--profile` is supplied.
- Writes managed files through temporary files and atomic rename.
- Validates the installed config, profile, and tool registry afterward.

Without `--force`:

- A valid complete installation returns `already_initialized`.
- Missing managed files may be added.
- A conflicting managed file produces a config error.

With `--force`, managed templates are refreshed. Foreign files, state
contents, and trace contents are retained.

## 8. `aegis run`

Accepted task sources:

```bash
aegis run --task "Explain this code"
aegis run --task-file task.txt
aegis run "Explain this code"
printf '%s\n' "Explain this code" | aegis run -
```

Exactly one task source is required. Input is limited to 1 MiB, cannot be
blank, and cannot contain a NUL byte.

Useful options:

```text
--dry-run
--session <id>
--trace <relative-path>
--max-steps <n>
--max-output-bytes <n>
--approval <mode>
--yes
--no-input
```

`--dry-run` loads and validates the workspace, config, profile, provider,
model, limits, and registry. It does not call the model or a tool.

Normal execution creates a session ID unless `--session` is supplied. A
successful JSON response contains:

```json
{
  "status": "success",
  "command": "run",
  "session_id": "cli_...",
  "steps": 2,
  "final": "Task completed.",
  "trace": "/workspace/.aegis/traces/cli_....jsonl"
}
```

## 9. `aegis chat`

```bash
aegis chat [--session <id>] [environment options]
```

Chat is interactive and rejects `--json` and `--no-input`.

Built-in commands:

| Command | Effect |
|---|---|
| `/help` | List chat commands |
| `/exit` | Leave chat |
| `/session` | Print current session ID |
| `/tools` | Print effective tools |
| `/profile` | Print active profile |
| `/workspace` | Print workspace |
| `/trace` | Print trace path |
| `/clear` | Clear the terminal |
| `/compact` | Replace in-memory chat history with a compact marker |

Current chat history is reconstructed as text and included in the next task.
It is not yet a normalized role-by-role message store. Resuming chat from an
existing session loads the stored original task and final result as a compact
history seed.

## 10. `aegis resume`

```bash
aegis resume <session-id> [environment options]
aegis resume --session <session-id> [environment options]
```

Resume:

1. Loads the selected state database.
2. Finds the session.
3. Loads its stored profile unless a profile override is supplied.
4. Reads prior generic events.
5. Builds a synthetic continuation task containing the original task and
   event JSON.
6. Reuses the session ID and trace path.
7. Continues step numbering from the stored step count.

Resume does not blindly re-execute a recorded tool call. It asks the model to
continue from the latest recorded state.

## 11. `aegis sessions`

```bash
aegis sessions list
aegis sessions show <session-id>
aegis sessions delete <session-id> [--keep-trace]
aegis sessions clean --older-than <Nm|Nh|Nd> [--keep-trace]
```

`delete` and `clean` remove trace files only when the trace resolves inside
the current workspace. SQLite foreign keys cascade associated events and
reminders.

Examples:

```bash
aegis sessions clean --older-than 30d
aegis sessions delete cli_123 --keep-trace
```

## 12. `aegis inspect`

```bash
aegis inspect --trace <path>
aegis inspect --session <id>
```

Exactly one source is required.

Filters:

```text
--step <n>
--tools
--policy
```

Inspection calculates session ID, final status, maximum step, event count,
tool event count, and policy/approval event count. It then prints selected
events or returns them under `events` in JSON mode.

## 13. `aegis replay`

```bash
aegis replay --trace <path>
aegis replay <path>
```

Options:

```text
--replay-mode timeline|dry-run|compare|tools-only|policy-only
--from-step <n>
--to-step <n>
--against <trace>
--json
```

`timeline` and `dry-run` are read-only timeline views. Neither contacts a
provider nor executes a tool.

`compare` compares events positionally and ignores `sequence`,
`timestamp_ms`, and `session_id`. It reports event counts, count delta, and
changed event count. It is a structural comparison, not a semantic model
evaluation.

## 14. `aegis eval`

```bash
aegis eval --suite <path> [--fail-fast] [environment options]
```

The suite format is documented in [Evaluation Design](eval-design.md).
Each case may run directly in the selected workspace or in a temporary copy
when it declares a workspace. Cases can assert final-text content and run a
success command.

Exit `0` means every executed case passed. Exit `13` means at least one
failed. `Ctrl+C` returns `130`.

## 15. `aegis tools`

```bash
aegis tools list
aegis tools info <name>
aegis tools schema <name>
aegis tools test <name> --args '<json-object>' [--yes]
```

`list` displays all 20 registry entries, including disabled entries. Quiet
mode prints only effective tool names.

`test` still applies config/profile intersection, risk consistency, policy,
approval, workspace, network, and output controls. It is not a policy bypass.
`--yes` does not auto-approve a critical-risk tool.

## 16. `aegis config`

```bash
aegis config check
aegis config path
aegis config show
aegis config get <dot.path>
aegis config set <dot.path> <json-value>
```

Examples:

```bash
aegis config get runtime.max_steps
aegis config set runtime.max_steps 20
aegis config set model.profiles.default.temperature 0.2
```

`set`:

- Works only on a workspace-local `.aegis/config/*.json`.
- Updates an existing path only.
- Parses the new value as JSON.
- Writes atomically.
- Reloads and validates the temporary config before rename.

Use quotes when the replacement is a JSON string:

```bash
aegis config set model.profiles.default.model '"my-model"'
```

## 17. `aegis profiles`

```bash
aegis profiles list
aegis profiles show <id|alias>
aegis profiles validate <id|alias>
aegis profiles new <id>
```

`new` requires an initialized workspace. It copies the local
`coding_agent.json`, replaces `id`, and replaces `identity.name`. The caller
should then edit identity, prompt, limits, and requested tools before using
the new profile.

Profile IDs may contain letters, digits, `_`, and `-`.

## 18. `aegis mcp`

```bash
aegis mcp list
aegis mcp add <name> --cmd '<server command>'
aegis mcp add <name> --url <endpoint>
aegis mcp remove <name>
aegis mcp tools
aegis mcp call <server/tool> --args '<json-object>'
```

The registry is stored at:

```text
<workspace>/.aegis/config/mcp.json
```

MCP calls require `mcp_tool` to be effective. For built-in resources, that
normally means selecting a config that enables MCP and a profile that
requests it, for example:

```bash
aegis mcp list --mode dangerous --profile ops
aegis mcp tools --mode dangerous --profile ops
```

See [MCP Client](mcp-client.md) for protocol and security details.

## 19. `aegis doctor`

```bash
aegis doctor [environment options]
```

Checks:

- Binary version.
- Config and profile loading.
- Workspace validity.
- State path writability.
- Trace path writability.
- Provider credential presence.
- SQLite availability.
- libcurl availability.
- Effective tool count.

A missing provider key is a warning and does not make `doctor` fail. Invalid
state or trace paths do.

## 20. Completion, Version, and Help

```bash
aegis completion bash
aegis completion zsh
aegis completion fish
aegis version
aegis version --json
aegis help [command]
```

Current completion output covers top-level commands only.

## 21. Dangerous Mode

Agent execution through `run`, `chat`, `resume`, and `eval` prints a warning
when the loaded config mode is `dangerous`.

Interactive use asks for confirmation. Non-interactive use must pass
`--yes`, otherwise exit code `8` is returned. Critical tool approvals remain
separate and are not automatically granted by `--yes`.

## 22. Signal Handling

The CLI handles `SIGINT` by setting a cancellation flag. The flag propagates
through:

- The agent loop.
- Provider transfers.
- Tool subprocess execution.
- HTTP tools.
- MCP stdio execution.

The runtime records a cancelled session when cancellation reaches the core.

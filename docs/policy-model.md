# Aegis Policy Model

## 1. Purpose

The policy model decides whether a registered model-selected tool can be
exposed and executed. Policy is intentionally config-authoritative: a prompt
or profile may request a capability, but it cannot grant permission.

The three decisions are represented as config strings:

```text
allow
deny
require_approval
```

Unknown tools and unknown decisions fail closed.

## 2. Effective Tool Formula

For a tool `T`:

```text
effective(T) =
    registry_contains(T)
    AND registry_availability(T) == READY
    AND T in config.tools.enabled
    AND T in active_profile.tools.requested
    AND config.policy.decisions[T] != deny
    AND config.policy.risk_levels[T] == registry_risk(T)
```

Execution additionally requires:

```text
decision == allow
OR
(decision == require_approval AND approval_granted)
```

This check exists in the registry even if the caller already filtered tools
for model context.

## 3. Authority Order

The JSON contract declares:

```json
{
  "tool_access": "intersection",
  "policy_authority": "config",
  "limit_strategy": "most_restrictive",
  "model_source": "config"
}
```

Meaning:

- Tool access is config/profile intersection.
- Config policy wins over profile intent.
- Runtime and context limits choose the tighter config/profile value.
- The model identity comes from config.
- Profiles may override only allowed generation values such as
  `temperature`, `top_p`, and `max_tokens`.

## 4. Canonical Risks

| Risk | Tools |
|---|---|
| low | `list_dir`, `read_file`, `search_file`, `git_status`, `git_diff`, `git_log`, `ask_user`, `read_log`, `grep_log` |
| medium | `write_file`, `append_file`, `run_tests`, `git_apply_patch`, `reminder`, `health_check` |
| high | `shell`, `http_get`, `mcp_tool` |
| critical | `http_post`, `send_message` |

Config risk values must exactly match registry metadata. A mismatch makes the
config invalid.

Risk is classification, not permission. A low-risk tool may still be denied,
and a high-risk tool may be allowed in a deliberately permissive config.

## 5. Built-In Presets

### 5.1 Summary

| Preset | Mode | Default profile | Steps | Tool calls | Effective intent |
|---|---|---|---:|---:|---|
| `aegis.json` | balanced | `coding_agent` | 30 | 80 | Coding with approval for writes, shell, tests, and patch |
| `safe.json` | safe | `minimal_agent` | 12 config / 8 profile | 24 config / 16 profile | Read-only list and read |
| `dev.json` | dev | `coding_agent` | 40 config / 30 profile | 120 config / 80 profile | Writes and tests allowed; shell approval |
| `dangerous.json` | dangerous | `coding_agent` | 60 config / 30 profile | 200 config / 80 profile | Broad config permissions; profile still narrows model-visible tools |

Because limits are most restrictive, profile limits may reduce the config
values shown above.

### 5.2 Safe

Enabled and allowed:

```text
list_dir
read_file
```

Every other catalog tool is disabled and denied. Network, HTTP, shell, write,
and MCP are disabled.

### 5.3 Balanced default

Allowed immediately:

```text
list_dir
read_file
search_file
git_status
git_diff
git_log
```

Require approval:

```text
write_file
append_file
shell
run_tests
git_apply_patch
```

All other tools are disabled and denied. Network is disabled.

### 5.4 Dev

Allowed:

```text
list_dir
read_file
write_file
append_file
search_file
run_tests
git_status
git_diff
git_log
git_apply_patch
```

Requires approval:

```text
shell
```

Network, HTTP effects, assistant effects, ops tools, and MCP remain disabled
or denied.

### 5.5 Dangerous

All 20 tools are enabled in config.

Allowed:

```text
list_dir read_file write_file append_file search_file
shell run_tests git_status git_diff git_log git_apply_patch
http_get ask_user reminder read_log grep_log health_check mcp_tool
```

Require approval:

```text
http_post
send_message
```

The default `coding_agent` profile requests only coding tools. Selecting
dangerous config does not automatically expose all 20 tools to that profile.
For example, MCP also needs a profile such as `ops_agent` or
`security_agent`.

## 6. Profile Requests

| Profile | Requested capability groups |
|---|---|
| `minimal_agent` | list, read, search |
| `coding_agent` | files, search, shell, tests, Git |
| `security_agent` | coding audit set, HTTP GET, MCP |
| `ops_agent` | files, search, shell, tests, Git read, logs, health, HTTP, MCP |
| `assistant_agent` | list, read, search, HTTP GET, ask, send, reminder |

Examples:

- `safe + minimal`: `list_dir`, `read_file`.
- `safe + coding`: still only config-allowed tools requested by coding, which
  means `list_dir` and `read_file`.
- `dangerous + coding`: coding tool set, not assistant/MCP/ops tools.
- `dangerous + ops`: the ops-requested subset of all 20.

## 7. Approval

The agent checks `require_approval` before registry execution.

Current terminal behavior:

```text
Approval required for tool '<name>' (risk=<risk>). Approve? [y/N]
```

Rules:

- `y` or `Y` grants one execution.
- EOF or any other answer denies.
- `--no-input` denies required approval.
- Non-TTY execution denies required approval.
- `--yes` auto-approves non-critical tools.
- `--yes` never auto-approves critical tools.

An approval event records:

```json
{
  "tool": "write_file",
  "granted": true,
  "automatic": false
}
```

## 8. Approval Overrides

The CLI `--approval` option can convert existing `allow` decisions to
`require_approval`:

| Mode | Added approval gates |
|---|---|
| `never` | No added gates; existing config gates remain |
| `on_write` | write, append, apply patch |
| `on_shell` | shell and tests |
| `on_risky_action` | every non-low allowed tool |
| `always` | every allowed tool |

Despite its name, `never` means "do not add approval gates." It does not
remove a `require_approval` decision already present in config.

## 9. Path Policy

File-like tools use common path resolution:

- Empty paths are rejected where a path is required.
- Absolute paths are rejected.
- `..` components are rejected.
- Hidden path segments are rejected when hidden files are disabled.
- Configured blocked paths are rejected.
- Existing paths are canonicalized with `realpath()`.
- Resolved paths must remain below the canonical workspace root.
- Symlink components are rejected when `follow_symlinks` is false.
- A missing leaf is allowed only for create/append operations.

Default blocked values include:

```text
.git/objects
.env
.ssh
id_rsa
id_ed25519
secrets.json
```

`dangerous` mode retains the workspace boundary and blocked paths.

## 10. Command Policy

Shell execution requires an allowed command prefix. It also rejects:

- Built-in dangerous substrings.
- Configured blocked command substrings.
- Shell composition operators.

Rejected operators include:

```text
;
newline
carriage return
`
&&
||
$(
|
>
<
```

The built-in block list covers examples such as `sudo`, `rm -rf /`, `mkfs`,
`dd if=`, mount operations, broad permission changes, pipe-to-shell patterns,
shutdown, reboot, and firewall commands.

This is a guardrail, not a formal shell parser. See
[Sandbox Model](sandbox-model.md).

## 11. Network Policy

HTTP execution requires all of:

```text
tool effective
AND sandbox network enabled
AND tools.http.enabled
AND scheme allowed
AND host/address allowed
AND policy approval when required
```

By default, private and special-use address ranges are blocked. Redirects are
disabled, so a permitted public URL cannot redirect into a blocked network.

`http_post` is critical because it can create effects and exfiltrate data.

## 12. MCP Policy

MCP has two policy layers:

1. The native `mcp_tool` must be effective.
2. The registered transport must pass command or HTTP controls.

The model supplies a registered server name, not a raw endpoint. This prevents
it from selecting an arbitrary process or URL through `mcp_tool`.

MCP result content remains untrusted and is returned as an observation.

## 13. Policy Tracing

The current trace does not emit a separate event named `policy_decision`.
Instead, the `tool_call` payload contains:

```json
{
  "tool": "shell",
  "arguments": {"command":"ctest --test-dir build"},
  "decision": "require_approval",
  "risk": "high"
}
```

An `approval` event follows when applicable, then `tool_result`.

Inspect policy filtering therefore includes event types containing `policy`
or `approval`, plus `tool_call`.

## 14. Fail-Closed Cases

Execution is denied or rejected when:

- Tool is absent from the registry.
- Tool is a stub.
- Tool is disabled by config.
- Tool is not requested by profile.
- Policy is missing.
- Policy says deny.
- Risk differs between registry and config.
- Approval is required but not granted.
- Tool-specific write, shell, network, path, command, or MCP checks fail.

## 15. Policy Testing

The synchronization and execution suites should continue to verify:

- Exactly 20 unique names.
- Enabled/disabled is a complete partition.
- Every tool has a policy and risk.
- Config risk equals registry risk.
- Profile requests reference known tools.
- Safe mode is read-only.
- Dev mode does not enable network.
- Dangerous mode keeps blocked paths and destructive command blocks.
- Config/profile intersection.
- Deny and approval execution behavior.
- Critical tools are not auto-approved.
- Path, command, HTTP, and MCP secondary controls.


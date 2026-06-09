# Aegis Roadmap

## 1. Status

The current source tree reports version `1.0.0`. The major runtime path is
operational:

- Modular CLI.
- Config and profile loading.
- Provider-neutral context construction.
- OpenRouter, OpenAI-compatible, Ollama, Anthropic, Gemini, and mock
  providers.
- Strict action parsing.
- Config-authoritative policy and approval.
- Twenty `READY` tools.
- SQLite session state.
- JSONL traces.
- Replay, inspect, eval, MCP, and sanitizer-backed tests.

This document is therefore a forward roadmap, not the old staged plan in
which shell, SQLite, replay, eval, and MCP were still future milestones.

## 2. Roadmap Principles

Future work should preserve these priorities:

1. Keep security controls in common runtime paths.
2. Prefer stable public contracts over adapter-specific shortcuts.
3. Test observable behavior, not only individual helper functions.
4. Keep config and profile semantics deterministic.
5. Document limitations as carefully as features.
6. Avoid expanding the tool surface without policy, schema, trace, and tests.

## 3. Baseline for 1.0

The current baseline includes:

| Area | Current state |
|---|---|
| CLI | All planned v1 commands have handlers under `src/cli/` |
| Runtime | Synchronous bounded agent loop |
| Providers | Six selectors, native tool-call normalization |
| Tools | 20 registered and implemented |
| Policy | Per-tool allow, deny, or require approval |
| Sandbox | Soft POSIX controls and selected `rlimit` values |
| State | SQLite sessions, events, and reminders |
| Trace | Strict JSONL schema version 1 |
| MCP | stdio plus minimal HTTP JSON-RPC client |
| Quality | Strict warning build, CTest, ASan, and UBSan |

The project should not call itself fully hardened merely because this baseline
exists. The threat model still identifies important residual risk.

## 4. Near-Term Reliability

### 4.1 Stable Public Schema Documentation

- Publish explicit JSON schemas for configs, profiles, eval suites, MCP
  registry, CLI JSON output, and trace events.
- Add schema version migration rules.
- Add compatibility tests against saved fixtures.

### 4.2 State and Resume Fidelity

- Persist normalized user, assistant, and tool messages.
- Resume from structured history rather than embedding all prior events into
  a synthetic prompt.
- Persist selected provider, model, config identity, and effective policy.
- Detect sessions left `running` after a crash and mark them interrupted.

### 4.3 Atomic Workspace Mutation

- Add optional write previews.
- Support atomic write and append behavior where practical.
- Record before/after hashes for mutations.
- Improve Git patch diagnostics and rollback guidance.

### 4.4 Output and Error Contracts

- Make all command-specific `--help` pages complete.
- Define one versioned JSON output schema per command family.
- Preserve provider error details without exposing credentials.
- Add structured timeout and truncation fields to tool results.

## 5. Security Hardening

### 5.1 Process Isolation

Candidate Linux features:

- Landlock workspace restrictions.
- seccomp profiles.
- User, mount, PID, and network namespaces.
- cgroup resource controls.
- No-new-privileges enforcement.

These should be optional and capability-detected. A broken hard sandbox that
silently falls back is worse than an explicit soft sandbox.

### 5.2 Command Policy

The current command policy is string-based and intentionally conservative.
Future work should:

- Parse command arguments without shell ambiguity.
- Prefer direct `execve()` argument vectors.
- Replace broad prefix matching with typed command policies.
- Add command-specific path validation.
- Separate read-only Git commands from mutation commands.

### 5.3 Network Policy

- Add host and domain allowlists.
- Revalidate every redirect if redirects are ever enabled.
- Add DNS rebinding tests.
- Add proxy policy.
- Separate internet access from local/private service access.
- Attach declared network effects to MCP tools.

### 5.4 Secret Handling

- Expand redaction beyond configured provider key values and sensitive key
  names.
- Introduce secret value registration without storing raw values in trace
  objects.
- Add trace/state file permission policy.
- Consider encrypted state as an optional deployment feature.

## 6. Provider Improvements

- Streaming responses with cancellation and bounded buffering.
- Provider-specific structured-output support where available.
- Tool result messages with native tool-call IDs.
- Better token usage normalization.
- Retry classification for transient and permanent errors.
- Configurable headers through a validated generic mechanism.
- Live compatibility tests gated by opt-in credentials.

The provider-neutral context and action contracts should remain stable while
wire adapters evolve.

## 7. Adapter Expansion

New adapters should be implemented only after the adapter contract is covered
by reusable tests.

Recommended order:

1. Local HTTP server adapter.
2. Generic authenticated webhook adapter.
3. Scheduled/cron adapter.
4. One interactive messaging adapter.
5. Additional messaging platforms only when maintenance ownership exists.

Every adapter must:

- Normalize to `AegisMessage`.
- Use `AegisRuntime`.
- Supply approval, user-interaction, send-message, and cancellation callbacks
  where appropriate.
- Enforce channel authentication and input limits.
- Avoid bypassing tool policy.

## 8. MCP Maturity

The current MCP implementation is deliberately small. A complete client
roadmap includes:

- Stateful Streamable HTTP sessions.
- Protocol negotiation and capability validation.
- Server lifecycle reuse instead of one process per request.
- `tools/list` metadata import with explicit Aegis policy mapping.
- Roots and resource support.
- Per-server timeout, environment, and sandbox configuration.
- Server stderr separation from protocol stdout.
- Conformance fixtures and malicious-server tests.

MCP servers remain untrusted even after these features are implemented.

## 9. Evaluation Maturity

The existing eval runner supports repeatable cases, isolated workspace copies,
final text assertions, success commands, and fail-fast behavior.

Future additions:

- Versioned suite schema.
- Per-case profile, provider, model, and limits.
- Expected policy outcomes.
- Workspace diff capture.
- Duration, token, tool-call, denial, and approval metrics.
- JSON report files and historical comparison.
- Deterministic mock-provider fixtures.
- Safety, replay, adapter, and provider compatibility suites.

## 10. Developer Experience

- Add CMake install and packaging rules.
- Generate API documentation from public headers.
- Add examples for embedding `AegisRuntime`.
- Add config/profile schema validation tooling.
- Add shell completion for subcommands and options, not only top-level
  commands.
- Publish a contribution guide and release process.
- Add a license file before distribution.

## 11. Testing Roadmap

The current suites cover tools, context, runtime, CLI integration, catalog
synchronization, and sanitizer builds.

Recommended additions:

- Fuzz `aegis_action_parse()`.
- Fuzz config/profile parsing.
- Fuzz path normalization and symlink layouts.
- Fuzz trace and MCP JSON-RPC parsing.
- Fault-injection tests for allocation and I/O failures.
- Provider response corpus tests.
- Concurrent state access tests.
- Long-running cancellation and process cleanup tests.
- Platform CI across supported compilers and Linux distributions.

## 12. Compatibility Policy

Before a tagged stable release, compatibility should be stated explicitly for:

- CLI command names and exit codes.
- Config/profile schema version `0.1`.
- Trace schema version `1`.
- Tool names and JSON schemas.
- `AegisMessage`, `AegisResponse`, and public C APIs.
- SQLite schema migration behavior.

Until such a policy is published and releases are tagged, downstream users
should treat source-level interfaces as stabilizing rather than immutable.


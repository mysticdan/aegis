# Aegis Evaluation Design

## 1. Purpose

Unit and integration tests verify implementation behavior. Evaluation suites
exercise the complete agent path on repeatable tasks:

```text
suite case
  -> optional workspace copy
  -> runtime and provider
  -> model/tool loop
  -> optional final-text assertion
  -> optional success command
  -> pass/fail result
```

The current eval runner is intentionally small. It provides a useful
foundation but not yet a complete benchmark reporting system.

## 2. Command

```bash
aegis eval --suite <path> [--fail-fast] [environment options]
```

Relevant shared options:

```text
--workspace or --workspace-root
--config or --mode
--profile
--provider
--model
--max-steps
--max-output-bytes
--yes
--json
```

`--max-steps` may only tighten the effective limit.

## 3. Current Suite Schema

The root must be an object containing:

```json
{
  "id": "suite_id",
  "cases": []
}
```

Each case supports:

```json
{
  "id": "case_id",
  "task": "Task sent to the agent",
  "workspace": "optional/path",
  "expect_final_contains": "optional substring",
  "success_command": "optional command"
}
```

Only `id` and `task` are required by the current runner.

## 4. Complete Example

```json
{
  "id": "coding_smoke",
  "cases": [
    {
      "id": "explain_readme",
      "task": "Read README.md and finish with the phrase README checked.",
      "expect_final_contains": "README checked"
    },
    {
      "id": "fix_fixture",
      "workspace": "evals/workspaces/fix_fixture",
      "task": "Fix the implementation without changing the tests.",
      "expect_final_contains": "completed",
      "success_command": "ctest --test-dir build --output-on-failure"
    }
  ]
}
```

Run it:

```bash
aegis eval \
  --suite evals/suites/coding_smoke.json \
  --mode dev \
  --provider mock
```

## 5. Workspace Semantics

### 5.1 No case workspace

When `workspace` is absent, the case runs directly in the CLI-selected
workspace.

### 5.2 Case workspace

When `workspace` is present:

- An absolute value is used as the source.
- A relative value is resolved below the CLI-selected workspace.
- A temporary root is created with `mkdtemp()`.
- The source is copied with `cp -a` to `<temp>/workspace`.
- The agent runs in the copied workspace.
- The temporary root is removed afterward.

The original fixture is therefore not modified by normal case execution.

The suite file is trusted local input. It can select absolute source paths.

## 6. Per-Case Execution

For each case, the runner:

1. Increments the total count.
2. Validates string `id` and `task`.
3. Prepares the workspace.
4. Generates an `eval_...` session ID.
5. Builds an `AegisMessage` with channel/user `eval`.
6. Sets `no_input = 1`.
7. Runs the normal runtime and agent loop.
8. Requires `AegisStatus == AEGIS_OK`.
9. Applies optional final substring matching.
10. Applies optional success command.
11. Records a boolean result.
12. Cleans the temporary workspace.

## 7. Approval Implications

Eval messages are non-interactive. The current runner sets `no_input = 1`
and does not set `auto_approve`.

Therefore, a model tool call with `require_approval` fails the case. This is
useful for deterministic safety but means:

- The balanced preset cannot autonomously perform writes or shell calls.
- The `dev` preset is more suitable for coding evals because writes,
  `run_tests`, and patch application are allowed.
- `shell` still requires approval in `dev`.
- Critical actions remain unsuitable for normal eval cases.

Choose a preset whose policy already allows the intended case behavior.

## 8. Final Text Assertion

`expect_final_contains` uses a case-sensitive substring search:

```c
strstr(response.text, expected)
```

It is appropriate for simple deterministic fixtures. It is not a semantic
quality metric.

For mock-provider tests, make the expected final output exact enough to catch
the intended behavior.

## 9. Success Command

When present, `success_command` runs after a successful agent result.

It uses the Aegis process runner with:

- The case workspace as cwd.
- Configured environment policy.
- Configured shell timeout.
- Configured output limits.
- Configured command allowlist and blocklist.
- Process resource limits.

The eval runner sets the context shell flag for verification, but the command
must still pass command policy.

Example:

```json
{
  "success_command":"ctest --test-dir build --output-on-failure"
}
```

## 10. Output

Human output:

```text
PASS explain_readme
FAIL fix_fixture

Score: 1/2
```

JSON output:

```json
{
  "status":"failed",
  "command":"eval",
  "suite":"coding_smoke",
  "passed":1,
  "total":2,
  "results":[
    {"id":"explain_readme","passed":true},
    {"id":"fix_fixture","passed":false}
  ]
}
```

Statuses:

- `success` when every executed case passes.
- `failed` when at least one executed case fails.
- `interrupted` on cancellation.

Exit codes:

- `0` all passed.
- `13` at least one failed or suite-level eval failure.
- `130` interrupted.

## 11. Fail Fast

```bash
aegis eval --suite suite.json --fail-fast
```

Execution stops after the first failed case. JSON `total` reflects cases
actually attempted, not the total number declared in the suite.

## 12. Traces and Temporary Workspaces

Runtime uses the normal state and trace configuration for each case.

If a case runs in a temporary workspace and runtime paths are relative, its
state and trace are created inside that temporary workspace and removed
during cleanup. The current eval command has no `--keep-workspace`,
`--keep-trace`, or report-file option.

For durable traces:

- Run a case without an isolated case workspace, or
- Use an explicitly designed external harness, or
- Extend the eval runner to copy artifacts before cleanup.

## 13. Deterministic Mock Evaluation

The mock provider reads:

```text
AEGIS_MOCK_RESPONSE
AEGIS_MOCK_RESPONSES
```

Single response:

```bash
export AEGIS_MOCK_RESPONSE='{
  "type":"final",
  "message":"README checked"
}'
```

Sequence:

```bash
export AEGIS_MOCK_RESPONSES='[
  {
    "type":"tool_call",
    "tool":"read_file",
    "arguments":{"path":"README.md"}
  },
  {
    "type":"final",
    "message":"README checked"
  }
]'
```

The sequence index is based on assistant history in the current context.

## 14. Recommended Suite Families

### Coding

- Small fixture with one defect.
- Dev mode.
- Deterministic tests.
- Success command validates behavior.
- Original fixture remains unchanged.

### Policy

- Requests a denied tool.
- Uses `stop_on_policy_denied` as appropriate.
- Verifies failure status and trace.

### Read-only analysis

- Safe mode.
- Uses list/read only.
- Final output contains an expected fact.

### Provider compatibility

- Local mock HTTP server.
- One tool-call turn followed by final.
- Verify provider-specific request and response conversion.

### Cancellation

- Slow local provider.
- Send `SIGINT`.
- Verify exit `130` and stored `cancelled` state.

## 15. Anti-Cheating Guidance

For coding benchmarks:

- Keep verification outside model control where possible.
- Do not allow tests to be modified unless the case says so.
- Compare workspace diff after completion.
- Use independent hidden tests for serious scoring.
- Reject deletion of test infrastructure as a passing solution.
- Preserve the trace before deleting an isolated workspace.

The current runner does not enforce these rules automatically.

## 16. Current Limitations

Not currently supported:

- Per-case profile, provider, model, or max-step overrides.
- Expected policy decision fields.
- Duration, token, tool-call, and approval metrics.
- Structured failure reasons in result objects.
- Report file output.
- Kept isolated workspace.
- Automatic diff artifacts.
- Parallel cases.
- Suite schema version.

These are logical extensions, not behavior users should assume exists.

## 17. Eval Runner Tests

Tests should cover:

- Valid and invalid root shape.
- Missing suite.
- Invalid case ID or task.
- Relative and absolute case workspace.
- Copy failure.
- Mock final pass/fail.
- `expect_final_contains`.
- Success command pass/fail.
- Command policy denial.
- Fail-fast count.
- JSON validity.
- Interruption cleanup.
- No modification of the source fixture.


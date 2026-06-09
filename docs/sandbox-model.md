# Aegis Sandbox Model

## 1. Security Statement

Aegis currently implements a soft POSIX sandbox. It reduces risk, bounds
resources, and makes execution auditable. It is not a kernel-enforced
containment boundary.

The current controls are strongest for native file tools, which all use a
shared canonical path resolver. Subprocess tools have process and command
controls but do not yet have Landlock, mount namespaces, seccomp, or a
container filesystem.

Do not run untrusted model-selected shell commands on a sensitive host merely
because `sandbox.enabled` is true.

## 2. Control Layers

The effective execution controls are:

```text
Config/profile intersection
  -> policy decision
  -> optional human approval
  -> tool-specific permission flags
  -> workspace/path, command, or network checks
  -> process/HTTP/MCP timeout and output bounds
  -> selected POSIX resource limits
  -> trace and state recording
```

No single layer is intended to carry the entire security burden.

## 3. Workspace Resolution for Native File Tools

`aegis_tool_resolve_path()` is used by file, log, path-scoped Git, and patch
operations.

### 3.1 Normalization

The resolver:

1. Canonicalizes the workspace root with `realpath()`.
2. Requires a non-empty relative path.
3. Rejects absolute paths.
4. Removes `.` components.
5. Rejects every `..` component.
6. Rejects hidden path components when hidden files are disabled.
7. Rejects configured blocked paths.
8. Joins the normalized path below the real workspace root.

These restrictions are currently enforced directly by the resolver even
though similarly named config booleans also exist.

### 3.2 Blocked Paths

Blocked entries are matched either as:

- A path prefix when the configured value contains `/`.
- Any complete path segment when it does not.

For example:

```text
blocked ".git/objects"
  rejects ".git/objects"
  rejects ".git/objects/ab/cd"

blocked ".env"
  rejects ".env"
  rejects "service/.env"
```

### 3.3 Symlinks

When `follow_symlinks` is false:

- Every existing path component is inspected with `lstat()`.
- Any symlink component is rejected.
- Existing final paths are canonicalized and checked against the root.
- Create operations may allow a missing final component only after
  canonicalizing and validating the parent.

This prevents a workspace symlink from escaping to another host path.

### 3.4 Hidden Files

When hidden files are disabled, any path segment beginning with `.` is
rejected. Directory listing and recursive search also skip hidden entries.

The `dangerous` preset enables hidden files but retains blocked paths and
workspace confinement.

## 4. Runtime State and Trace Paths

Runtime state and trace paths are resolved separately by
`safe_runtime_path()`.

Rules:

- Relative paths are joined to the workspace.
- `..` components are rejected.
- The resulting lexical path must begin with the workspace.
- Explicit trace paths supplied by an adapter are subject to the same rule.

Unlike native tool path resolution, runtime path resolution does not
canonicalize every existing component or explicitly reject symlinks. The CLI
`init` command rejects symlinks in managed `.aegis` paths, but callers that
construct runtime state paths manually should treat symlinked runtime
directories as unsafe.

## 5. Process Runner

Shell, tests, and Git use the shared POSIX process layer.

The process runner:

- Creates stdin, stdout, and stderr pipes.
- Calls `fork()`.
- Creates a child process group.
- Changes the child current directory to the workspace.
- Rebuilds the environment when whitelist mode is active.
- Applies selected resource limits.
- Executes the command.
- Uses non-blocking pipes and `poll()`.
- Captures stdout and stderr.
- Enforces timeout and output bounds.
- Kills the process group on timeout, overflow, or cancellation.
- Collects an exit status.

### 5.1 Exit behavior

| Condition | Result |
|---|---|
| Normal success | Child exit code `0`, `ok = 1` |
| Normal failure | Child exit code, runtime error |
| Signalled | `128 + signal` |
| Timeout | Exit code `124` |
| Cancellation | Exit code `130` |
| Output overflow | Runtime error and process group termination |

## 6. Resource Limits

When `sandbox.enabled` is true, the child applies:

```text
RLIMIT_CPU    <- sandbox.limits.cpu_time_seconds
RLIMIT_AS     <- sandbox.limits.memory_bytes
RLIMIT_FSIZE  <- sandbox.limits.file_size_bytes
```

The config also contains `process_count`, but the current runner does not
apply `RLIMIT_NPROC`. That limit is user-wide on common systems and can break
unrelated processes or fail unpredictably in shared environments.

The current runner also does not set:

- `RLIMIT_NOFILE`
- `RLIMIT_CORE`
- cgroup limits

## 7. Environment Policy

When `tools.shell.env_policy` is `whitelist`, the child:

1. Copies values for configured allowed environment names.
2. Calls `clearenv()`.
3. Restores only copied values.
4. Adds a minimal default `PATH` when no allowed `PATH` existed.

Typical allowed names include:

```text
PATH
HOME
LANG
LC_ALL
CC
CXX
CFLAGS
CXXFLAGS
```

Provider key variables are not included in the built-in allowlists.

Allowing `HOME` does reveal the host home path to a subprocess. It does not
grant filesystem access by itself, but the current process sandbox also does
not prevent host filesystem reads. Hardened deployments should consider
removing or replacing `HOME`.

## 8. Shell Command Policy

The shell tool first requires:

- `shell` to be effective.
- `context.allow_shell`.
- Approval when configured.

It then rejects:

- Built-in destructive command substrings.
- Configured blocked substrings.
- Shell operators and composition.
- Any command not matching an allowed prefix.

Allowed prefix matching requires the command to equal the entry or continue
with whitespace. An allowed `git status` does not accidentally match
`git statusx`.

The current implementation is case-sensitive and string-based.

## 9. Important Subprocess Filesystem Limitation

`chdir(workspace)` is not filesystem isolation.

The shell command policy validates the command prefix, but it does not
validate every path argument. If `cat` is allowed, a command such as:

```text
cat /some/absolute/path
```

is not automatically confined by the native file resolver. Similar concerns
apply to `find`, test programs, compilers, build scripts, and arbitrary
executables launched by an allowed command.

Therefore:

- File tools have a stronger workspace guarantee than shell tools.
- Approval remains important for shell access.
- Safe mode disables shell.
- A hardened future sandbox needs Landlock, namespaces, a container, or
  command-specific argument validation.

This limitation is one of the most important facts in the Aegis threat model.

## 10. Process Network Limitation

`sandbox.network.enabled` controls Aegis HTTP tool permission through
`context.allow_network`. It does not create a network namespace and does not
block socket syscalls in child processes.

An allowed build, test, or executable may still use the network if the host
allows it. The command allowlist reduces direct network commands in safe
presets, but it is not kernel network isolation.

Use OS-level isolation when subprocess network denial is a hard requirement.

## 11. HTTP Tool Controls

Native HTTP tools have stronger application-level network controls:

- They require `allow_network` and `tools.http.enabled`.
- Schemes must appear in `allowed_schemes`.
- Redirects are disabled.
- TLS peer and host verification are enabled.
- Literal private and special-use addresses are rejected by default.
- Resolved socket addresses are checked again before connect.
- Requests have a timeout.
- Responses have a size bound.
- Cancellation aborts transfer.

Blocked IPv4 categories include:

- Unspecified/current network.
- RFC 1918 private space.
- Carrier-grade NAT.
- Loopback.
- Link-local.
- Documentation and benchmark ranges.
- Multicast and reserved ranges.

Blocked IPv6 categories include:

- Unspecified and loopback.
- IPv4-mapped blocked addresses.
- Link-local and site-local.
- Unique local.
- Documentation.
- Multicast.

This substantially reduces SSRF risk but does not replace a deployment
firewall or egress proxy.

## 12. MCP Execution Controls

### 12.1 stdio

The stdio client:

- Starts the registered command below the workspace.
- Uses a new process group.
- Sends JSON-RPC on stdin.
- Reads bounded line-oriented JSON from combined stdout/stderr.
- Uses the shell timeout.
- Terminates the process after the response.
- Supports cancellation.
- Applies a built-in destructive-command substring check.

It does not currently reuse the full environment filtering and `setrlimit()`
path used by normal shell tools. A third-party MCP command should therefore be
treated as high risk.

### 12.2 HTTP

MCP HTTP uses the native HTTP request controls and a JSON-RPC POST body.
Private-address policy still applies.

## 13. Evaluation Workspace Isolation

When an eval case declares a workspace, the CLI copies it with:

```text
cp -a <source> /tmp/aegis-eval-XXXXXX/workspace
```

It later removes the temporary root with:

```text
rm -rf -- <generated-temp-root>
```

These are trusted CLI implementation operations, not model-selected shell
tools. Suite files should still be treated as trusted local input because
they select source workspaces and success commands.

## 14. Not Currently Implemented

The following hardening mechanisms are represented only as future design
directions:

- seccomp
- Landlock
- Linux namespaces
- cgroups
- chroot or container isolation
- no-new-privileges enforcement
- syscall-level network denial
- per-command filesystem capabilities

Config hardening fields that say these are false should be read literally.

## 15. Recommended Deployment Posture

For analysis of untrusted repositories:

1. Use `safe` mode when read-only native file access is enough.
2. Run Aegis inside a disposable VM or container before enabling shell.
3. Mount only the intended workspace.
4. Do not mount SSH keys, cloud credentials, or host sockets.
5. Disable outbound network at the container or VM layer.
6. Use a low-privilege user.
7. Review trace and state retention.
8. Treat `dangerous` mode as explicit elevated capability, not a convenience
   preset.

## 16. Sandbox Tests

Current and future tests should cover:

- Traversal and absolute path denial.
- Hidden and blocked path behavior.
- Symlink component and final-target escape.
- Missing-leaf creation behavior.
- Process cwd.
- Environment filtering.
- Command operators and blocked patterns.
- Timeout and process-group cleanup.
- Output limits.
- SIGINT cancellation.
- HTTP private IPv4 and IPv6 denial.
- MCP command and response bounds.
- State/trace path symlink behavior.
- Subprocess absolute-path access once a hard filesystem sandbox is added.


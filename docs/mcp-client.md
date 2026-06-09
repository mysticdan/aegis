# Aegis MCP Client

## 1. Purpose

Aegis acts as an MCP client. It can register local stdio servers or HTTP
endpoints, list their tools, call a tool directly from the CLI, and expose a
registered server through the native `mcp_tool`.

MCP is an external capability boundary. An MCP server is not trusted merely
because it speaks JSON-RPC.

## 2. Current Components

```text
src/cli/cli_mcp.c
  Workspace-local server registry and CLI commands

src/tools/mcp/mcp_client.c
  JSON-RPC request creation, stdio process transport, HTTP transport

src/tools/mcp/mcp_tool.c
  Native model-facing tool using registered server names

include/aegis/mcp.h
  Public request API
```

Public API:

```c
AegisStatus aegis_mcp_request(
    const AegisToolContext *context,
    const char *server,
    const char *method,
    const char *params_json,
    AegisToolResult *result
);
```

Here `server` is the resolved command or URL. The model-facing native tool
resolves a registered name before calling this API.

## 3. Enablement

For an MCP call to be available to the model:

```text
config.mcp.enabled == true
AND mcp_tool in config.tools.enabled
AND mcp_tool in active profile tools.requested
AND policy decision is allow or approved
AND config risk for mcp_tool is high
```

Built-in `safe`, balanced `aegis`, and `dev` presets disable MCP. The
`dangerous` preset enables it, but its default `coding_agent` profile does
not request it.

An example effective combination:

```bash
aegis tools info mcp_tool --mode dangerous --profile ops
```

## 4. Workspace Registry

The CLI stores server definitions at:

```text
<workspace>/.aegis/config/mcp.json
```

Initialize the workspace before adding servers:

```bash
aegis init
```

Registry shape:

```json
{
  "servers": {
    "local_example": {
      "transport": "stdio",
      "command": "./bin/example-mcp-server"
    },
    "remote_example": {
      "transport": "http",
      "url": "https://mcp.example.test/rpc"
    }
  }
}
```

Server names may contain letters, digits, `_`, and `-`.

The registry file is written through a temporary file, `fsync()`, permission
`0644`, and atomic rename.

## 5. CLI Commands

### List registrations

```bash
aegis mcp list
aegis mcp list --json
```

If no registry file exists, list behaves as an empty in-memory registry.

### Add stdio

```bash
aegis mcp add local_example \
  --cmd './bin/example-mcp-server'
```

### Add HTTP

```bash
aegis mcp add remote_example \
  --url 'https://mcp.example.test/rpc'
```

Exactly one of `--cmd` and `--url` is required.

### Remove

```bash
aegis mcp remove local_example
```

### List tools from all registered servers

```bash
aegis mcp tools --mode dangerous --profile ops
```

This invokes `tools/list` on each server for which an endpoint can be read.

### Call one tool

```bash
aegis mcp call local_example/read_resource \
  --args '{"path":"README.md"}' \
  --mode dangerous \
  --profile ops
```

The qualified name must use:

```text
server-name/tool-name
```

## 6. Model-Facing Tool

The native tool schema is:

```json
{
  "type": "object",
  "required": ["server", "tool"],
  "properties": {
    "server": {"type":"string","minLength":1},
    "tool": {"type":"string","minLength":1},
    "arguments": {"type":"object"}
  },
  "additionalProperties": false
}
```

Example action:

```json
{
  "type": "tool_call",
  "tool": "mcp_tool",
  "arguments": {
    "server": "local_example",
    "tool": "read_resource",
    "arguments": {"path":"README.md"}
  }
}
```

Security properties:

- The model supplies a server name, not a raw command or URL.
- The registry must be a real regular file.
- Registry symlinks are rejected.
- The canonical registry path must remain below the workspace.
- Only `stdio` and `http` transport strings are accepted by the tool bridge.

## 7. JSON-RPC Requests

Normal requests use:

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "tools/list",
  "params": {}
}
```

Tool calls use:

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "tools/call",
  "params": {
    "name": "read_resource",
    "arguments": {"path":"README.md"}
  }
}
```

The current client expects the requested response to carry numeric ID `2`.

## 8. stdio Transport

For each request, Aegis:

1. Creates pipes.
2. Forks a process group.
3. Changes cwd to the workspace.
4. Starts `/bin/sh -c <registered-command>`.
5. Writes three newline-delimited messages:
   - `initialize` with ID `1`
   - `notifications/initialized`
   - The requested method with ID `2`
6. Reads line-oriented output.
7. Returns the first JSON response with ID `2`.
8. Terminates the process.

Initialization currently advertises:

```json
{
  "protocolVersion":"2025-06-18",
  "capabilities":{},
  "clientInfo":{"name":"aegis","version":"1.0.0"}
}
```

### Current protocol limitation

The messages are sent as one batch. The client does not wait for and validate
the initialize response before sending `notifications/initialized` and the
actual request. This works with simple servers and test fixtures but is not a
complete lifecycle implementation.

### Output handling

- stdout and stderr are currently merged into one protocol stream.
- Responses are bounded by `max_output_bytes`.
- The shell timeout is used as the MCP timeout.
- Cancellation terminates the process group.
- The server is not reused between calls.

MCP servers should write protocol JSON only to stdout. A diagnostic line on
stderr can currently corrupt the merged stream.

## 9. stdio Command Security

The registered stdio command is chosen by the local user through `mcp add`,
not by the model.

Before execution, Aegis applies the built-in destructive-command substring
check. It does not apply the full shell command allowlist and does not reject
all shell composition operators for MCP stdio commands.

The MCP stdio path also does not currently reuse the normal process runner's
environment whitelist and `setrlimit()` setup.

Consequences:

- Treat registry write access as code-execution authority.
- Do not import untrusted `mcp.json`.
- Prefer a direct executable path with simple fixed arguments.
- Run third-party servers in an external container or sandbox.

## 10. HTTP Transport

HTTP transport sends the requested JSON-RPC object as one POST request.

The response may be:

- A JSON object in the response body.
- A line beginning with `data:` containing a JSON object.

The normalizer scans for a JSON response with ID `2`.

HTTP transport inherits native HTTP controls:

- Network must be enabled.
- Scheme must be allowed.
- Private and special-use addresses are denied by default.
- Redirects are disabled.
- TLS verification is enabled.
- Response size and timeout are bounded.
- Cancellation is supported.

### Current protocol limitation

The HTTP client does not currently implement:

- Initialize/session negotiation.
- `Mcp-Session-Id` storage.
- Protocol version headers.
- Resumable SSE streams.
- Stateful Streamable HTTP lifecycle.

It is a stateless JSON-RPC POST transport suitable for compatible endpoints,
not a complete MCP Streamable HTTP client.

## 11. Local HTTP Servers

Built-in configs set `http_allow_private_networks` to false. As a result,
`localhost`, loopback, RFC 1918, and other private endpoints are blocked even
in the built-in dangerous preset.

To use a local HTTP MCP server, a trusted local config must explicitly enable
private networks. That change increases SSRF and internal-service risk and
should be narrowly scoped.

## 12. MCP Result Mapping

The selected JSON-RPC response is stored as `AegisToolResult.stdout_data`.

If the response has an `error` object:

- `ok = 0`
- `exit_code = 1`
- Core status is runtime error

Otherwise:

- `ok = 1`
- `exit_code = 0`

The full JSON-RPC result is returned to the model as a tool observation. The
client does not currently normalize MCP content blocks into a smaller native
representation.

## 13. Trust Model

An MCP server may:

- Lie in tool metadata.
- Return prompt injection.
- Read or mutate data outside its declared purpose.
- Exfiltrate data.
- Hang or emit huge output.
- Execute arbitrary code as the local user.
- Return malformed protocol data.

Mitigations currently available:

- Native `mcp_tool` policy and high risk classification.
- Workspace-local server registry.
- Model cannot choose raw endpoints.
- HTTP egress checks.
- Output and time bounds.
- Cancellation.
- Trace recording through normal tool events.

These controls do not make an untrusted local server safe.

## 14. Recommended Configuration

For a trusted local stdio server:

1. Initialize the workspace.
2. Place the server binary in a controlled path.
3. Register a fixed command without shell composition.
4. Use a profile that requests `mcp_tool`.
5. Use a config that explicitly enables MCP.
6. Review `aegis tools info mcp_tool`.
7. Call `aegis mcp tools` manually before model use.
8. Inspect trace results.

For a remote server:

- Require HTTPS.
- Use a dedicated endpoint.
- Avoid sending workspace secrets.
- Prefer a deployment network allowlist outside Aegis.
- Remember that the current registry has no credential/header fields.

## 15. Future MCP Work

High-priority improvements:

- Full initialize handshake validation.
- Persistent server processes and request IDs.
- Separate stdout protocol and stderr diagnostics.
- Normal process sandbox reuse.
- Per-server environment and timeout config.
- Streamable HTTP sessions.
- Authentication/header references through environment variables.
- Capability and protocol-version negotiation.
- Tool metadata import with explicit Aegis risk mapping.
- Roots support limited to the workspace.
- Conformance and malicious-server tests.


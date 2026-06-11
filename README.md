# Aegis

Aegis is a general-purpose, security-conscious agent runtime written in C.
It connects a user-facing adapter to an LLM provider, converts model output
into structured actions, evaluates those actions against configuration and
policy, executes approved tools inside a workspace boundary, and records the
result in durable state and an append-only trace.

The command-line interface is the first adapter, not the architecture itself.
The reusable runtime contract is:

```text
Adapter
  -> AegisMessage
  -> Runtime
  -> Context builder
  -> Provider
  -> Action parser
  -> Policy and approval
  -> Tool registry
  -> Workspace and process controls
  -> State and trace
  -> AegisResponse
  -> Adapter
```

The current source tree identifies itself as Aegis `0.1.0`.

## What Aegis Provides

- A modular CLI with `init`, `run`, `chat`, `resume`, `sessions`, `inspect`,
  `replay`, `eval`, `tools`, `config`, `profiles`, `mcp`, `doctor`,
  `completion`, `version`, and `help`.
- Provider adapters for OpenRouter, OpenAI-compatible APIs, Ollama,
  Anthropic, Gemini, and a deterministic mock provider.
- A strict `json_action_v1` protocol with exactly two actions: `final` and
  `tool_call`.
- Twenty registered tools covering files, shell commands, tests, Git, HTTP,
  adapter interaction, operations, and MCP.
- Config-authoritative policy with per-tool risk and approval decisions.
- Workspace path controls, blocked paths, symlink checks, command allowlists,
  process timeouts, output limits, environment filtering, and selected POSIX
  resource limits.
- SQLite session state and JSONL execution traces.
- Trace replay, inspection, comparison, evaluation suites, and cancellation
  through `Ctrl+C`.

## Security Position

Aegis reduces risk through defense in depth. It is not a perfect isolation
boundary for hostile code.

The current process sandbox is a soft POSIX sandbox. It combines policy,
workspace confinement, environment filtering, command restrictions,
timeouts, output limits, and `setrlimit()` controls. It does not currently
use seccomp, Landlock, namespaces, containers, or cgroups. Read
[Sandbox Model](docs/sandbox-model.md) and
[Threat Model](docs/threat-model.md) before enabling `dangerous` mode.

## Requirements

Aegis currently targets a POSIX environment and uses Linux-oriented process
behavior such as `fork()`, process groups, signals, and `setrlimit()`.

Build requirements:

- CMake 3.20 or newer.
- A compiler with C23 support.
- cJSON development files.
- libcurl development files.
- SQLite 3 development files.
- Python 3 for the CLI and catalog test suites.

Typical Debian or Ubuntu packages:

```bash
sudo apt install build-essential cmake pkg-config \
  libcjson-dev libcurl4-openssl-dev libsqlite3-dev python3
```

Package names differ between operating systems and distributions.

## Quick Install

The recommended way to build and install Aegis:

```bash
./scripts/build.sh
```

This builds the project and installs the binary and resources to `~/.aegis/`.
After installation, add the following to your shell profile (`~/.bashrc`,
`~/.zshrc`, etc.):

```bash
export PATH="$HOME/.aegis/bin:$PATH"
```

Then reload your shell:

```bash
source ~/.bashrc
```

Verify the installation:

```bash
aegis --version
aegis doctor
```

To uninstall:

```bash
./scripts/build.sh --uninstall
```

## Build

### Build Script (recommended)

```bash
./scripts/build.sh                  # Release build
./scripts/build.sh --debug          # Debug build
./scripts/build.sh --sanitize       # Debug with ASan/UBSan
./scripts/build.sh --uninstall      # Remove installation
./scripts/build.sh --help           # Show all options
```

### Manual CMake

Configure and build from the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The executable is created at:

```text
build/aegis
```

The build copies `config/`, `profiles/`, and `prompts/` into:

```text
build/aegis-resources/
```

The binary uses that directory as its built-in resource bundle.

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `AEGIS_BUILD_APP` | `ON` | Build the CLI executable |
| `AEGIS_ENABLE_SANITIZERS` | `OFF` | Enable AddressSanitizer and UBSan |
| `AEGIS_ENABLE_HARDENING` | `ON` | Enable binary hardening flags |

When `AEGIS_ENABLE_HARDENING` is on, the following flags are applied:

```text
Compile: -D_FORTIFY_SOURCE=3 -fstack-protector-strong -fstack-clash-protection
         -fPIE -fno-plt -fno-strict-overflow -fno-delete-null-pointer-checks
         -ftrivial-auto-var-init=zero (GCC 12+ / Clang 16+)
Link:    -pie -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack
         -Wl,-z,separate-code -Wl,-z,text -Wl,--gc-sections -Wl,--as-needed
```

Example with options:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DAEGIS_ENABLE_SANITIZERS=ON \
  -DAEGIS_ENABLE_HARDENING=ON
```

## Test

Run the complete normal test suite:

```bash
ctest --test-dir build --output-on-failure
```

Build and test with AddressSanitizer and UndefinedBehaviorSanitizer:

```bash
cmake -S . -B build-sanitize \
  -DCMAKE_BUILD_TYPE=Debug \
  -DAEGIS_ENABLE_SANITIZERS=ON
cmake --build build-sanitize -j
ctest --test-dir build-sanitize --output-on-failure
```

The CMake targets compile with strict warnings, including `-Wall`, `-Wextra`,
`-Wpedantic`, `-Wconversion`, `-Wshadow`, `-Wstrict-prototypes`,
`-Wmissing-prototypes`, `-Wundef`, `-Wcast-align`, `-Wwrite-strings`,
`-Wformat=2`, `-Werror=implicit-function-declaration`, and
`-Werror=format-security` on GCC and Clang.

## Quick Start

The recommended workflow is to initialize the target workspace first.

```bash
cd /path/to/project
aegis init
```

This creates:

```text
.aegis/
|-- config/
|   |-- aegis.json
|   |-- safe.json
|   |-- dev.json
|   `-- dangerous.json
|-- profiles/
|   |-- minimal_agent.json
|   |-- coding_agent.json
|   |-- security_agent.json
|   |-- ops_agent.json
|   `-- assistant_agent.json
|-- prompts/
|   `-- system_*.md
|-- state/
`-- traces/
    |-- safe/
    |-- dev/
    `-- dangerous/
```

Validate the installation:

```bash
aegis doctor
aegis config check
aegis tools list
```

Perform a dry run before contacting a provider:

```bash
aegis run \
  --dry-run \
  --task "Explain the repository structure"
```

## Configure a Provider

The default config uses OpenRouter and reads its key from the environment:

```bash
export OPENROUTER_API_KEY='...'
```

Provider errors now include detailed messages from the API (authentication
failure, rate limit, quota exceeded, model not found, etc.) so you can
diagnose issues quickly.

Run a task:

```bash
aegis run \
  --workspace /path/to/project \
  --task "Inspect the build and explain why the tests fail"
```

Supported provider selectors:

| Provider | Selector | Credential |
|---|---|---|
| OpenRouter | `openrouter` | `OPENROUTER_API_KEY` |
| OpenAI-compatible | `openai_compat` or `openai` | `OPENAI_API_KEY` by default |
| Ollama | `ollama` | No key required |
| Anthropic | `anthropic` | `ANTHROPIC_API_KEY` |
| Gemini | `gemini` | `GEMINI_API_KEY` |
| Mock | `mock` | No key required |

The provider and model can be overridden for one command:

```bash
aegis run \
  --provider ollama \
  --model qwen2.5-coder \
  --task "Summarize this project"
```

Provider URLs, paths, timeouts, credential environment names, and the model
are defined in the active JSON config. Secrets should remain in environment
variables, never in committed JSON.

For a local provider-free smoke test, select `mock` and provide a valid
action:

```bash
export AEGIS_MOCK_RESPONSE='{"type":"final","message":"Mock run succeeded."}'
aegis run --provider mock --task "Smoke test"
```

## Modes

Use `--mode` to select a preset:

```bash
aegis run --mode safe --task "List the visible files"
aegis run --mode dev --task "Run the tests and fix the failure"
aegis run --mode dangerous --yes --task "Perform the approved task"
```

| Mode | Default profile | Effective intent |
|---|---|---|
| `safe` | `minimal_agent` | Read-only `list_dir` and `read_file` |
| default (no `--mode`) | `coding_agent` | Coding tools with approval for mutation and shell |
| `dev` | `coding_agent` | Workspace mutation allowed, shell still requires approval |
| `dangerous` | `coding_agent` | All tools enabled; critical actions still require approval |

`dangerous` mode prints a warning and requires explicit confirmation. In a
non-interactive process, pass `--yes` deliberately.

## Profiles

Profiles describe agent identity, prompt, limits, requested tools, and
capability metadata:

- `minimal_agent`
- `coding_agent`
- `security_agent`
- `ops_agent`
- `assistant_agent`

Aliases such as `--profile coding`, `minimal`, `security`, `ops`, and
`assistant` are accepted.

Tool access is always the intersection of:

```text
config tools.enabled
AND profile tools.requested
AND policy decision is not deny
AND registry availability is ready
```

A profile cannot grant itself a permission denied by the config.

## Common Workflows

Start an interactive session:

```bash
aegis chat --workspace /path/to/project
```

List and inspect sessions:

```bash
aegis sessions list
aegis sessions show <session-id>
```

Resume a session:

```bash
aegis resume <session-id>
```

Inspect or replay its trace:

```bash
aegis inspect --session <session-id>
aegis replay --trace .aegis/traces/<session-id>.jsonl
```

Inspect tool metadata and execute a controlled manual tool test:

```bash
aegis tools info read_file
aegis tools schema read_file
aegis tools test read_file --args '{"path":"README.md"}'
```

Use `--json` for machine-readable command output:

```bash
aegis doctor --json
aegis tools list --json
```

## Configuration Discovery

For commands that load an environment, Aegis resolves the workspace first:

1. `--workspace`
2. `AEGIS_WORKSPACE`
3. Current directory

It then resolves the active config:

1. `--config`
2. `AEGIS_CONFIG`
3. `<workspace>/.aegis/config/aegis.json`
4. Resource config selected by `AEGIS_RESOURCE_DIR`, or the compiled build
   resource directory

When `--mode safe|dev|dangerous` is used, Aegis first checks the matching
workspace-local preset, then falls back to the built-in resource preset.
`--mode` and `--config` cannot be combined.

## Repository Layout

```text
include/aegis/   Public C contracts
src/cli/         CLI parser and one implementation file per command
src/core/        Runtime, agent, context, state, trace, and shared core logic
src/providers/   Provider-specific request and response adapters
src/tools/       Tool implementations and shared execution controls
src/utils/       Shared utility functions (string helpers)
config/          Built-in config presets
profiles/        Built-in agent profiles
prompts/         Built-in system prompts
tests/           C and Python unit/integration tests
docs/            Design and developer documentation
cmake/           CMake modules (compiler flags, dependencies, hardening)
scripts/         Build, install, and uninstall helper scripts
```

## Documentation

- [Architecture](docs/architecture.md)
- [Roadmap](docs/roadmap.md)
- [CLI Design and Reference](docs/cli-design.md)
- [Adapter Contract](docs/adapter-contract.md)
- [Tool Contract](docs/tool-contract.md)
- [Policy Model](docs/policy-model.md)
- [Sandbox Model](docs/sandbox-model.md)
- [Session and State](docs/session-state.md)
- [Replay and Inspect](docs/replay-inspect.md)
- [Evaluation Design](docs/eval-design.md)
- [MCP Client](docs/mcp-client.md)
- [Threat Model](docs/threat-model.md)

## Current Limitations

- The only user-facing adapter currently implemented is the CLI.
- HTTP, messaging, webhook, and scheduler adapters are design extensions, not
  current binaries or services.
- Streaming model output is not implemented.
- Chat persistence reconstructs compact conversation text rather than loading
  a normalized message table.
- MCP HTTP support is a minimal JSON-RPC POST client, not a complete
  stateful Streamable HTTP implementation.
- The sandbox is not a kernel-level security boundary.
- Trace redaction is useful but cannot guarantee removal of every secret.


These limitations are intentional documentation boundaries. They should not
be hidden behind optimistic wording.
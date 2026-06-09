#!/usr/bin/env python3

import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import threading


ROOT = Path.cwd()
BINARY = Path(sys.argv[1]).resolve()


def run(*args, input_text=None, extra_env=None, cwd=None):
    environment = os.environ.copy()
    for name in (
        "AEGIS_CONFIG",
        "AEGIS_PROFILE",
        "AEGIS_WORKSPACE",
        "AEGIS_STATE_DIR",
        "AEGIS_TRACE_DIR",
        "AEGIS_RESOURCE_DIR",
        "OPENROUTER_API_KEY",
        "AEGIS_MOCK_KEY",
        "AEGIS_MOCK_RESPONSE",
        "AEGIS_MOCK_RESPONSES",
    ):
        environment.pop(name, None)
    if extra_env:
        environment.update(extra_env)

    return subprocess.run(
        [str(BINARY), *args],
        cwd=cwd or ROOT,
        env=environment,
        input=input_text,
        text=True,
        capture_output=True,
        check=False,
    )


def expect_code(result, code):
    assert result.returncode == code, (
        f"expected exit {code}, got {result.returncode}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )


def parse_json(result):
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise AssertionError(
            f"invalid JSON stdout: {error}\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        ) from error


def test_help_and_version():
    result = run("--help")
    expect_code(result, 0)
    assert "Usage:" in result.stdout
    assert "tools" in result.stdout

    result = run("help", "run")
    expect_code(result, 0)
    assert "--task-file" in result.stdout

    result = run("help", "init")
    expect_code(result, 0)
    assert "--force" in result.stdout

    for command in (
        "init",
        "run",
        "chat",
        "resume",
        "sessions",
        "inspect",
        "replay",
        "eval",
        "tools",
        "config",
        "profiles",
        "mcp",
        "doctor",
        "completion",
        "version",
        "help",
    ):
        result = run("help", command)
        expect_code(result, 0)
        assert "Usage:" in result.stdout

    result = run("help", "unknown")
    expect_code(result, 2)
    assert "unknown help command" in result.stderr

    result = run("--version")
    expect_code(result, 0)
    assert result.stdout == "aegis 1.0.0\n"

    result = run("version", "--verbose")
    expect_code(result, 0)
    assert "platform:" in result.stdout

    result = run("version", "--json")
    expect_code(result, 0)
    payload = parse_json(result)
    assert payload["version"] == "1.0.0"
    assert payload["status"] == "success"

    result = run("unknown", "--json")
    expect_code(result, 2)
    payload = parse_json(result)
    assert payload["status"] == "error"
    assert payload["exit_code"] == 2

    result = run()
    expect_code(result, 2)


def tool_by_name(payload, name):
    return next(tool for tool in payload["tools"] if tool["name"] == name)


def test_tools_and_config():
    result = run("tools", "list", "--mode", "safe", "--json")
    expect_code(result, 0)
    payload = parse_json(result)
    assert len(payload["tools"]) == 20
    assert payload["effective_count"] == 2
    assert tool_by_name(payload, "read_file")["effective"] is True
    assert tool_by_name(payload, "write_file")["effective"] is False
    assert tool_by_name(payload, "shell")["availability"] == "ready"

    result = run("tools", "list", "--mode", "dev", "--json")
    expect_code(result, 0)
    payload = parse_json(result)
    assert payload["effective_count"] == 11
    assert tool_by_name(payload, "write_file")["state"] == "enabled"

    result = run("tools", "list", "--profile", "missing", "--json")
    expect_code(result, 4)
    assert parse_json(result)["exit_code"] == 4

    result = run("tools", "list", "--profile", "minimal", "--json")
    expect_code(result, 0)
    assert parse_json(result)["profile"] == "minimal_agent"

    result = run("config", "check", "--json")
    expect_code(result, 0)
    payload = parse_json(result)
    assert payload["status"] == "success"
    assert payload["checks"]["policy"] is True
    assert isinstance(payload["warnings"], list)

    result = run(
        "config",
        "check",
        "--json",
        extra_env={"AEGIS_CONFIG": "config/safe.json"},
    )
    expect_code(result, 0)
    assert parse_json(result)["profile"] == "minimal_agent"

    result = run(
        "config",
        "check",
        "--config",
        "config/dev.json",
        "--json",
        extra_env={"AEGIS_CONFIG": "config/safe.json"},
    )
    expect_code(result, 0)
    assert parse_json(result)["profile"] == "coding_agent"

    result = run("config", "check", "--config", "/tmp/aegis-missing.json")
    expect_code(result, 3)

    result = run(
        "config",
        "check",
        "--mode",
        "safe",
        "--config",
        "config/aegis.json",
    )
    expect_code(result, 2)


def assert_dry_run(result, expected_bytes=None):
    expect_code(result, 0)
    payload = parse_json(result)
    assert payload["status"] == "dry_run"
    assert payload["command"] == "run"
    if expected_bytes is not None:
        assert payload["task_bytes"] == expected_bytes


def test_run_inputs(temporary):
    task_file = temporary / "task.txt"
    task_file.write_text("from file", encoding="utf-8")

    assert_dry_run(
        run("run", "--task", "inline", "--dry-run", "--json"),
        len("inline"),
    )
    assert_dry_run(
        run("run", "positional", "--dry-run", "--json"),
        len("positional"),
    )
    assert_dry_run(
        run(
            "run",
            "--task-file",
            str(task_file),
            "--dry-run",
            "--json",
        ),
        len("from file"),
    )
    assert_dry_run(
        run("run", "-", "--dry-run", "--json", input_text="from stdin"),
        len("from stdin"),
    )

    result = run(
        "run",
        "--task",
        "one",
        "--task-file",
        str(task_file),
        "--dry-run",
    )
    expect_code(result, 2)

    empty = temporary / "empty.txt"
    empty.write_text(" \n\t", encoding="utf-8")
    result = run("run", "--task-file", str(empty), "--dry-run")
    expect_code(result, 2)

    oversized = temporary / "oversized.txt"
    oversized.write_bytes(b"x" * (1024 * 1024 + 1))
    result = run("run", "--task-file", str(oversized), "--dry-run")
    expect_code(result, 2)

    missing_workspace = temporary / "missing-workspace"
    result = run(
        "run",
        "--task",
        "x",
        "--dry-run",
        "--workspace",
        str(missing_workspace),
    )
    expect_code(result, 9)

    result = run(
        "run",
        "--task",
        "x",
        "--dry-run",
        "--max-steps",
        "5",
        "--json",
    )
    assert_dry_run(result)
    assert parse_json(result)["max_steps"] == 5

    result = run(
        "run",
        "--task",
        "x",
        "--dry-run",
        "--max-steps",
        "31",
    )
    expect_code(result, 2)

    result = run(
        "run",
        "--task",
        "x",
        "--provider",
        "mock",
        "--json",
    )
    expect_code(result, 0)
    payload = parse_json(result)
    assert payload["status"] == "success"
    assert payload["final"] == "Mock provider completed the task."


def test_trace_and_eval(temporary):
    trace = temporary / "trace.jsonl"
    trace.write_text(
        '{"schema_version":1,"sequence":1,"timestamp_ms":1,'
        '"session_id":"s_test","step":0,"type":"session_start",'
        '"payload":{"task":"test"}}\n'
        '{"schema_version":1,"sequence":2,"timestamp_ms":2,'
        '"session_id":"s_test","step":1,"type":"final",'
        '"payload":{"message":"ok"}}\n',
        encoding="utf-8",
    )
    invalid_trace = temporary / "invalid.jsonl"
    invalid_trace.write_text("not json\n", encoding="utf-8")
    suite = temporary / "suite.json"
    suite.write_text('{"id":"basic","cases":[]}', encoding="utf-8")

    result = run("replay")
    expect_code(result, 2)

    result = run("replay", "--trace", str(temporary / "missing.jsonl"))
    expect_code(result, 12)

    result = run("replay", "--trace", str(invalid_trace))
    expect_code(result, 12)

    result = run("replay", "--trace", str(trace), "--json")
    expect_code(result, 0)
    assert parse_json(result)["command"] == "replay"

    result = run(
        "inspect",
        "--trace",
        str(trace),
        "--session",
        "session-1",
    )
    expect_code(result, 2)

    result = run("inspect", "--trace", str(trace), "--json")
    expect_code(result, 0)
    assert parse_json(result)["command"] == "inspect"

    result = run("inspect", "--session", "session-1")
    expect_code(result, 11)

    result = run("eval")
    expect_code(result, 2)

    result = run("eval", "--suite", str(suite), "--json")
    expect_code(result, 0)
    assert parse_json(result)["command"] == "eval"


CONFIG_NAMES = ("aegis.json", "safe.json", "dev.json", "dangerous.json")
PROFILE_NAMES = (
    "minimal_agent.json",
    "coding_agent.json",
    "security_agent.json",
    "ops_agent.json",
    "assistant_agent.json",
)
PROMPT_NAMES = (
    "system_minimal_agent.md",
    "system_coding_agent.md",
    "system_security_agent.md",
    "system_ops_agent.md",
    "system_assistant_agent.md",
)


def assert_init_tree(workspace):
    root = workspace / ".aegis"
    for name in CONFIG_NAMES:
        path = root / "config" / name
        assert path.is_file()
        json.loads(path.read_text(encoding="utf-8"))
        assert stat.S_IMODE(path.stat().st_mode) == 0o644
    for name in PROFILE_NAMES:
        path = root / "profiles" / name
        assert path.is_file()
        json.loads(path.read_text(encoding="utf-8"))
        assert stat.S_IMODE(path.stat().st_mode) == 0o644
    for name in PROMPT_NAMES:
        assert (root / "prompts" / name).is_file()
    for relative in (
        "state",
        "traces",
        "traces/safe",
        "traces/dev",
        "traces/dangerous",
    ):
        assert (root / relative).is_dir()
    assert not list(root.rglob("*.tmp.*"))


def active_config(workspace):
    return json.loads(
        (workspace / ".aegis/config/aegis.json").read_text(encoding="utf-8")
    )


def test_init_default(temporary):
    workspace = temporary / "default"
    workspace.mkdir()

    result = run("init", "--workspace", str(workspace), "--json")
    expect_code(result, 0)
    payload = parse_json(result)
    assert payload["status"] == "initialized"
    assert payload["mode"] == "balanced"
    assert payload["profile"] == "coding_agent"
    assert ".aegis/config" in payload["created"]
    assert payload["updated"] == []
    assert_init_tree(workspace)

    config = active_config(workspace)
    assert config["agent"]["profile_directory"] == "profiles"
    assert config["state"]["path"] == ".aegis/state/aegis.db"
    assert config["trace"]["directory"] == ".aegis/traces"
    assert config["logging"]["file"]["path"] == ".aegis/state/aegis.log"

    result = run("config", "check", "--json", cwd=workspace)
    expect_code(result, 0)
    payload = parse_json(result)
    assert payload["path"] == str(workspace / ".aegis/config/aegis.json")
    assert payload["checks"]["state_writable"] is True
    assert payload["checks"]["trace_writable"] is True

    result = run("tools", "list", "--json", cwd=workspace)
    expect_code(result, 0)
    assert parse_json(result)["effective_count"] == 11

    result = run(
        "run",
        "--task",
        "initialized workspace",
        "--dry-run",
        "--json",
        cwd=workspace,
    )
    assert_dry_run(result)
    assert parse_json(result)["workspace"] == str(workspace)

    result = run("init", "--workspace", str(workspace), "--quiet")
    expect_code(result, 0)
    assert result.stdout == "Already initialized.\n"


def test_init_modes_and_profiles(temporary):
    for mode in ("safe", "dev", "dangerous"):
        workspace = temporary / f"mode-{mode}"
        workspace.mkdir()
        result = run(
            "init",
            "--workspace",
            str(workspace),
            "--mode",
            mode,
            "--json",
        )
        expect_code(result, 0)
        payload = parse_json(result)
        assert payload["mode"] == mode
        assert active_config(workspace)["app"]["mode"] == mode

        result = run("tools", "list", "--mode", mode, "--json", cwd=workspace)
        expect_code(result, 0)
        assert str(workspace / f".aegis/config/{mode}.json") == (
            parse_json(result)["config"]
        )

    aliases = {
        "minimal": "minimal_agent",
        "coding": "coding_agent",
        "security": "security_agent",
        "ops": "ops_agent",
        "assistant": "assistant_agent",
    }
    for alias, profile_id in aliases.items():
        workspace = temporary / f"profile-{alias}"
        workspace.mkdir()
        result = run(
            "init",
            "--workspace",
            str(workspace),
            "--profile",
            alias,
            "--json",
        )
        expect_code(result, 0)
        assert parse_json(result)["profile"] == profile_id
        config = active_config(workspace)
        assert config["app"]["default_profile"] == profile_id
        assert config["agent"]["default_profile"] == profile_id

    workspace = temporary / "profile-direct"
    workspace.mkdir()
    result = run(
        "init",
        "--workspace",
        str(workspace),
        "--profile",
        "security_agent",
        "--json",
    )
    expect_code(result, 0)
    assert parse_json(result)["profile"] == "security_agent"

    workspace = temporary / "profile-invalid"
    workspace.mkdir()
    result = run(
        "init",
        "--workspace",
        str(workspace),
        "--profile",
        "missing_agent",
    )
    expect_code(result, 4)
    assert not (workspace / ".aegis").exists()


def test_init_partial_force_and_symlink(temporary):
    partial = temporary / "partial"
    (partial / ".aegis/config").mkdir(parents=True)
    result = run("init", "--workspace", str(partial), "--json")
    expect_code(result, 0)
    assert parse_json(result)["status"] == "initialized"
    assert_init_tree(partial)

    conflict = temporary / "conflict"
    (conflict / ".aegis/config").mkdir(parents=True)
    conflict_file = conflict / ".aegis/config/aegis.json"
    conflict_file.write_text('{"custom":true}\n', encoding="utf-8")
    result = run("init", "--workspace", str(conflict))
    expect_code(result, 3)
    assert conflict_file.read_text(encoding="utf-8") == '{"custom":true}\n'
    assert not (conflict / ".aegis/profiles").exists()

    forced = temporary / "forced"
    forced.mkdir()
    expect_code(run("init", "--workspace", str(forced)), 0)
    state_file = forced / ".aegis/state/user.db"
    state_file.write_text("state", encoding="utf-8")
    foreign_file = forced / ".aegis/custom.txt"
    foreign_file.write_text("foreign", encoding="utf-8")
    prompt = forced / ".aegis/prompts/system_coding_agent.md"
    prompt.write_text("custom prompt", encoding="utf-8")

    result = run(
        "init",
        "--workspace",
        str(forced),
        "--force",
        "--json",
    )
    expect_code(result, 0)
    payload = parse_json(result)
    assert ".aegis/prompts/system_coding_agent.md" in payload["updated"]
    assert "Aegis Coding Agent" in prompt.read_text(encoding="utf-8")
    assert state_file.read_text(encoding="utf-8") == "state"
    assert foreign_file.read_text(encoding="utf-8") == "foreign"

    symlink_workspace = temporary / "symlink"
    symlink_workspace.mkdir()
    (symlink_workspace / "target").mkdir()
    (symlink_workspace / ".aegis").symlink_to(
        symlink_workspace / "target",
        target_is_directory=True,
    )
    result = run("init", "--workspace", str(symlink_workspace))
    expect_code(result, 9)

    missing = temporary / "missing"
    result = run("init", "--workspace", str(missing))
    expect_code(result, 9)


def test_init_resource_override(temporary):
    resource = temporary / "resources"
    for directory in ("config", "profiles", "prompts"):
        shutil.copytree(ROOT / directory, resource / directory)
    config_path = resource / "config/aegis.json"
    config = json.loads(config_path.read_text(encoding="utf-8"))
    config["model"]["profiles"]["default"]["model"] = "test/resource-model"
    config_path.write_text(json.dumps(config), encoding="utf-8")

    workspace = temporary / "override"
    workspace.mkdir()
    result = run(
        "init",
        "--workspace",
        str(workspace),
        "--json",
        extra_env={"AEGIS_RESOURCE_DIR": str(resource)},
    )
    expect_code(result, 0)
    assert (
        active_config(workspace)["model"]["profiles"]["default"]["model"]
        == "test/resource-model"
    )

    broken_resource = temporary / "broken-resource"
    broken_resource.mkdir()
    broken_workspace = temporary / "broken-workspace"
    broken_workspace.mkdir()
    result = run(
        "init",
        "--workspace",
        str(broken_workspace),
        extra_env={"AEGIS_RESOURCE_DIR": str(broken_resource)},
    )
    expect_code(result, 3)
    assert not (broken_workspace / ".aegis").exists()


def test_v1_workflows(temporary):
    workspace = temporary / "v1"
    workspace.mkdir()
    expect_code(run("init", "--workspace", str(workspace)), 0)

    result = run(
        "--workspace",
        str(workspace),
        "--json",
        "run",
        "--task",
        "global options work",
        "--provider",
        "mock",
    )
    expect_code(result, 0)
    payload = parse_json(result)
    session_id = payload["session_id"]
    trace = Path(payload["trace"])
    assert trace.is_file()

    result = run("sessions", "show", session_id, "--json", cwd=workspace)
    expect_code(result, 0)
    assert parse_json(result)["session_status"] == "success"

    result = run("sessions", "list", "--json", cwd=workspace)
    expect_code(result, 0)
    assert any(
        session["id"] == session_id
        for session in parse_json(result)["sessions"]
    )

    result = run("inspect", "--session", session_id, "--json", cwd=workspace)
    expect_code(result, 0)
    assert parse_json(result)["session_id"] == session_id

    result = run(
        "inspect",
        "--trace",
        str(trace),
        "--tools",
        "--json",
        cwd=workspace,
    )
    expect_code(result, 0)
    assert all(
        "tool" in event["type"]
        for event in parse_json(result)["events"]
    )

    result = run("replay", "--trace", str(trace), "--json", cwd=workspace)
    expect_code(result, 0)
    assert len(parse_json(result)["events"]) >= 5

    result = run(
        "replay",
        "--trace",
        str(trace),
        "--mode",
        "compare",
        "--against",
        str(trace),
        "--json",
        cwd=workspace,
    )
    expect_code(result, 0)
    assert parse_json(result)["changed_events"] == 0

    result = run(
        "resume",
        session_id,
        "--provider",
        "mock",
        "--json",
        cwd=workspace,
    )
    expect_code(result, 0)
    assert parse_json(result)["session_id"] == session_id

    result = run("tools", "schema", "read_file", "--json", cwd=workspace)
    expect_code(result, 0)
    assert parse_json(result)["schema"]["type"] == "object"

    result = run("tools", "info", "read_file", "--json", cwd=workspace)
    expect_code(result, 0)
    assert parse_json(result)["availability"] == "ready"

    result = run(
        "tools",
        "test",
        "write_file",
        "--args",
        '{"path":"created.txt","content":"hello"}',
        "--yes",
        "--json",
        cwd=workspace,
    )
    expect_code(result, 0)
    assert (workspace / "created.txt").read_text(encoding="utf-8") == "hello"

    result = run("config", "get", "provider.active", "--json", cwd=workspace)
    expect_code(result, 0)
    assert parse_json(result)["value"] == "openrouter"

    result = run(
        "config",
        "set",
        "provider.active",
        '"mock"',
        "--json",
        cwd=workspace,
    )
    expect_code(result, 0)
    result = run("config", "show", "--json", cwd=workspace)
    expect_code(result, 0)
    assert parse_json(result)["config"]["provider"]["active"] == "mock"

    result = run("profiles", "new", "custom_agent", "--json", cwd=workspace)
    expect_code(result, 0)
    result = run("profiles", "list", "--json", cwd=workspace)
    expect_code(result, 0)
    assert "custom_agent" in parse_json(result)["profiles"]
    result = run("profiles", "show", "custom_agent", "--json", cwd=workspace)
    expect_code(result, 0)
    assert parse_json(result)["definition"]["id"] == "custom_agent"
    result = run(
        "profiles", "validate", "custom_agent", "--json", cwd=workspace
    )
    expect_code(result, 0)
    assert parse_json(result)["valid"] is True

    result = run("doctor", "--json", cwd=workspace)
    expect_code(result, 0)
    assert parse_json(result)["checks"]["sqlite"] is True

    for shell in ("bash", "zsh", "fish"):
        result = run("completion", shell)
        expect_code(result, 0)
        assert result.stdout

    result = run(
        "chat",
        "--provider",
        "mock",
        cwd=workspace,
        input_text="hello\n/trace\n/exit\n",
    )
    expect_code(result, 0)
    assert "Mock provider completed the task." in result.stdout
    assert ".aegis/traces/chat_" in result.stdout

    result = run(
        "run",
        "--task",
        "danger",
        "--mode",
        "dangerous",
        "--provider",
        "mock",
        "--no-input",
        "--json",
        cwd=workspace,
    )
    expect_code(result, 8)

    result = run(
        "run",
        "--task",
        "danger",
        "--mode",
        "dangerous",
        "--provider",
        "mock",
        "--yes",
        "--json",
        cwd=workspace,
    )
    expect_code(result, 0)

    critical_sequence = json.dumps([
        {
            "type": "tool_call",
            "tool": "send_message",
            "arguments": {"message": "do not send"},
        },
        {"type": "final", "message": "unexpected"},
    ])
    result = run(
        "run",
        "--task",
        "critical",
        "--mode",
        "dangerous",
        "--profile",
        "assistant",
        "--provider",
        "mock",
        "--yes",
        "--no-input",
        "--json",
        cwd=workspace,
        extra_env={"AEGIS_MOCK_RESPONSES": critical_sequence},
    )
    expect_code(result, 8)

    eval_suite = workspace / "suite.json"
    eval_suite.write_text(
        json.dumps(
            {
                "id": "mock_suite",
                "cases": [
                    {
                        "id": "final",
                        "task": "finish",
                        "expect_final_contains": "Mock provider",
                    }
                ],
            }
        ),
        encoding="utf-8",
    )
    result = run(
        "eval",
        "--suite",
        str(eval_suite),
        "--workspace-root",
        str(workspace),
        "--json",
        cwd=workspace,
    )
    expect_code(result, 0)
    assert parse_json(result)["passed"] == 1

    result = run("sessions", "delete", session_id, "--json", cwd=workspace)
    expect_code(result, 0)
    assert not trace.exists()


def test_http_and_mcp(temporary):
    workspace = temporary / "network"
    workspace.mkdir()
    expect_code(run("init", "--workspace", str(workspace)), 0)

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            body = b"healthy"
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self):
            length = int(self.headers.get("Content-Length", "0"))
            request = json.loads(self.rfile.read(length))
            body = json.dumps(
                {
                    "jsonrpc": "2.0",
                    "id": request["id"],
                    "result": {
                        "tools": [
                            {
                                "name": "remote_echo",
                                "description": "echo",
                                "inputSchema": {"type": "object"},
                            }
                        ]
                    },
                }
            ).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, format, *args):
            del format, args

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        url = f"http://127.0.0.1:{server.server_port}/health"
        result = run(
            "tools",
            "test",
            "http_get",
            "--mode",
            "dangerous",
            "--profile",
            "ops",
            "--args",
            json.dumps({"url": url}),
            "--json",
            cwd=workspace,
        )
        expect_code(result, 7)

        dangerous = workspace / ".aegis/config/dangerous.json"
        config = json.loads(dangerous.read_text(encoding="utf-8"))
        config["tools"]["http"]["allow_private_networks"] = True
        dangerous.write_text(json.dumps(config), encoding="utf-8")

        result = run(
            "tools",
            "test",
            "http_get",
            "--mode",
            "dangerous",
            "--profile",
            "ops",
            "--args",
            json.dumps({"url": url}),
            "--json",
            cwd=workspace,
        )
        expect_code(result, 0)
        assert parse_json(result)["stdout"] == "healthy"

        result = run(
            "mcp",
            "add",
            "remote",
            "--url",
            f"http://127.0.0.1:{server.server_port}/mcp",
            "--json",
            cwd=workspace,
        )
        expect_code(result, 0)
        result = run(
            "mcp",
            "tools",
            "--mode",
            "dangerous",
            "--profile",
            "ops",
            "--yes",
            "--json",
            cwd=workspace,
        )
        expect_code(result, 0)
        response = parse_json(result)["servers"][0]["response"]
        assert response["result"]["tools"][0]["name"] == "remote_echo"
        result = run(
            "mcp", "remove", "remote", "--json", cwd=workspace
        )
        expect_code(result, 0)
    finally:
        server.shutdown()
        server.server_close()
        thread.join()

    mcp_server = workspace / "mcp_server.py"
    mcp_server.write_text(
        "import json, sys\n"
        "for line in sys.stdin:\n"
        "    request=json.loads(line)\n"
        "    if 'id' not in request: continue\n"
        "    method=request.get('method')\n"
        "    if method == 'initialize': result={'protocolVersion':'2025-06-18','capabilities':{},'serverInfo':{'name':'test','version':'1'}}\n"
        "    elif method == 'tools/list': result={'tools':[{'name':'echo','description':'echo','inputSchema':{'type':'object'}}]}\n"
        "    else: result={'content':[{'type':'text','text':'called'}]}\n"
        "    print(json.dumps({'jsonrpc':'2.0','id':request['id'],'result':result}), flush=True)\n",
        encoding="utf-8",
    )
    result = run(
        "mcp",
        "add",
        "local",
        "--cmd",
        f"{sys.executable} {mcp_server}",
        "--json",
        cwd=workspace,
    )
    expect_code(result, 0)
    assert parse_json(result)["added"] == "local"

    result = run(
        "mcp",
        "tools",
        "--mode",
        "dangerous",
        "--profile",
        "ops",
        "--yes",
        "--json",
        cwd=workspace,
    )
    expect_code(result, 0)
    assert parse_json(result)["servers"][0]["server"] == "local"

    result = run(
        "mcp",
        "call",
        "local/echo",
        "--mode",
        "dangerous",
        "--profile",
        "ops",
        "--yes",
        "--args",
        '{"value":"hello"}',
        "--json",
        cwd=workspace,
    )
    expect_code(result, 0)
    assert parse_json(result)["status"] == "success"

    result = run("mcp", "remove", "local", "--json", cwd=workspace)
    expect_code(result, 0)
    assert parse_json(result)["removed"] == "local"


def test_http_providers(temporary):
    workspace = temporary / "providers"
    workspace.mkdir()
    expect_code(run("init", "--workspace", str(workspace)), 0)
    requests = {}

    class Handler(BaseHTTPRequestHandler):
        def do_POST(self):
            length = int(self.headers.get("Content-Length", "0"))
            body = json.loads(self.rfile.read(length))
            exchange = {
                "body": body,
                "authorization": self.headers.get("Authorization"),
                "api_key": self.headers.get("x-api-key"),
                "gemini_key": self.headers.get("x-goog-api-key"),
                "anthropic_version": self.headers.get("anthropic-version"),
                "referer": self.headers.get("HTTP-Referer"),
            }
            requests.setdefault(self.path, []).append(exchange)
            provider = self.path.strip("/").split("/", 1)[0]
            action = json.dumps(
                {"type": "final", "message": f"{provider} complete"}
            )
            first_response = len(requests[self.path]) == 1
            tool_arguments = {"path": "provider.txt"}
            if provider in ("openrouter", "openai") and first_response:
                response = {
                    "choices": [
                        {
                            "message": {
                                "content": None,
                                "tool_calls": [
                                    {
                                        "type": "function",
                                        "function": {
                                            "name": "read_file",
                                            "arguments": json.dumps(
                                                tool_arguments
                                            ),
                                        },
                                    }
                                ],
                            },
                            "finish_reason": "tool_calls",
                        }
                    ]
                }
            elif provider in ("openrouter", "openai"):
                response = {
                    "choices": [
                        {
                            "message": {"content": action},
                            "finish_reason": "stop",
                        }
                    ],
                    "usage": {
                        "prompt_tokens": 10,
                        "completion_tokens": 5,
                    },
                }
            elif provider == "ollama" and first_response:
                response = {
                    "message": {
                        "content": "",
                        "tool_calls": [
                            {
                                "function": {
                                    "name": "read_file",
                                    "arguments": tool_arguments,
                                }
                            }
                        ],
                    },
                    "done_reason": "stop",
                }
            elif provider == "ollama":
                response = {
                    "message": {"content": action},
                    "done_reason": "stop",
                }
            elif provider == "anthropic" and first_response:
                response = {
                    "content": [
                        {"type": "text", "text": "Checking the file."},
                        {
                            "type": "tool_use",
                            "id": "tool_1",
                            "name": "read_file",
                            "input": tool_arguments,
                        },
                    ],
                    "stop_reason": "tool_use",
                }
            elif provider == "anthropic":
                response = {
                    "content": [{"type": "text", "text": action}],
                    "stop_reason": "end_turn",
                }
            elif first_response:
                response = {
                    "candidates": [
                        {
                            "content": {
                                "parts": [
                                    {"text": "Checking the file."},
                                    {
                                        "functionCall": {
                                            "name": "read_file",
                                            "args": tool_arguments,
                                        }
                                    },
                                ]
                            }
                        }
                    ]
                }
            else:
                response = {
                    "candidates": [
                        {"content": {"parts": [{"text": action}]}}
                    ]
                }
            rendered = json.dumps(response).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(rendered)))
            self.end_headers()
            self.wfile.write(rendered)

        def log_message(self, format, *args):
            del format, args

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        config_path = workspace / ".aegis/config/aegis.json"
        config = json.loads(config_path.read_text(encoding="utf-8"))
        base_url = f"http://127.0.0.1:{server.server_port}"
        providers = config["provider"]["providers"]
        providers["openrouter"]["base_url"] = base_url
        providers["openrouter"]["chat_completions_path"] = "/openrouter"
        providers["openai_compat"]["base_url"] = base_url
        providers["openai_compat"]["chat_completions_path"] = "/openai"
        providers["ollama"]["base_url"] = base_url
        providers["ollama"]["chat_completions_path"] = "/ollama"
        providers["anthropic"]["base_url"] = base_url
        providers["anthropic"]["chat_completions_path"] = "/anthropic"
        providers["gemini"]["base_url"] = base_url
        providers["gemini"]["chat_completions_path"] = (
            "/gemini/{model}:generateContent"
        )
        config["model"]["profiles"]["default"]["model"] = "test-model"
        config_path.write_text(json.dumps(config), encoding="utf-8")
        (workspace / "provider.txt").write_text(
            "provider tool observation", encoding="utf-8"
        )

        provider_cases = (
            ("openrouter", "openrouter complete"),
            ("openai_compat", "openai complete"),
            ("ollama", "ollama complete"),
            ("anthropic", "anthropic complete"),
            ("gemini", "gemini complete"),
        )
        credentials = {
            "OPENROUTER_API_KEY": "openrouter-test-key",
            "OPENAI_API_KEY": "openai-test-key",
            "ANTHROPIC_API_KEY": "anthropic-test-key",
            "GEMINI_API_KEY": "gemini-test-key",
        }
        for provider, expected in provider_cases:
            result = run(
                "run",
                "--task",
                f"test {provider}",
                "--provider",
                provider,
                "--max-steps",
                "2",
                "--json",
                cwd=workspace,
                extra_env=credentials,
            )
            expect_code(result, 0)
            assert parse_json(result)["final"] == expected
    finally:
        server.shutdown()
        server.server_close()
        thread.join()

    assert len(requests["/openrouter"]) == 2
    assert len(requests["/openai"]) == 2
    assert len(requests["/ollama"]) == 2
    assert len(requests["/anthropic"]) == 2
    assert len(requests["/gemini/test-model:generateContent"]) == 2
    assert requests["/openrouter"][0]["authorization"] == (
        "Bearer openrouter-test-key"
    )
    assert requests["/openrouter"][0]["referer"] == "http://localhost"
    assert requests["/openrouter"][0]["body"]["response_format"]["type"] == (
        "json_object"
    )
    assert requests["/openai"][0]["authorization"] == "Bearer openai-test-key"
    assert requests["/ollama"][0]["authorization"] is None
    assert requests["/ollama"][0]["body"]["format"] == "json"
    assert "options" in requests["/ollama"][0]["body"]
    assert requests["/anthropic"][0]["api_key"] == "anthropic-test-key"
    assert requests["/anthropic"][0]["anthropic_version"] == "2023-06-01"
    assert isinstance(requests["/anthropic"][0]["body"]["system"], list)
    gemini_path = "/gemini/test-model:generateContent"
    assert requests[gemini_path][0]["gemini_key"] == "gemini-test-key"
    assert "systemInstruction" in requests[gemini_path][0]["body"]


def test_interrupt(temporary):
    workspace = temporary / "interrupt"
    workspace.mkdir()
    expect_code(run("init", "--workspace", str(workspace)), 0)
    request_started = threading.Event()

    class Handler(BaseHTTPRequestHandler):
        def do_POST(self):
            length = int(self.headers.get("Content-Length", "0"))
            self.rfile.read(length)
            request_started.set()
            threading.Event().wait(5)
            action = json.dumps(
                {"type": "final", "message": "too late"}
            )
            body = json.dumps(
                {
                    "choices": [
                        {
                            "message": {"content": action},
                            "finish_reason": "stop",
                        }
                    ]
                }
            ).encode()
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            try:
                self.wfile.write(body)
            except OSError:
                pass

        def log_message(self, format, *args):
            del format, args

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        config_path = workspace / ".aegis/config/aegis.json"
        config = json.loads(config_path.read_text(encoding="utf-8"))
        provider = config["provider"]["providers"]["openai_compat"]
        provider["base_url"] = f"http://127.0.0.1:{server.server_port}"
        provider["chat_completions_path"] = "/slow"
        config_path.write_text(json.dumps(config), encoding="utf-8")

        environment = os.environ.copy()
        for name in (
            "AEGIS_CONFIG",
            "AEGIS_PROFILE",
            "AEGIS_WORKSPACE",
            "AEGIS_STATE_DIR",
            "AEGIS_TRACE_DIR",
            "AEGIS_RESOURCE_DIR",
            "AEGIS_MOCK_RESPONSE",
            "AEGIS_MOCK_RESPONSES",
        ):
            environment.pop(name, None)
        environment["OPENAI_API_KEY"] = "interrupt-test-key"
        process = subprocess.Popen(
            [
                str(BINARY),
                "run",
                "--task",
                "wait for provider",
                "--provider",
                "openai_compat",
                "--json",
            ],
            cwd=workspace,
            env=environment,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        assert request_started.wait(5), "provider request did not start"
        process.send_signal(signal.SIGINT)
        stdout, stderr = process.communicate(timeout=5)
        assert process.returncode == 130, (
            f"expected interrupt exit 130, got {process.returncode}\n"
            f"stdout:\n{stdout}\nstderr:\n{stderr}"
        )
        assert json.loads(stdout)["exit_code"] == 130

        result = run("sessions", "list", "--json", cwd=workspace)
        expect_code(result, 0)
        assert any(
            session["status"] == "cancelled"
            for session in parse_json(result)["sessions"]
        )
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


def main():
    test_help_and_version()
    test_tools_and_config()
    with tempfile.TemporaryDirectory(prefix="aegis-cli-") as directory:
        temporary = Path(directory)
        test_run_inputs(temporary)
        test_trace_and_eval(temporary)
        test_init_default(temporary)
        test_init_modes_and_profiles(temporary)
        test_init_partial_force_and_symlink(temporary)
        test_init_resource_override(temporary)
        test_v1_workflows(temporary)
        test_http_and_mcp(temporary)
        test_http_providers(temporary)
        test_interrupt(temporary)
    print("aegis cli tests: ok")


if __name__ == "__main__":
    main()

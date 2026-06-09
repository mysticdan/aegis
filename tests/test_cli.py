#!/usr/bin/env python3

import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile


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

    result = run("help", "unknown")
    expect_code(result, 2)
    assert "unknown help command" in result.stderr

    result = run("--version")
    expect_code(result, 0)
    assert result.stdout == "aegis 0.1.0\n"

    result = run("version", "--verbose")
    expect_code(result, 0)
    assert "platform:" in result.stdout

    result = run("version", "--json")
    expect_code(result, 0)
    payload = parse_json(result)
    assert payload["version"] == "0.1.0"
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
    assert tool_by_name(payload, "shell")["availability"] == "stub"

    result = run("tools", "list", "--mode", "dev", "--json")
    expect_code(result, 0)
    payload = parse_json(result)
    assert payload["effective_count"] == 4
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

    result = run("run", "--task", "x", "--json")
    expect_code(result, 1)
    payload = parse_json(result)
    assert payload["exit_code"] == 1
    assert "not implemented" in payload["error"]


def test_trace_and_eval(temporary):
    trace = temporary / "trace.jsonl"
    trace.write_text(
        '{"type":"session_start"}\n{"type":"final","text":"ok"}\n',
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
    expect_code(result, 1)
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
    expect_code(result, 1)
    assert parse_json(result)["command"] == "inspect"

    result = run("inspect", "--session", "session-1")
    expect_code(result, 1)

    result = run("eval")
    expect_code(result, 2)

    result = run("eval", "--suite", str(suite), "--json")
    expect_code(result, 1)
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
    assert parse_json(result)["effective_count"] == 4

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
    print("aegis cli tests: ok")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3

import json
import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[1]
CATALOG = {
    "list_dir",
    "read_file",
    "write_file",
    "append_file",
    "search_file",
    "shell",
    "run_tests",
    "git_status",
    "git_diff",
    "git_log",
    "git_apply_patch",
    "http_get",
    "http_post",
    "ask_user",
    "send_message",
    "reminder",
    "read_log",
    "grep_log",
    "health_check",
    "mcp_tool",
}
READY = CATALOG
RISKS = {
    "list_dir": "low",
    "read_file": "low",
    "write_file": "medium",
    "append_file": "medium",
    "search_file": "low",
    "shell": "high",
    "run_tests": "medium",
    "git_status": "low",
    "git_diff": "low",
    "git_log": "low",
    "git_apply_patch": "medium",
    "http_get": "high",
    "http_post": "critical",
    "ask_user": "low",
    "send_message": "critical",
    "reminder": "medium",
    "read_log": "low",
    "grep_log": "low",
    "health_check": "medium",
    "mcp_tool": "high",
}
PROFILE_TOOLS = {
    "minimal_agent": {"list_dir", "read_file", "search_file"},
    "coding_agent": {
        "list_dir", "read_file", "write_file", "append_file", "search_file",
        "shell", "run_tests", "git_status", "git_diff", "git_log",
        "git_apply_patch",
    },
    "security_agent": {
        "list_dir", "read_file", "write_file", "append_file", "search_file",
        "shell", "run_tests", "git_status", "git_diff", "git_log",
        "git_apply_patch", "http_get", "mcp_tool",
    },
    "ops_agent": {
        "list_dir", "read_file", "write_file", "append_file", "search_file",
        "shell", "run_tests", "git_status", "git_diff", "git_log",
        "read_log", "grep_log", "health_check", "http_get", "http_post",
        "mcp_tool",
    },
    "assistant_agent": {
        "list_dir", "read_file", "search_file", "http_get", "ask_user",
        "send_message", "reminder",
    },
}


def load(path):
    return json.loads(path.read_text(encoding="utf-8"))


header = (ROOT / "include/aegis/tool.h").read_text(encoding="utf-8")
header_names = set(re.findall(r'#define AEGIS_TOOL_[A-Z_]+ "([^"]+)"', header))
assert header_names == CATALOG, (header_names ^ CATALOG)

for path in sorted((ROOT / "config").glob("*.json")):
    config = load(path)
    enabled = set(config["tools"]["enabled"])
    disabled = set(config["tools"]["disabled"])
    assert enabled.isdisjoint(disabled), path
    assert enabled | disabled == CATALOG, path
    assert enabled <= READY, path
    assert set(config["policy"]["decisions"]) == CATALOG, path
    assert set(config["policy"]["risk_levels"]) == CATALOG, path
    assert config["policy"]["risk_levels"] == RISKS, path
    for tool in disabled:
        assert config["policy"]["decisions"][tool] == "deny", (path, tool)

assert set(load(ROOT / "config/safe.json")["tools"]["enabled"]) == {
    "list_dir", "read_file"
}
CODING_TOOLS = {
    "list_dir", "read_file", "write_file", "append_file", "search_file",
    "shell", "run_tests", "git_status", "git_diff", "git_log",
    "git_apply_patch",
}
for preset in ("aegis", "dev"):
    assert set(load(ROOT / f"config/{preset}.json")["tools"]["enabled"]) == (
        CODING_TOOLS
    )
assert set(load(ROOT / "config/dangerous.json")["tools"]["enabled"]) == CATALOG

for profile_id, expected in PROFILE_TOOLS.items():
    profile_path = ROOT / f"profiles/{profile_id}.json"
    profile = load(profile_path)
    requested = set(profile["tools"]["requested"])
    assert requested == expected, profile_path
    prompt = (ROOT / profile["prompt"]["path"]).read_text(encoding="utf-8")
    for tool in requested:
        assert f"`{tool}`" in prompt, (profile_path, tool)

assert '"command"' in (ROOT / "prompts/system_ops_agent.md").read_text()
assert '"url"' in (ROOT / "prompts/system_assistant_agent.md").read_text()

for path in sorted((ROOT / "config").glob("*.json")):
    config = load(path)
    providers = config["provider"]["providers"]
    assert set(providers) == {
        "openrouter", "openai_compat", "ollama",
        "anthropic", "gemini", "mock",
    }
    encoded = path.read_text(encoding="utf-8")
    assert not re.search(
        r'"(?:api_key|token|secret)"\s*:\s*"[^"]+"',
        encoded,
        re.IGNORECASE,
    )

for source in (ROOT / "src/tools").rglob("*.c"):
    text = source.read_text(encoding="utf-8")
    if ".availability" in text:
        assert "AEGIS_TOOL_STUB" not in text, source
print("aegis tool catalog sync: ok")

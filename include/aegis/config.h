#ifndef AEGIS_CONFIG_H
#define AEGIS_CONFIG_H

#include <stddef.h>

#include "aegis/error.h"

#define AEGIS_CONFIG_PATH_MAX 512
#define AEGIS_CONFIG_NAME_MAX 64
#define AEGIS_CONFIG_TEXT_MAX 256
#define AEGIS_CONFIG_URL_MAX 256
#define AEGIS_CONFIG_MAX_TOOLS 32
#define AEGIS_CONFIG_MAX_ACTIONS 8
#define AEGIS_CONFIG_MAX_POLICY_DECISIONS 32

typedef struct {
    size_t count;
    char items[AEGIS_CONFIG_MAX_TOOLS][AEGIS_CONFIG_NAME_MAX];
} AegisConfigStringList;

typedef struct AegisAgentProfile {
    char tool[AEGIS_CONFIG_NAME_MAX];
    char decision[AEGIS_CONFIG_NAME_MAX];
    char risk[AEGIS_CONFIG_NAME_MAX];
} AegisPolicyDecision;

typedef struct {
    char profile_path[AEGIS_CONFIG_PATH_MAX];
    char schema_version[AEGIS_CONFIG_NAME_MAX];
    char kind[AEGIS_CONFIG_NAME_MAX];
    char id[AEGIS_CONFIG_NAME_MAX];
    char name[AEGIS_CONFIG_TEXT_MAX];
    char role[AEGIS_CONFIG_TEXT_MAX];
    char description[AEGIS_CONFIG_TEXT_MAX];

    char prompt_type[AEGIS_CONFIG_NAME_MAX];
    char prompt_path[AEGIS_CONFIG_PATH_MAX];
    char action_protocol[AEGIS_CONFIG_NAME_MAX];
    AegisConfigStringList allowed_actions;
    int force_json_action;
    int invalid_json_repair_attempts;

    double temperature;
    double top_p;
    int max_tokens;

    int max_steps;
    int max_tool_calls;
    int max_context_chars;
    int max_observation_chars;
    int max_history_events;
    int summarize_old_history;

    AegisConfigStringList requested_tools;

    char autonomy[AEGIS_CONFIG_NAME_MAX];
    int can_modify_workspace;
    int can_execute_commands;
    int can_access_network;
    int config_policy_is_authoritative;
    int request_only_needed_tools;
} AegisAgentProfile;

typedef struct AegisConfig {
    char config_path[AEGIS_CONFIG_PATH_MAX];
    char schema_version[AEGIS_CONFIG_NAME_MAX];
    char kind[AEGIS_CONFIG_NAME_MAX];

    char app_name[AEGIS_CONFIG_NAME_MAX];
    char display_name[AEGIS_CONFIG_NAME_MAX];
    char mode[AEGIS_CONFIG_NAME_MAX];
    char default_profile[AEGIS_CONFIG_NAME_MAX];

    int max_steps;
    int max_tool_calls;
    int max_wall_time_ms;
    int stop_on_tool_error;
    int stop_on_policy_denied;
    int retry_enabled;
    int max_retries;
    int retry_backoff_ms;
    int configured_max_steps;
    int configured_max_tool_calls;

    char provider[AEGIS_CONFIG_NAME_MAX];
    char provider_base_url[AEGIS_CONFIG_URL_MAX];
    char chat_completions_path[AEGIS_CONFIG_PATH_MAX];
    char api_key_env[AEGIS_CONFIG_NAME_MAX];
    int provider_timeout_ms;
    int provider_connect_timeout_ms;

    char model_profile[AEGIS_CONFIG_NAME_MAX];
    char model[AEGIS_CONFIG_TEXT_MAX];
    double temperature;
    double top_p;
    int max_tokens;
    int stream;
    double configured_temperature;
    double configured_top_p;
    int configured_max_tokens;

    char profile_directory[AEGIS_CONFIG_PATH_MAX];
    char profile_file_suffix[AEGIS_CONFIG_NAME_MAX];
    char action_protocol[AEGIS_CONFIG_NAME_MAX];
    AegisConfigStringList allowed_actions;
    int force_json_action;
    int invalid_json_repair_attempts;
    char tool_access_strategy[AEGIS_CONFIG_NAME_MAX];
    char policy_authority[AEGIS_CONFIG_NAME_MAX];
    char limit_strategy[AEGIS_CONFIG_NAME_MAX];
    char model_source[AEGIS_CONFIG_NAME_MAX];
    AegisConfigStringList allowed_profile_model_overrides;

    int max_history_events;
    int max_file_read_bytes;
    int max_tool_output_bytes;
    int summarize_old_history;
    int configured_max_history_events;

    char workspace_root[AEGIS_CONFIG_PATH_MAX];
    int restrict_to_root;
    int deny_absolute_paths;
    int deny_parent_traversal;
    int follow_symlinks;
    int allow_hidden_files;
    int max_file_bytes;
    AegisConfigStringList blocked_paths;

    AegisConfigStringList enabled_tools;
    AegisConfigStringList disabled_tools;
    int allow_file_read;
    int allow_file_write;
    int write_requires_approval;
    int shell_enabled;
    int shell_requires_approval;
    int shell_timeout_ms;
    int http_enabled;
    int http_timeout_ms;

    char approval_mode[AEGIS_CONFIG_NAME_MAX];
    char default_decision[AEGIS_CONFIG_NAME_MAX];
    size_t policy_decision_count;
    AegisPolicyDecision
        policy_decisions[AEGIS_CONFIG_MAX_POLICY_DECISIONS];

    int sandbox_enabled;
    int allow_network;
    int cpu_time_seconds;
    long long memory_bytes;
    long long file_size_bytes;
    int process_count;

    int state_enabled;
    char state_path[AEGIS_CONFIG_PATH_MAX];
    int trace_enabled;
    char trace_directory[AEGIS_CONFIG_PATH_MAX];
    int redact_secrets;
    char logging_level[AEGIS_CONFIG_NAME_MAX];
    int mcp_enabled;
    int eval_enabled;
    int max_eval_steps;

    /* Compatibility field derived from tools.shell.enabled. */
    int allow_shell;

    AegisAgentProfile active_profile;
} AegisConfig;

void aegis_agent_profile_defaults(AegisAgentProfile *profile);
void aegis_config_defaults(AegisConfig *cfg);

AegisStatus aegis_agent_profile_load_json(
    const char *path,
    AegisAgentProfile *profile
);
AegisStatus aegis_config_load_json(const char *path, AegisConfig *cfg);

/*
 * Load config/<preset>.json and its default profile. Passing NULL or an empty
 * string selects the "aegis" preset. Preset names may contain only letters,
 * digits, '_' and '-'.
 */
AegisStatus aegis_config_load_preset(
    const char *preset,
    AegisConfig *cfg
);

/*
 * Replace cfg->active_profile with profiles/<profile_id>.json, then apply the
 * configured most-restrictive runtime limits and permitted model overrides.
 */
AegisStatus aegis_config_load_profile(
    AegisConfig *cfg,
    const char *profile_id
);

int aegis_config_tool_enabled(
    const AegisConfig *cfg,
    const char *tool
);
int aegis_config_profile_requests_tool(
    const AegisConfig *cfg,
    const char *tool
);
int aegis_config_tool_is_effective(
    const AegisConfig *cfg,
    const char *tool
);
const char *aegis_config_tool_decision(
    const AegisConfig *cfg,
    const char *tool
);
const char *aegis_config_tool_risk(
    const AegisConfig *cfg,
    const char *tool
);

#endif

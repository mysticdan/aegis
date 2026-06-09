#ifndef AEGIS_CLI_COMMAND_H
#define AEGIS_CLI_COMMAND_H

#include <stdio.h>

#include <cjson/cJSON.h>

#include "aegis/cli.h"

int cli_json_print(cJSON *root);
int cli_error(
    const CliOptions *options,
    int exit_code,
    const char *format,
    ...
);
const char *cli_profile_id(const char *profile);
void cli_environment_clear(CliEnvironment *environment);
int cli_resolve_workspace(
    const CliOptions *options,
    char **workspace,
    char *error,
    size_t error_size
);
int cli_join_path(
    char *destination,
    size_t size,
    const char *left,
    const char *right
);
int cli_load_environment(
    const CliOptions *options,
    CliEnvironment *environment,
    char *error,
    size_t error_size
);
int cli_load_task(
    const CliOptions *options,
    CliTask *task,
    char *error,
    size_t error_size
);
size_t cli_effective_tool_count(const CliEnvironment *environment);
const char *cli_tool_availability(const AegisTool *tool);
const char *cli_tool_state(
    const AegisConfig *config,
    const AegisTool *tool
);
int cli_validate_trace(
    const char *path,
    char *error,
    size_t error_size
);
int cli_load_json_file(
    const char *path,
    char *error,
    size_t error_size
);
int cli_status_exit_code(AegisStatus status);
int cli_confirm_agent_execution(
    const CliOptions *options,
    const AegisConfig *config,
    int dry_run
);
int cli_interrupted(void *userdata);

int aegis_cli_cmd_init(const CliOptions *options);
int aegis_cli_cmd_run(const CliOptions *options);
int aegis_cli_cmd_chat(const CliOptions *options);
int aegis_cli_cmd_resume(const CliOptions *options);
int aegis_cli_cmd_sessions(const CliOptions *options);
int aegis_cli_cmd_inspect(const CliOptions *options);
int aegis_cli_cmd_replay(const CliOptions *options);
int aegis_cli_cmd_eval(const CliOptions *options);
int aegis_cli_cmd_tools(const CliOptions *options);
int aegis_cli_cmd_config(const CliOptions *options);
int aegis_cli_cmd_profiles(const CliOptions *options);
int aegis_cli_cmd_mcp(const CliOptions *options);
int aegis_cli_cmd_doctor(const CliOptions *options);
int aegis_cli_cmd_version(const CliOptions *options);
int aegis_cli_cmd_completion(const CliOptions *options);
int aegis_cli_cmd_help(const CliOptions *options);

#endif

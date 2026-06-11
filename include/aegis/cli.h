#ifndef AEGIS_CLI_H
#define AEGIS_CLI_H

#include <stddef.h>

#include "aegis/command.h"
#include "aegis/config.h"
#include "aegis/tool_registry.h"

#define AEGIS_CLI_VERSION "0.1.0"

#define AEGIS_CLI_TASK_MAX_BYTES (1024U * 1024U)
#define AEGIS_CLI_TRACE_LINE_MAX_BYTES (1024U * 1024U)
#define AEGIS_CLI_ERROR_MAX 512U
#define AEGIS_CLI_MAX_POSITIONALS 4U

typedef enum {
    AEGIS_CLI_EXIT_SUCCESS = 0,
    AEGIS_CLI_EXIT_GENERAL = 1,
    AEGIS_CLI_EXIT_USAGE = 2,
    AEGIS_CLI_EXIT_CONFIG = 3,
    AEGIS_CLI_EXIT_PROFILE = 4,
    AEGIS_CLI_EXIT_PROVIDER = 5,
    AEGIS_CLI_EXIT_TOOL = 6,
    AEGIS_CLI_EXIT_POLICY = 7,
    AEGIS_CLI_EXIT_APPROVAL = 8,
    AEGIS_CLI_EXIT_WORKSPACE = 9,
    AEGIS_CLI_EXIT_MAX_STEPS = 10,
    AEGIS_CLI_EXIT_STATE = 11,
    AEGIS_CLI_EXIT_TRACE = 12,
    AEGIS_CLI_EXIT_EVAL = 13,
    AEGIS_CLI_EXIT_INTERRUPTED = 130
} AegisCliExitCode;

typedef struct {
    AegisCommand command;
    const char *config_path;
    const char *mode;
    const char *profile;
    const char *workspace;
    const char *state_dir;
    const char *provider;
    const char *model;
    const char *task;
    const char *task_file;
    const char *trace;
    const char *session;
    const char *suite;
    const char *args_json;
    const char *against;
    const char *replay_mode;
    const char *older_than;
    const char *command_value;
    const char *url;
    const char *approval;
    const char *positionals[AEGIS_CLI_MAX_POSITIONALS];
    size_t positional_count;
    int max_steps;
    int has_max_steps;
    size_t max_output_bytes;
    int has_max_output_bytes;
    int json;
    int quiet;
    int verbose;
    int dry_run;
    int force;
    int yes;
    int no_input;
    int fail_fast;
    int keep_trace;
    int tools_only;
    int policy_only;
    int step;
    int has_step;
    int from_step;
    int has_from_step;
    int to_step;
    int has_to_step;
    int no_color;
    int help;
} CliOptions;

typedef struct {
    AegisConfig config;
    AegisToolRegistry registry;
    char *workspace;
} CliEnvironment;

typedef struct {
    const char *text;
    char *owned;
    size_t length;
} CliTask;

int aegis_cli_main(int argc, char **argv);

#endif

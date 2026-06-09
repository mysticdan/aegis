#define _XOPEN_SOURCE 700

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#include "aegis/cli.h"
#include "aegis/cli_command.h"
#include "aegis/cli_init.h"

static volatile sig_atomic_t cli_interrupt_requested;

static void cli_signal_handler(int signal_number)
{
    if (signal_number == SIGINT) {
        cli_interrupt_requested = 1;
    }
}

int cli_interrupted(void *userdata)
{
    (void)userdata;
    return cli_interrupt_requested != 0;
}

static void cli_print_main_help(FILE *stream)
{
    fprintf(
        stream,
        "Usage:\n"
        "  aegis <command> [options]\n"
        "\n"
        "Commands:\n"
        "  init      Initialize Aegis in a workspace\n"
        "  run       Validate or run one task\n"
        "  chat      Start an interactive multi-turn session\n"
        "  resume    Resume a stored session\n"
        "  sessions  Manage stored sessions\n"
        "  replay    Replay or compare trace events\n"
        "  inspect   Analyze a trace or stored session\n"
        "  eval      Run an evaluation suite\n"
        "  tools     Inspect the registered tool catalog\n"
        "  config    Validate the active configuration\n"
        "  profiles  Manage agent profiles\n"
        "  mcp       Manage MCP integrations\n"
        "  doctor    Check installation health\n"
        "  completion Generate shell completion\n"
        "  version   Show the Aegis version\n"
        "  help      Show help for a command\n"
        "\n"
        "Global options:\n"
        "  --config <path>       Configuration file\n"
        "  --mode <mode>         safe, dev, or dangerous preset\n"
        "  --profile <name>      Agent profile ID or alias\n"
        "  --workspace <path>    Workspace directory\n"
        "  --json                Emit machine-readable JSON\n"
        "  --quiet               Minimize human-readable output\n"
        "  --verbose             Include diagnostic details\n"
        "  --no-color            Disable terminal colors\n"
        "\n"
        "Run 'aegis help <command>' for command-specific help.\n"
    );
}

static void cli_print_init_help(FILE *stream)
{
    fprintf(
        stream,
        "Usage:\n"
        "  aegis init [--workspace <path>] [options]\n"
        "\n"
        "Options:\n"
        "  --workspace <path>     Existing workspace directory\n"
        "  --mode <mode>          safe, dev, or dangerous active preset\n"
        "  --profile <name>       Active profile ID or alias\n"
        "  --force                Refresh managed template files\n"
        "  --json                 Emit JSON\n"
        "  --quiet                Print only the final status\n"
        "  --verbose              List every created or updated path\n"
    );
}

static void cli_print_run_help(FILE *stream)
{
    fprintf(
        stream,
        "Usage:\n"
        "  aegis run --task <text> [options]\n"
        "  aegis run --task-file <path> [options]\n"
        "  aegis run <task> [options]\n"
        "  aegis run - [options]\n"
        "\n"
        "Options:\n"
        "  --task <text>          Task text\n"
        "  --task-file <path>     Read task from a file\n"
        "  --config <path>        Configuration file\n"
        "  --mode <mode>          safe, dev, or dangerous preset\n"
        "  --profile <name>       Profile ID or alias\n"
        "  --workspace <path>     Existing workspace directory\n"
        "  --max-steps <n>        Tighten the configured step limit\n"
        "  --dry-run              Validate without running an agent\n"
        "  --json                  Emit JSON\n"
        "  --quiet                 Print only the result\n"
        "  --verbose               Include provider/model details\n"
    );
}

static void cli_print_tools_help(FILE *stream)
{
    fprintf(
        stream,
        "Usage:\n"
        "  aegis tools list [options]\n"
        "\n"
        "Lists all 20 registered tools with risk, availability, policy, and\n"
        "effective state for the selected config/profile.\n"
    );
}

static void cli_print_config_help(FILE *stream)
{
    fprintf(
        stream,
        "Usage:\n"
        "  aegis config check [options]\n"
        "\n"
        "Validates the config, active profile, workspace, provider/model, and\n"
        "tool registry synchronization.\n"
    );
}

static void cli_print_trace_help(FILE *stream, const char *command)
{
    if (strcmp(command, "inspect") == 0) {
        fprintf(
            stream,
            "Usage:\n"
            "  aegis inspect --trace <path> [--json]\n"
            "  aegis inspect --session <id> [--json]\n"
            "\n"
            "Exactly one of --trace or --session is required.\n"
        );
    } else {
        fprintf(
            stream,
            "Usage:\n"
            "  aegis replay --trace <path> [--json]\n"
            "  aegis replay <path> [--json]\n"
        );
    }
}

static void cli_print_eval_help(FILE *stream)
{
    fprintf(
        stream,
        "Usage:\n"
        "  aegis eval --suite <path> [--json]\n"
        "\n"
        "Runs each evaluation case in an isolated workspace when requested,\n"
        "then reports pass/fail results and the aggregate score.\n"
    );
}

static int cli_print_command_help(const char *command, FILE *stream)
{
    AegisCommand parsed;

    if (!command) {
        cli_print_main_help(stream);
        return AEGIS_CLI_EXIT_SUCCESS;
    }

    parsed = aegis_command_from_string(command);
    switch (parsed) {
        case AEGIS_CMD_INIT:
            cli_print_init_help(stream);
            return AEGIS_CLI_EXIT_SUCCESS;
        case AEGIS_CMD_RUN:
            cli_print_run_help(stream);
            return AEGIS_CLI_EXIT_SUCCESS;
        case AEGIS_CMD_CHAT:
            fprintf(stream, "Usage:\n  aegis chat [options]\n");
            return AEGIS_CLI_EXIT_SUCCESS;
        case AEGIS_CMD_RESUME:
            fprintf(
                stream,
                "Usage:\n  aegis resume <session-id> [options]\n"
            );
            return AEGIS_CLI_EXIT_SUCCESS;
        case AEGIS_CMD_SESSIONS:
            fprintf(
                stream,
                "Usage:\n  aegis sessions list|show|delete|clean [options]\n"
            );
            return AEGIS_CLI_EXIT_SUCCESS;
        case AEGIS_CMD_REPLAY:
            cli_print_trace_help(stream, "replay");
            return AEGIS_CLI_EXIT_SUCCESS;
        case AEGIS_CMD_INSPECT:
            cli_print_trace_help(stream, "inspect");
            return AEGIS_CLI_EXIT_SUCCESS;
        case AEGIS_CMD_EVAL:
            cli_print_eval_help(stream);
            return AEGIS_CLI_EXIT_SUCCESS;
        case AEGIS_CMD_TOOLS:
            cli_print_tools_help(stream);
            return AEGIS_CLI_EXIT_SUCCESS;
        case AEGIS_CMD_CONFIG:
            cli_print_config_help(stream);
            return AEGIS_CLI_EXIT_SUCCESS;
        case AEGIS_CMD_PROFILES:
            fprintf(
                stream,
                "Usage:\n  aegis profiles list|show|validate|new [options]\n"
            );
            return AEGIS_CLI_EXIT_SUCCESS;
        case AEGIS_CMD_MCP:
            fprintf(
                stream,
                "Usage:\n  aegis mcp list|add|remove|tools|call [options]\n"
            );
            return AEGIS_CLI_EXIT_SUCCESS;
        case AEGIS_CMD_DOCTOR:
            fprintf(stream, "Usage:\n  aegis doctor [options]\n");
            return AEGIS_CLI_EXIT_SUCCESS;
        case AEGIS_CMD_COMPLETION:
            fprintf(
                stream,
                "Usage:\n  aegis completion bash|zsh|fish\n"
            );
            return AEGIS_CLI_EXIT_SUCCESS;
        case AEGIS_CMD_VERSION:
            fprintf(stream, "Usage:\n  aegis version [--verbose] [--json]\n");
            return AEGIS_CLI_EXIT_SUCCESS;
        case AEGIS_CMD_HELP:
            fprintf(stream, "Usage:\n  aegis help [command]\n");
            return AEGIS_CLI_EXIT_SUCCESS;
        default:
            return AEGIS_CLI_EXIT_USAGE;
    }
}

int cli_json_print(cJSON *root)
{
    char *rendered;

    rendered = cJSON_PrintUnformatted(root);
    if (!rendered) {
        return 0;
    }
    puts(rendered);
    cJSON_free(rendered);
    return 1;
}

int cli_error(
    const CliOptions *options,
    int exit_code,
    const char *format,
    ...
)
{
    char message[AEGIS_CLI_ERROR_MAX];
    const char *command;
    va_list arguments;

    va_start(arguments, format);
    vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);

    command = options && options->command != AEGIS_CMD_UNKNOWN
        ? aegis_command_name(options->command)
        : "unknown";

    if (options && options->json) {
        cJSON *root = cJSON_CreateObject();

        if (root) {
            cJSON_AddStringToObject(root, "status", "error");
            cJSON_AddStringToObject(root, "command", command);
            cJSON_AddNumberToObject(root, "exit_code", exit_code);
            cJSON_AddStringToObject(root, "error", message);
            if (!cli_json_print(root)) {
                fprintf(stderr, "error: failed to render JSON error\n");
            }
            cJSON_Delete(root);
        } else {
            fprintf(stderr, "error: %s\n", message);
        }
    } else {
        fprintf(stderr, "error: %s\n", message);
    }
    return exit_code;
}

static int cli_set_value(
    const char **destination,
    int *index,
    int argc,
    char **argv,
    char *error,
    size_t error_size
)
{
    if (*destination) {
        snprintf(error, error_size, "duplicate option: %s", argv[*index]);
        return 0;
    }
    if (*index + 1 >= argc) {
        snprintf(error, error_size, "missing value for %s", argv[*index]);
        return 0;
    }
    *destination = argv[++*index];
    return 1;
}

static int cli_parse_positive_int(
    const char *value,
    int *result
)
{
    char *end;
    long parsed;

    if (!value || value[0] == '\0') {
        return 0;
    }
    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || *end != '\0' || parsed <= 0 || parsed > INT_MAX) {
        return 0;
    }
    *result = (int)parsed;
    return 1;
}

static int cli_parse_positive_size(
    const char *value,
    size_t *result
)
{
    char *end;
    unsigned long long parsed;

    if (!value || value[0] == '\0' || value[0] == '-') {
        return 0;
    }
    errno = 0;
    parsed = strtoull(value, &end, 10);
    if (errno != 0 || *end != '\0' || parsed == 0U ||
        parsed > (unsigned long long)SIZE_MAX) {
        return 0;
    }
    *result = (size_t)parsed;
    return 1;
}

static int cli_parse_options(
    int argc,
    char **argv,
    int command_index,
    CliOptions *options,
    char *error,
    size_t error_size
)
{
    int positional_only = 0;
    int index;

    for (index = 1; index < argc; ++index) {
        const char *argument = argv[index];

        if (index == command_index) {
            continue;
        }
        if (!positional_only && strcmp(argument, "--") == 0) {
            positional_only = 1;
            continue;
        }
        if (!positional_only &&
            (strcmp(argument, "--help") == 0 ||
             strcmp(argument, "-h") == 0)) {
            options->help = 1;
        } else if (!positional_only && strcmp(argument, "--json") == 0) {
            options->json = 1;
        } else if (!positional_only && strcmp(argument, "--quiet") == 0) {
            options->quiet = 1;
        } else if (!positional_only && strcmp(argument, "--verbose") == 0) {
            options->verbose = 1;
        } else if (!positional_only && strcmp(argument, "--dry-run") == 0) {
            options->dry_run = 1;
        } else if (!positional_only && strcmp(argument, "--force") == 0) {
            options->force = 1;
        } else if (!positional_only && strcmp(argument, "--yes") == 0) {
            options->yes = 1;
        } else if (!positional_only && strcmp(argument, "--no-input") == 0) {
            options->no_input = 1;
        } else if (!positional_only && strcmp(argument, "--fail-fast") == 0) {
            options->fail_fast = 1;
        } else if (!positional_only && strcmp(argument, "--keep-trace") == 0) {
            options->keep_trace = 1;
        } else if (!positional_only && strcmp(argument, "--tools") == 0) {
            options->tools_only = 1;
        } else if (!positional_only && strcmp(argument, "--policy") == 0) {
            options->policy_only = 1;
        } else if (!positional_only && strcmp(argument, "--no-color") == 0) {
            options->no_color = 1;
        } else if (!positional_only && strcmp(argument, "--config") == 0) {
            if (!cli_set_value(
                    &options->config_path,
                    &index,
                    argc,
                    argv,
                    error,
                    error_size)) {
                return 0;
            }
        } else if (!positional_only && strcmp(argument, "--mode") == 0) {
            if (!cli_set_value(
                    &options->mode,
                    &index,
                    argc,
                    argv,
                    error,
                    error_size)) {
                return 0;
            }
        } else if (!positional_only && strcmp(argument, "--profile") == 0) {
            if (!cli_set_value(
                    &options->profile,
                    &index,
                    argc,
                    argv,
                    error,
                    error_size)) {
                return 0;
            }
        } else if (!positional_only &&
                   (strcmp(argument, "--workspace") == 0 ||
                    strcmp(argument, "--workspace-root") == 0)) {
            if (!cli_set_value(
                    &options->workspace,
                    &index,
                    argc,
                    argv,
                    error,
                    error_size)) {
                return 0;
            }
        } else if (!positional_only && strcmp(argument, "--state-dir") == 0) {
            if (!cli_set_value(
                    &options->state_dir,
                    &index,
                    argc,
                    argv,
                    error,
                    error_size)) {
                return 0;
            }
        } else if (!positional_only && strcmp(argument, "--provider") == 0) {
            if (!cli_set_value(
                    &options->provider,
                    &index,
                    argc,
                    argv,
                    error,
                    error_size)) {
                return 0;
            }
        } else if (!positional_only && strcmp(argument, "--model") == 0) {
            if (!cli_set_value(
                    &options->model,
                    &index,
                    argc,
                    argv,
                    error,
                    error_size)) {
                return 0;
            }
        } else if (!positional_only && strcmp(argument, "--task") == 0) {
            if (!cli_set_value(
                    &options->task,
                    &index,
                    argc,
                    argv,
                    error,
                    error_size)) {
                return 0;
            }
        } else if (!positional_only && strcmp(argument, "--task-file") == 0) {
            if (!cli_set_value(
                    &options->task_file,
                    &index,
                    argc,
                    argv,
                    error,
                    error_size)) {
                return 0;
            }
        } else if (!positional_only && strcmp(argument, "--trace") == 0) {
            if (!cli_set_value(
                    &options->trace,
                    &index,
                    argc,
                    argv,
                    error,
                    error_size)) {
                return 0;
            }
        } else if (!positional_only && strcmp(argument, "--session") == 0) {
            if (!cli_set_value(
                    &options->session,
                    &index,
                    argc,
                    argv,
                    error,
                    error_size)) {
                return 0;
            }
        } else if (!positional_only && strcmp(argument, "--suite") == 0) {
            if (!cli_set_value(
                    &options->suite,
                    &index,
                    argc,
                    argv,
                    error,
                    error_size)) {
                return 0;
            }
        } else if (!positional_only && strcmp(argument, "--args") == 0) {
            if (!cli_set_value(
                    &options->args_json, &index, argc, argv,
                    error, error_size)) {
                return 0;
            }
        } else if (!positional_only && strcmp(argument, "--against") == 0) {
            if (!cli_set_value(
                    &options->against, &index, argc, argv,
                    error, error_size)) {
                return 0;
            }
        } else if (!positional_only && strcmp(argument, "--older-than") == 0) {
            if (!cli_set_value(
                    &options->older_than, &index, argc, argv,
                    error, error_size)) {
                return 0;
            }
        } else if (!positional_only && strcmp(argument, "--cmd") == 0) {
            if (!cli_set_value(
                    &options->command_value, &index, argc, argv,
                    error, error_size)) {
                return 0;
            }
        } else if (!positional_only && strcmp(argument, "--url") == 0) {
            if (!cli_set_value(
                    &options->url, &index, argc, argv,
                    error, error_size)) {
                return 0;
            }
        } else if (!positional_only && strcmp(argument, "--approval") == 0) {
            if (!cli_set_value(
                    &options->approval, &index, argc, argv,
                    error, error_size)) {
                return 0;
            }
        } else if (!positional_only &&
                   strcmp(argument, "--replay-mode") == 0) {
            if (!cli_set_value(
                    &options->replay_mode, &index, argc, argv,
                    error, error_size)) {
                return 0;
            }
        } else if (!positional_only &&
                   (strcmp(argument, "--step") == 0 ||
                    strcmp(argument, "--from-step") == 0 ||
                    strcmp(argument, "--to-step") == 0)) {
            int value;

            if (index + 1 >= argc ||
                !cli_parse_positive_int(argv[++index], &value)) {
                snprintf(error, error_size, "invalid step value");
                return 0;
            }
            if (strcmp(argument, "--step") == 0) {
                options->step = value;
                options->has_step = 1;
            } else if (strcmp(argument, "--from-step") == 0) {
                options->from_step = value;
                options->has_from_step = 1;
            } else {
                options->to_step = value;
                options->has_to_step = 1;
            }
        } else if (!positional_only && strcmp(argument, "--max-steps") == 0) {
            const char *value;

            if (options->has_max_steps) {
                snprintf(error, error_size, "duplicate option: --max-steps");
                return 0;
            }
            if (index + 1 >= argc) {
                snprintf(error, error_size, "missing value for --max-steps");
                return 0;
            }
            value = argv[++index];
            if (!cli_parse_positive_int(value, &options->max_steps)) {
                snprintf(
                    error,
                    error_size,
                    "invalid --max-steps value: %s",
                    value
                );
                return 0;
            }
            options->has_max_steps = 1;
        } else if (!positional_only &&
                   strcmp(argument, "--max-output-bytes") == 0) {
            const char *value;

            if (options->has_max_output_bytes) {
                snprintf(
                    error,
                    error_size,
                    "duplicate option: --max-output-bytes"
                );
                return 0;
            }
            if (index + 1 >= argc) {
                snprintf(
                    error,
                    error_size,
                    "missing value for --max-output-bytes"
                );
                return 0;
            }
            value = argv[++index];
            if (!cli_parse_positive_size(
                    value,
                    &options->max_output_bytes)) {
                snprintf(
                    error,
                    error_size,
                    "invalid --max-output-bytes value: %s",
                    value
                );
                return 0;
            }
            options->has_max_output_bytes = 1;
        } else if (!positional_only && argument[0] == '-') {
            if (strcmp(argument, "-") == 0) {
                if (options->positional_count >=
                    AEGIS_CLI_MAX_POSITIONALS) {
                    snprintf(error, error_size, "too many positional arguments");
                    return 0;
                }
                options->positionals[options->positional_count++] = argument;
            } else {
                snprintf(error, error_size, "unknown option: %s", argument);
                return 0;
            }
        } else {
            if (options->positional_count >= AEGIS_CLI_MAX_POSITIONALS) {
                snprintf(error, error_size, "too many positional arguments");
                return 0;
            }
            options->positionals[options->positional_count++] = argument;
        }
    }

    if (options->quiet && options->verbose) {
        snprintf(error, error_size, "--quiet and --verbose cannot be combined");
        return 0;
    }
    if (options->mode && options->config_path) {
        snprintf(error, error_size, "--mode and --config cannot be combined");
        return 0;
    }
    return 1;
}

const char *cli_profile_id(const char *profile)
{
    if (!profile) {
        return NULL;
    }
    if (strcmp(profile, "coding") == 0) {
        return "coding_agent";
    }
    if (strcmp(profile, "minimal") == 0) {
        return "minimal_agent";
    }
    if (strcmp(profile, "security") == 0) {
        return "security_agent";
    }
    if (strcmp(profile, "ops") == 0) {
        return "ops_agent";
    }
    if (strcmp(profile, "assistant") == 0) {
        return "assistant_agent";
    }
    return profile;
}

static int cli_valid_mode(const char *mode)
{
    return mode &&
        (strcmp(mode, "safe") == 0 ||
         strcmp(mode, "dev") == 0 ||
         strcmp(mode, "dangerous") == 0);
}

static int cli_apply_approval_override(
    AegisConfig *config,
    const char *mode
)
{
    size_t index;

    if (!mode) {
        return 1;
    }
    if (strcmp(mode, "never") != 0 &&
        strcmp(mode, "on_write") != 0 &&
        strcmp(mode, "on_shell") != 0 &&
        strcmp(mode, "on_risky_action") != 0 &&
        strcmp(mode, "always") != 0) {
        return 0;
    }
    snprintf(
        config->approval_mode,
        sizeof(config->approval_mode),
        "%s",
        mode
    );
    for (index = 0U; index < config->policy_decision_count; ++index) {
        AegisPolicyDecision *policy = &config->policy_decisions[index];
        int require = 0;

        if (strcmp(policy->decision, "allow") != 0) {
            continue;
        }
        if (strcmp(mode, "always") == 0) {
            require = 1;
        } else if (strcmp(mode, "on_risky_action") == 0) {
            require = strcmp(policy->risk, "low") != 0;
        } else if (strcmp(mode, "on_write") == 0) {
            require = strcmp(policy->tool, AEGIS_TOOL_WRITE_FILE) == 0 ||
                strcmp(policy->tool, AEGIS_TOOL_APPEND_FILE) == 0 ||
                strcmp(policy->tool, AEGIS_TOOL_GIT_APPLY_PATCH) == 0;
        } else if (strcmp(mode, "on_shell") == 0) {
            require = strcmp(policy->tool, AEGIS_TOOL_SHELL) == 0 ||
                strcmp(policy->tool, AEGIS_TOOL_RUN_TESTS) == 0;
        }
        if (require) {
            snprintf(
                policy->decision,
                sizeof(policy->decision),
                "require_approval"
            );
        }
    }
    return 1;
}

void cli_environment_clear(CliEnvironment *environment)
{
    if (!environment) {
        return;
    }
    free(environment->workspace);
    memset(environment, 0, sizeof(*environment));
}

int cli_resolve_workspace(
    const CliOptions *options,
    char **workspace,
    char *error,
    size_t error_size
)
{
    const char *environment_workspace = getenv("AEGIS_WORKSPACE");
    const char *selected = options->workspace
        ? options->workspace
        : (environment_workspace && environment_workspace[0] != '\0'
            ? environment_workspace
            : ".");
    struct stat metadata;

    *workspace = realpath(selected, NULL);
    if (!*workspace ||
        stat(*workspace, &metadata) != 0 ||
        !S_ISDIR(metadata.st_mode)) {
        free(*workspace);
        *workspace = NULL;
        snprintf(
            error,
            error_size,
            "workspace does not exist or is not a directory: %s",
            selected
        );
        return 0;
    }
    return 1;
}

int cli_join_path(
    char *destination,
    size_t size,
    const char *left,
    const char *right
)
{
    if (right[0] == '/') {
        int written = snprintf(destination, size, "%s", right);

        return written >= 0 && (size_t)written < size;
    }

    size_t left_length = strlen(left);
    int written = snprintf(
        destination,
        size,
        "%s%s%s",
        left,
        left_length > 0U && left[left_length - 1U] == '/' ? "" : "/",
        right
    );

    return written >= 0 && (size_t)written < size;
}

static int cli_local_config_available(
    const char *path,
    int *available,
    char *error,
    size_t error_size
)
{
    struct stat metadata;

    *available = 0;
    if (lstat(path, &metadata) != 0) {
        if (errno == ENOENT) {
            return 1;
        }
        snprintf(error, error_size, "cannot inspect local config: %s", path);
        return 0;
    }
    if (S_ISLNK(metadata.st_mode) || !S_ISREG(metadata.st_mode)) {
        snprintf(
            error,
            error_size,
            "local config is not a regular file: %s",
            path
        );
        return 0;
    }
    *available = 1;
    return 1;
}

int cli_load_environment(
    const CliOptions *options,
    CliEnvironment *environment,
    char *error,
    size_t error_size
)
{
    const char *config_path;
    const char *profile;
    const char *environment_config;
    const char *resource_root;
    char local_config[AEGIS_CONFIG_PATH_MAX];
    char local_directory[AEGIS_CONFIG_PATH_MAX];
    char resource_config[AEGIS_CONFIG_PATH_MAX];
    char resource_directory[AEGIS_CONFIG_PATH_MAX];
    char config_name[AEGIS_CONFIG_NAME_MAX];
    int local_available;
    AegisStatus status;

    memset(environment, 0, sizeof(*environment));
    if (options->mode && !cli_valid_mode(options->mode)) {
        snprintf(error, error_size, "invalid mode: %s", options->mode);
        return AEGIS_CLI_EXIT_USAGE;
    }
    if (!cli_resolve_workspace(
            options,
            &environment->workspace,
            error,
            error_size)) {
        return AEGIS_CLI_EXIT_WORKSPACE;
    }

    if (options->mode) {
        resource_root = aegis_cli_resource_directory();
        if (snprintf(
                config_name,
                sizeof(config_name),
                "%s.json",
                options->mode) < 0 ||
            strlen(config_name) >= sizeof(config_name) ||
            !cli_join_path(
                local_directory,
                sizeof(local_directory),
                environment->workspace,
                ".aegis/config") ||
            !cli_join_path(
                local_config,
                sizeof(local_config),
                local_directory,
                config_name) ||
            !cli_join_path(
                resource_directory,
                sizeof(resource_directory),
                resource_root,
                "config") ||
            !cli_join_path(
                resource_config,
                sizeof(resource_config),
                resource_directory,
                config_name)) {
            snprintf(error, error_size, "config path is too long");
            return AEGIS_CLI_EXIT_CONFIG;
        }
        if (!cli_local_config_available(
                local_config,
                &local_available,
                error,
                error_size)) {
            return AEGIS_CLI_EXIT_CONFIG;
        }
        config_path = local_available
            ? local_config
            : resource_config;
        status = aegis_config_load_json(config_path, &environment->config);
    } else {
        environment_config = getenv("AEGIS_CONFIG");
        if (options->config_path) {
            config_path = options->config_path;
        } else if (environment_config && environment_config[0] != '\0') {
            config_path = environment_config;
        } else {
            resource_root = aegis_cli_resource_directory();
            if (!cli_join_path(
                    local_config,
                    sizeof(local_config),
                    environment->workspace,
                    ".aegis/config/aegis.json") ||
                !cli_join_path(
                    resource_config,
                    sizeof(resource_config),
                    resource_root,
                    "config/aegis.json")) {
                snprintf(error, error_size, "config path is too long");
                return AEGIS_CLI_EXIT_CONFIG;
            }
            if (!cli_local_config_available(
                    local_config,
                    &local_available,
                    error,
                    error_size)) {
                return AEGIS_CLI_EXIT_CONFIG;
            }
            config_path = local_available
                ? local_config
                : resource_config;
        }
        status = aegis_config_load_json(config_path, &environment->config);
    }
    if (status != AEGIS_OK) {
        snprintf(
            error,
            error_size,
            "failed to load config: %s",
            aegis_status_string(status)
        );
        return AEGIS_CLI_EXIT_CONFIG;
    }

    profile = cli_profile_id(options->profile);
    if (profile) {
        status = aegis_config_load_profile(&environment->config, profile);
        if (status != AEGIS_OK) {
            snprintf(
                error,
                error_size,
                "failed to load profile '%s': %s",
                options->profile,
                aegis_status_string(status)
            );
            return AEGIS_CLI_EXIT_PROFILE;
        }
    }

    aegis_tool_registry_init(&environment->registry);
    status = aegis_tool_registry_register_defaults(&environment->registry);
    if (status != AEGIS_OK) {
        snprintf(error, error_size, "failed to initialize tool registry");
        return AEGIS_CLI_EXIT_CONFIG;
    }
    status = aegis_tool_registry_validate_config(
        &environment->registry,
        &environment->config
    );
    if (status != AEGIS_OK) {
        snprintf(error, error_size, "config and tool registry are inconsistent");
        return AEGIS_CLI_EXIT_CONFIG;
    }

    if (options->has_max_steps) {
        if (options->max_steps > environment->config.max_steps) {
            snprintf(
                error,
                error_size,
                "--max-steps cannot exceed configured limit %d",
                environment->config.max_steps
            );
            return AEGIS_CLI_EXIT_USAGE;
        }
        environment->config.max_steps = options->max_steps;
    }
    if (options->provider) {
        status = aegis_config_select_provider(
            &environment->config,
            options->provider
        );
        if (status != AEGIS_OK) {
            snprintf(
                error,
                error_size,
                "unknown or invalid provider: %s",
                options->provider
            );
            return AEGIS_CLI_EXIT_CONFIG;
        }
    }
    if (options->model) {
        if (options->model[0] == '\0' ||
            strlen(options->model) >= sizeof(environment->config.model)) {
            snprintf(error, error_size, "invalid model override");
            return AEGIS_CLI_EXIT_USAGE;
        }
        memcpy(
            environment->config.model,
            options->model,
            strlen(options->model) + 1U
        );
    }
    if (options->state_dir) {
        int written;

        if (options->state_dir[0] == '\0') {
            snprintf(error, error_size, "invalid state directory override");
            return AEGIS_CLI_EXIT_USAGE;
        }
        written = snprintf(
            environment->config.state_path,
            sizeof(environment->config.state_path),
            "%s%sstate.db",
            options->state_dir,
            options->state_dir[strlen(options->state_dir) - 1U] == '/'
                ? ""
                : "/"
        );
        if (written < 0 ||
            (size_t)written >= sizeof(environment->config.state_path)) {
            snprintf(error, error_size, "state directory path is too long");
            return AEGIS_CLI_EXIT_USAGE;
        }
    }
    if (options->has_max_output_bytes) {
        if (options->max_output_bytes >
            (size_t)environment->config.max_tool_output_bytes) {
            snprintf(
                error,
                error_size,
                "--max-output-bytes cannot exceed configured limit %d",
                environment->config.max_tool_output_bytes
            );
            return AEGIS_CLI_EXIT_USAGE;
        }
        environment->config.max_tool_output_bytes =
            (int)options->max_output_bytes;
    }
    if (!cli_apply_approval_override(
            &environment->config,
            options->approval)) {
        snprintf(
            error,
            error_size,
            "invalid approval mode: %s",
            options->approval
        );
        return AEGIS_CLI_EXIT_USAGE;
    }

    return AEGIS_CLI_EXIT_SUCCESS;
}

static int cli_is_blank(const char *text, size_t length)
{
    size_t index;

    for (index = 0U; index < length; ++index) {
        char value = text[index];

        if (value != ' ' && value != '\t' && value != '\r' && value != '\n') {
            return 0;
        }
    }
    return 1;
}

static int cli_read_stream(
    FILE *stream,
    size_t maximum,
    char **content,
    size_t *length,
    char *error,
    size_t error_size
)
{
    char *buffer;
    size_t capacity = 4096U;
    size_t used = 0U;

    if (capacity > maximum + 1U) {
        capacity = maximum + 1U;
    }
    buffer = malloc(capacity + 1U);
    if (!buffer) {
        snprintf(error, error_size, "out of memory while reading input");
        return 0;
    }

    while (!feof(stream)) {
        size_t available;
        size_t read_count;

        if (used == capacity) {
            size_t expanded;
            char *resized;

            if (capacity >= maximum + 1U) {
                free(buffer);
                snprintf(
                    error,
                    error_size,
                    "task exceeds %u bytes",
                    (unsigned int)maximum
                );
                return 0;
            }
            expanded = capacity * 2U;
            if (expanded > maximum + 1U) {
                expanded = maximum + 1U;
            }
            resized = realloc(buffer, expanded + 1U);
            if (!resized) {
                free(buffer);
                snprintf(error, error_size, "out of memory while reading input");
                return 0;
            }
            buffer = resized;
            capacity = expanded;
        }

        available = capacity - used;
        read_count = fread(buffer + used, 1U, available, stream);
        used += read_count;
        if (ferror(stream)) {
            free(buffer);
            snprintf(error, error_size, "failed to read task input");
            return 0;
        }
    }

    if (used > maximum) {
        free(buffer);
        snprintf(
            error,
            error_size,
            "task exceeds %u bytes",
            (unsigned int)maximum
        );
        return 0;
    }
    if (memchr(buffer, '\0', used)) {
        free(buffer);
        snprintf(error, error_size, "task input contains a NUL byte");
        return 0;
    }
    buffer[used] = '\0';
    if (used == 0U || cli_is_blank(buffer, used)) {
        free(buffer);
        snprintf(error, error_size, "task input is empty");
        return 0;
    }

    *content = buffer;
    *length = used;
    return 1;
}

int cli_load_task(
    const CliOptions *options,
    CliTask *task,
    char *error,
    size_t error_size
)
{
    const char *positional = options->positional_count == 1U
        ? options->positionals[0]
        : NULL;
    size_t source_count = 0U;
    FILE *file;

    memset(task, 0, sizeof(*task));
    source_count += options->task != NULL;
    source_count += options->task_file != NULL;
    source_count += positional != NULL;
    if (options->positional_count > 1U) {
        snprintf(error, error_size, "run accepts only one positional task");
        return 0;
    }
    if (source_count == 0U) {
        snprintf(error, error_size, "missing task");
        return 0;
    }
    if (source_count > 1U) {
        snprintf(error, error_size, "multiple task sources cannot be combined");
        return 0;
    }

    if (options->task) {
        task->length = strlen(options->task);
        if (task->length > AEGIS_CLI_TASK_MAX_BYTES) {
            snprintf(
                error,
                error_size,
                "task exceeds %u bytes",
                (unsigned int)AEGIS_CLI_TASK_MAX_BYTES
            );
            return 0;
        }
        if (task->length == 0U ||
            cli_is_blank(options->task, task->length)) {
            snprintf(error, error_size, "task input is empty");
            return 0;
        }
        task->text = options->task;
        return 1;
    }

    if (positional && strcmp(positional, "-") == 0) {
        if (!cli_read_stream(
                stdin,
                AEGIS_CLI_TASK_MAX_BYTES,
                &task->owned,
                &task->length,
                error,
                error_size)) {
            return 0;
        }
        task->text = task->owned;
        return 1;
    }

    if (positional) {
        task->length = strlen(positional);
        if (task->length > AEGIS_CLI_TASK_MAX_BYTES) {
            snprintf(
                error,
                error_size,
                "task exceeds %u bytes",
                (unsigned int)AEGIS_CLI_TASK_MAX_BYTES
            );
            return 0;
        }
        if (task->length == 0U || cli_is_blank(positional, task->length)) {
            snprintf(error, error_size, "task input is empty");
            return 0;
        }
        task->text = positional;
        return 1;
    }

    file = fopen(options->task_file, "rb");
    if (!file) {
        snprintf(
            error,
            error_size,
            "cannot open task file: %s",
            options->task_file
        );
        return 0;
    }
    if (!cli_read_stream(
            file,
            AEGIS_CLI_TASK_MAX_BYTES,
            &task->owned,
            &task->length,
            error,
            error_size)) {
        fclose(file);
        return 0;
    }
    if (fclose(file) != 0) {
        free(task->owned);
        memset(task, 0, sizeof(*task));
        snprintf(error, error_size, "failed to close task file");
        return 0;
    }
    task->text = task->owned;
    return 1;
}

size_t cli_effective_tool_count(const CliEnvironment *environment)
{
    size_t count = 0U;
    size_t index;

    for (index = 0U; index < environment->registry.count; ++index) {
        const AegisTool *tool = &environment->registry.tools[index];

        if (tool->availability == AEGIS_TOOL_READY &&
            aegis_config_tool_is_effective(
                &environment->config,
                tool->name)) {
            ++count;
        }
    }
    return count;
}

const char *cli_tool_availability(const AegisTool *tool)
{
    return tool->availability == AEGIS_TOOL_READY ? "ready" : "stub";
}

const char *cli_tool_state(
    const AegisConfig *config,
    const AegisTool *tool
)
{
    const char *decision;

    if (tool->availability != AEGIS_TOOL_READY) {
        return "stub";
    }
    if (!aegis_config_tool_enabled(config, tool->name)) {
        return "disabled";
    }
    if (!aegis_config_profile_requests_tool(config, tool->name)) {
        return "not_requested";
    }
    decision = aegis_config_tool_decision(config, tool->name);
    if (!decision || strcmp(decision, "deny") == 0 ||
        strcmp(decision, "deny_unknown") == 0) {
        return "denied";
    }
    if (strcmp(decision, "require_approval") == 0) {
        return "approval";
    }
    return "enabled";
}

int cli_validate_trace(
    const char *path,
    char *error,
    size_t error_size
)
{
    struct stat metadata;
    FILE *file;
    char *line = NULL;
    size_t line_capacity = 0U;
    size_t event_count = 0U;
    ssize_t line_length;

    if (!path || path[0] == '\0') {
        snprintf(error, error_size, "missing trace path");
        return 0;
    }
    if (stat(path, &metadata) != 0 || !S_ISREG(metadata.st_mode)) {
        snprintf(error, error_size, "trace file not found: %s", path);
        return 0;
    }
    file = fopen(path, "rb");
    if (!file) {
        snprintf(error, error_size, "cannot open trace file: %s", path);
        return 0;
    }

    while ((line_length = getline(&line, &line_capacity, file)) >= 0) {
        cJSON *event;

        if ((size_t)line_length > AEGIS_CLI_TRACE_LINE_MAX_BYTES) {
            free(line);
            fclose(file);
            snprintf(error, error_size, "trace contains an oversized event");
            return 0;
        }
        if (cli_is_blank(line, (size_t)line_length)) {
            continue;
        }
        event = cJSON_ParseWithOpts(line, NULL, 1);
        if (!event || !cJSON_IsObject(event)) {
            cJSON_Delete(event);
            free(line);
            fclose(file);
            snprintf(error, error_size, "trace contains invalid JSONL");
            return 0;
        }
        cJSON_Delete(event);
        ++event_count;
    }
    free(line);
    if (ferror(file) || fclose(file) != 0) {
        snprintf(error, error_size, "failed to read trace file");
        return 0;
    }
    if (event_count == 0U) {
        snprintf(error, error_size, "trace contains no events");
        return 0;
    }
    return 1;
}

int cli_load_json_file(
    const char *path,
    char *error,
    size_t error_size
)
{
    FILE *file;
    char *content = NULL;
    size_t length = 0U;
    cJSON *root;

    if (!path || path[0] == '\0') {
        snprintf(error, error_size, "missing JSON path");
        return 0;
    }
    file = fopen(path, "rb");
    if (!file) {
        snprintf(error, error_size, "file not found: %s", path);
        return 0;
    }
    if (!cli_read_stream(
            file,
            AEGIS_CLI_TASK_MAX_BYTES,
            &content,
            &length,
            error,
            error_size)) {
        fclose(file);
        return 0;
    }
    if (fclose(file) != 0) {
        free(content);
        snprintf(error, error_size, "failed to close JSON file");
        return 0;
    }

    root = cJSON_ParseWithOpts(content, NULL, 1);
    free(content);
    if (!root || (!cJSON_IsObject(root) && !cJSON_IsArray(root))) {
        cJSON_Delete(root);
        snprintf(error, error_size, "file is not valid JSON: %s", path);
        return 0;
    }
    cJSON_Delete(root);
    return 1;
}

int cli_status_exit_code(AegisStatus status)
{
    switch (status) {
        case AEGIS_OK: return AEGIS_CLI_EXIT_SUCCESS;
        case AEGIS_ERR_PROVIDER: return AEGIS_CLI_EXIT_PROVIDER;
        case AEGIS_ERR_TOOL: return AEGIS_CLI_EXIT_TOOL;
        case AEGIS_ERR_POLICY_DENIED: return AEGIS_CLI_EXIT_POLICY;
        case AEGIS_ERR_APPROVAL_REJECTED: return AEGIS_CLI_EXIT_APPROVAL;
        case AEGIS_ERR_PATH_ESCAPE: return AEGIS_CLI_EXIT_WORKSPACE;
        case AEGIS_ERR_MAX_STEPS: return AEGIS_CLI_EXIT_MAX_STEPS;
        case AEGIS_ERR_STATE: return AEGIS_CLI_EXIT_STATE;
        case AEGIS_ERR_INTERRUPTED: return AEGIS_CLI_EXIT_INTERRUPTED;
        default: return AEGIS_CLI_EXIT_GENERAL;
    }
}

int cli_confirm_agent_execution(
    const CliOptions *options,
    const AegisConfig *config,
    int dry_run
)
{
    char answer[32];

    if (!config || strcmp(config->mode, "dangerous") != 0) {
        return AEGIS_CLI_EXIT_SUCCESS;
    }
    fprintf(
        stderr,
        "warning: dangerous mode allows elevated tool capabilities\n"
    );
    if (dry_run || options->yes) {
        return AEGIS_CLI_EXIT_SUCCESS;
    }
    if (options->no_input || !isatty(STDIN_FILENO)) {
        return cli_error(
            options,
            AEGIS_CLI_EXIT_APPROVAL,
            "dangerous mode requires explicit confirmation"
        );
    }
    fprintf(stderr, "Continue in dangerous mode? [y/N] ");
    fflush(stderr);
    if (!fgets(answer, sizeof(answer), stdin) ||
        (answer[0] != 'y' && answer[0] != 'Y')) {
        return cli_error(
            options,
            AEGIS_CLI_EXIT_APPROVAL,
            "dangerous mode was not approved"
        );
    }
    return AEGIS_CLI_EXIT_SUCCESS;
}

static int cli_dispatch(const CliOptions *options)
{
    switch (options->command) {
        case AEGIS_CMD_INIT:
            return aegis_cli_cmd_init(options);
        case AEGIS_CMD_RUN:
            return aegis_cli_cmd_run(options);
        case AEGIS_CMD_CHAT:
            return aegis_cli_cmd_chat(options);
        case AEGIS_CMD_RESUME:
            return aegis_cli_cmd_resume(options);
        case AEGIS_CMD_SESSIONS:
            return aegis_cli_cmd_sessions(options);
        case AEGIS_CMD_REPLAY:
            return aegis_cli_cmd_replay(options);
        case AEGIS_CMD_INSPECT:
            return aegis_cli_cmd_inspect(options);
        case AEGIS_CMD_EVAL:
            return aegis_cli_cmd_eval(options);
        case AEGIS_CMD_TOOLS:
            return aegis_cli_cmd_tools(options);
        case AEGIS_CMD_CONFIG:
            return aegis_cli_cmd_config(options);
        case AEGIS_CMD_PROFILES:
            return aegis_cli_cmd_profiles(options);
        case AEGIS_CMD_MCP:
            return aegis_cli_cmd_mcp(options);
        case AEGIS_CMD_DOCTOR:
            return aegis_cli_cmd_doctor(options);
        case AEGIS_CMD_COMPLETION:
            return aegis_cli_cmd_completion(options);
        case AEGIS_CMD_VERSION:
            return aegis_cli_cmd_version(options);
        default:
            return cli_error(
                options,
                AEGIS_CLI_EXIT_USAGE,
                "unsupported command"
            );
    }
}

static int cli_validate_option_scope(
    const CliOptions *options,
    char *error,
    size_t error_size
)
{
#define REQUIRE_COMMAND(condition, message) \
    do { \
        if (condition) { \
            snprintf(error, error_size, "%s", message); \
            return 0; \
        } \
    } while (0)
    REQUIRE_COMMAND(
        (options->task || options->task_file) &&
            options->command != AEGIS_CMD_RUN,
        "--task and --task-file are only valid with run"
    );
    REQUIRE_COMMAND(
        options->suite && options->command != AEGIS_CMD_EVAL,
        "--suite is only valid with eval"
    );
    REQUIRE_COMMAND(
        options->args_json &&
            options->command != AEGIS_CMD_TOOLS &&
            options->command != AEGIS_CMD_MCP,
        "--args is only valid with tools or mcp"
    );
    REQUIRE_COMMAND(
        options->against && options->command != AEGIS_CMD_REPLAY,
        "--against is only valid with replay"
    );
    REQUIRE_COMMAND(
        options->older_than && options->command != AEGIS_CMD_SESSIONS,
        "--older-than is only valid with sessions"
    );
    REQUIRE_COMMAND(
        (options->command_value || options->url) &&
            options->command != AEGIS_CMD_MCP,
        "--cmd and --url are only valid with mcp"
    );
    REQUIRE_COMMAND(
        (options->has_step || options->tools_only ||
         options->policy_only) &&
            options->command != AEGIS_CMD_INSPECT,
        "--step, --tools, and --policy are only valid with inspect"
    );
    REQUIRE_COMMAND(
        (options->has_from_step || options->has_to_step ||
         options->replay_mode) &&
            options->command != AEGIS_CMD_REPLAY,
        "replay range and mode options are only valid with replay"
    );
    REQUIRE_COMMAND(
        options->keep_trace && options->command != AEGIS_CMD_SESSIONS,
        "--keep-trace is only valid with sessions"
    );
    REQUIRE_COMMAND(
        options->fail_fast && options->command != AEGIS_CMD_EVAL,
        "--fail-fast is only valid with eval"
    );
    REQUIRE_COMMAND(
        options->force && options->command != AEGIS_CMD_INIT,
        "--force is only valid with init"
    );
    REQUIRE_COMMAND(
        options->dry_run &&
            options->command != AEGIS_CMD_RUN &&
            options->command != AEGIS_CMD_REPLAY,
        "--dry-run is only valid with run or replay"
    );
#undef REQUIRE_COMMAND
    return 1;
}

int aegis_cli_main(int argc, char **argv)
{
    struct sigaction interrupt_action;
    CliOptions options;
    char error[AEGIS_CLI_ERROR_MAX];
    int command_index = -1;
    int index;

    memset(&interrupt_action, 0, sizeof(interrupt_action));
    interrupt_action.sa_handler = cli_signal_handler;
    sigemptyset(&interrupt_action.sa_mask);
    (void)sigaction(SIGINT, &interrupt_action, NULL);
    cli_interrupt_requested = 0;
    memset(&options, 0, sizeof(options));
    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--json") == 0) {
            options.json = 1;
        }
    }

    if (argc < 2) {
        cli_print_main_help(stderr);
        return AEGIS_CLI_EXIT_USAGE;
    }

    for (index = 1; index < argc; ++index) {
        const char *argument = argv[index];
        int takes_value =
            strcmp(argument, "--config") == 0 ||
            strcmp(argument, "--mode") == 0 ||
            strcmp(argument, "--profile") == 0 ||
            strcmp(argument, "--workspace") == 0 ||
            strcmp(argument, "--workspace-root") == 0 ||
            strcmp(argument, "--state-dir") == 0 ||
            strcmp(argument, "--provider") == 0 ||
            strcmp(argument, "--model") == 0 ||
            strcmp(argument, "--session") == 0 ||
            strcmp(argument, "--trace") == 0 ||
            strcmp(argument, "--approval") == 0 ||
            strcmp(argument, "--max-steps") == 0 ||
            strcmp(argument, "--max-output-bytes") == 0;

        if (strcmp(argument, "--help") == 0 ||
            strcmp(argument, "-h") == 0) {
            cli_print_main_help(stdout);
            return AEGIS_CLI_EXIT_SUCCESS;
        }
        if (strcmp(argument, "--version") == 0 ||
            strcmp(argument, "-v") == 0) {
            options.command = AEGIS_CMD_VERSION;
            command_index = index;
            break;
        }
        if (takes_value) {
            if (++index >= argc) {
                return cli_error(
                    &options,
                    AEGIS_CLI_EXIT_USAGE,
                    "missing value for %s",
                    argument
                );
            }
            continue;
        }
        if (argument[0] != '-') {
            command_index = index;
            break;
        }
        if (strcmp(argument, "--json") != 0 &&
            strcmp(argument, "--quiet") != 0 &&
            strcmp(argument, "--verbose") != 0 &&
            strcmp(argument, "--no-color") != 0 &&
            strcmp(argument, "--dry-run") != 0 &&
            strcmp(argument, "--yes") != 0 &&
            strcmp(argument, "--no-input") != 0) {
            return cli_error(
                &options,
                AEGIS_CLI_EXIT_USAGE,
                "unknown option before command: %s",
                argument
            );
        }
    }
    if (command_index < 0) {
        return cli_error(
            &options,
            AEGIS_CLI_EXIT_USAGE,
            "missing command"
        );
    }

    if (strcmp(argv[command_index], "--version") == 0 ||
        strcmp(argv[command_index], "-v") == 0) {
        options.command = AEGIS_CMD_VERSION;
    } else {
        options.command = aegis_command_from_string(argv[command_index]);
    }
    if (options.command == AEGIS_CMD_UNKNOWN) {
        return cli_error(
            &options,
            AEGIS_CLI_EXIT_USAGE,
            "unknown command: %s",
            argv[command_index]
        );
    }
    if (!cli_parse_options(
            argc,
            argv,
            command_index,
            &options,
            error,
            sizeof(error))) {
        return cli_error(
            &options,
            AEGIS_CLI_EXIT_USAGE,
            "%s",
            error
        );
    }

    if (!cli_validate_option_scope(
            &options,
            error,
            sizeof(error))) {
        return cli_error(
            &options,
            AEGIS_CLI_EXIT_USAGE,
            "%s",
            error
        );
    }
    if (options.command == AEGIS_CMD_HELP) {
        return aegis_cli_cmd_help(&options);
    }
    if (options.help) {
        return cli_print_command_help(
            aegis_command_name(options.command),
            stdout
        );
    }
    return cli_dispatch(&options);
}

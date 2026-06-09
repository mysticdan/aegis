#define _XOPEN_SOURCE 700

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#include "aegis/cli.h"
#include "aegis/command.h"
#include "aegis/config.h"
#include "aegis/error.h"
#include "aegis/tool_registry.h"

#include "cli_init.h"

#define AEGIS_CLI_TASK_MAX_BYTES (1024U * 1024U)
#define AEGIS_CLI_TRACE_LINE_MAX_BYTES (1024U * 1024U)
#define AEGIS_CLI_ERROR_MAX 512U
#define AEGIS_CLI_MAX_POSITIONALS 4U

typedef struct {
    AegisCommand command;
    const char *config_path;
    const char *mode;
    const char *profile;
    const char *workspace;
    const char *task;
    const char *task_file;
    const char *trace;
    const char *session;
    const char *suite;
    const char *positionals[AEGIS_CLI_MAX_POSITIONALS];
    size_t positional_count;
    int max_steps;
    int has_max_steps;
    int json;
    int quiet;
    int verbose;
    int dry_run;
    int force;
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
        "  replay    Validate and replay a trace (backend pending)\n"
        "  inspect   Validate and inspect a trace/session (backend pending)\n"
        "  eval      Validate an evaluation suite (backend pending)\n"
        "  tools     Inspect the registered tool catalog\n"
        "  config    Validate the active configuration\n"
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
        "The suite is validated as JSON before the unavailable eval backend is\n"
        "reported.\n"
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

static int cli_json_print(cJSON *root)
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

static int cli_error(
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

static int cli_parse_options(
    int argc,
    char **argv,
    CliOptions *options,
    char *error,
    size_t error_size
)
{
    int positional_only = 0;
    int index;

    for (index = 2; index < argc; ++index) {
        const char *argument = argv[index];

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
        } else if (!positional_only && strcmp(argument, "--workspace") == 0) {
            if (!cli_set_value(
                    &options->workspace,
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

static const char *cli_profile_id(const char *profile)
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

static void cli_environment_clear(CliEnvironment *environment)
{
    if (!environment) {
        return;
    }
    free(environment->workspace);
    memset(environment, 0, sizeof(*environment));
}

static int cli_resolve_workspace(
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

static int cli_join_path(
    char *destination,
    size_t size,
    const char *left,
    const char *right
)
{
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

static int cli_load_environment(
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

static int cli_load_task(
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

static int cli_cmd_init(const CliOptions *options)
{
    AegisCliInitRequest request;
    AegisCliInitResult result;
    const char *environment_workspace;
    const char *profile_id;
    char error[AEGIS_CLI_ERROR_MAX];
    int exit_code;
    size_t index;

    if (options->positional_count != 0U ||
        options->config_path ||
        options->task ||
        options->task_file ||
        options->trace ||
        options->session ||
        options->suite ||
        options->has_max_steps ||
        options->dry_run) {
        return cli_error(
            options,
            AEGIS_CLI_EXIT_USAGE,
            "usage: aegis init [--workspace <path>] [--mode <mode>] "
            "[--profile <name>] [--force]"
        );
    }
    if (options->mode && !cli_valid_mode(options->mode)) {
        return cli_error(
            options,
            AEGIS_CLI_EXIT_USAGE,
            "invalid mode: %s",
            options->mode
        );
    }

    environment_workspace = getenv("AEGIS_WORKSPACE");
    profile_id = cli_profile_id(options->profile);
    memset(&request, 0, sizeof(request));
    request.workspace = options->workspace
        ? options->workspace
        : (environment_workspace && environment_workspace[0] != '\0'
            ? environment_workspace
            : ".");
    request.mode = options->mode;
    request.profile_id = profile_id;
    request.force = options->force;

    exit_code = aegis_cli_init_execute(
        &request,
        &result,
        error,
        sizeof(error)
    );
    if (exit_code != AEGIS_CLI_EXIT_SUCCESS) {
        return cli_error(options, exit_code, "%s", error);
    }

    if (options->json) {
        cJSON *root = cJSON_CreateObject();
        cJSON *created = cJSON_CreateArray();
        cJSON *updated = cJSON_CreateArray();

        if (!root || !created || !updated) {
            cJSON_Delete(root);
            cJSON_Delete(created);
            cJSON_Delete(updated);
            return cli_error(
                options,
                AEGIS_CLI_EXIT_GENERAL,
                "failed to allocate JSON output"
            );
        }
        cJSON_AddStringToObject(
            root,
            "status",
            result.already_initialized
                ? "already_initialized"
                : "initialized"
        );
        cJSON_AddStringToObject(root, "command", "init");
        cJSON_AddStringToObject(root, "workspace", result.workspace);
        cJSON_AddStringToObject(root, "root", result.root);
        cJSON_AddStringToObject(root, "mode", result.mode);
        cJSON_AddStringToObject(root, "profile", result.profile);
        cJSON_AddItemToObject(root, "created", created);
        cJSON_AddItemToObject(root, "updated", updated);
        for (index = 0U; index < result.created_count; ++index) {
            cJSON_AddItemToArray(
                created,
                cJSON_CreateString(result.created[index])
            );
        }
        for (index = 0U; index < result.updated_count; ++index) {
            cJSON_AddItemToArray(
                updated,
                cJSON_CreateString(result.updated[index])
            );
        }
        if (!cli_json_print(root)) {
            cJSON_Delete(root);
            return cli_error(
                options,
                AEGIS_CLI_EXIT_GENERAL,
                "failed to render JSON output"
            );
        }
        cJSON_Delete(root);
    } else if (options->quiet) {
        puts(
            result.already_initialized
                ? "Already initialized."
                : "Initialized."
        );
    } else {
        puts(
            result.already_initialized
                ? "Aegis workspace already initialized"
                : "Initialized Aegis workspace"
        );
        printf("Workspace : %s\n", result.workspace);
        printf("Root      : %s\n", result.root);
        printf("Config    : %s/config/aegis.json\n", result.root);
        printf("Mode      : %s\n", result.mode);
        printf("Profile   : %s\n", result.profile);
        if (!result.already_initialized) {
            printf("Created   : %zu paths\n", result.created_count);
            printf("Updated   : %zu paths\n", result.updated_count);
        }
        if (options->verbose) {
            for (index = 0U; index < result.created_count; ++index) {
                printf("  created %s\n", result.created[index]);
            }
            for (index = 0U; index < result.updated_count; ++index) {
                printf("  updated %s\n", result.updated[index]);
            }
        }
    }
    return AEGIS_CLI_EXIT_SUCCESS;
}

static size_t cli_effective_tool_count(const CliEnvironment *environment)
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

static const char *cli_tool_availability(const AegisTool *tool)
{
    return tool->availability == AEGIS_TOOL_READY ? "ready" : "stub";
}

static const char *cli_tool_state(
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

static int cli_cmd_tools(const CliOptions *options)
{
    CliEnvironment environment;
    char error[AEGIS_CLI_ERROR_MAX];
    size_t index;
    int exit_code;

    if (options->positional_count != 1U ||
        strcmp(options->positionals[0], "list") != 0) {
        return cli_error(
            options,
            AEGIS_CLI_EXIT_USAGE,
            "usage: aegis tools list [options]"
        );
    }

    exit_code = cli_load_environment(
        options,
        &environment,
        error,
        sizeof(error)
    );
    if (exit_code != AEGIS_CLI_EXIT_SUCCESS) {
        cli_environment_clear(&environment);
        return cli_error(options, exit_code, "%s", error);
    }

    if (options->json) {
        cJSON *root = cJSON_CreateObject();
        cJSON *tools = cJSON_CreateArray();

        if (!root || !tools) {
            cJSON_Delete(root);
            cJSON_Delete(tools);
            cli_environment_clear(&environment);
            return cli_error(
                options,
                AEGIS_CLI_EXIT_GENERAL,
                "failed to allocate JSON output"
            );
        }
        cJSON_AddStringToObject(root, "status", "success");
        cJSON_AddStringToObject(root, "command", "tools");
        cJSON_AddStringToObject(
            root,
            "config",
            environment.config.config_path
        );
        cJSON_AddStringToObject(
            root,
            "profile",
            environment.config.active_profile.id
        );
        cJSON_AddNumberToObject(
            root,
            "effective_count",
            (double)cli_effective_tool_count(&environment)
        );
        cJSON_AddItemToObject(root, "tools", tools);

        for (index = 0U; index < environment.registry.count; ++index) {
            const AegisTool *tool = &environment.registry.tools[index];
            const char *decision = aegis_config_tool_decision(
                &environment.config,
                tool->name
            );
            cJSON *entry = cJSON_CreateObject();

            if (!entry) {
                cJSON_Delete(root);
                cli_environment_clear(&environment);
                return cli_error(
                    options,
                    AEGIS_CLI_EXIT_GENERAL,
                    "failed to allocate JSON output"
                );
            }
            cJSON_AddStringToObject(entry, "name", tool->name);
            cJSON_AddStringToObject(
                entry,
                "risk",
                aegis_tool_risk_name(tool->risk_level)
            );
            cJSON_AddStringToObject(
                entry,
                "availability",
                cli_tool_availability(tool)
            );
            cJSON_AddStringToObject(
                entry,
                "policy",
                decision ? decision : "unknown"
            );
            cJSON_AddStringToObject(
                entry,
                "state",
                cli_tool_state(&environment.config, tool)
            );
            cJSON_AddBoolToObject(
                entry,
                "effective",
                tool->availability == AEGIS_TOOL_READY &&
                    aegis_config_tool_is_effective(
                        &environment.config,
                        tool->name)
            );
            cJSON_AddStringToObject(
                entry,
                "description",
                tool->description
            );
            cJSON_AddItemToArray(tools, entry);
        }
        if (!cli_json_print(root)) {
            cJSON_Delete(root);
            cli_environment_clear(&environment);
            return cli_error(
                options,
                AEGIS_CLI_EXIT_GENERAL,
                "failed to render JSON output"
            );
        }
        cJSON_Delete(root);
    } else if (options->quiet) {
        for (index = 0U; index < environment.registry.count; ++index) {
            const AegisTool *tool = &environment.registry.tools[index];

            if (tool->availability == AEGIS_TOOL_READY &&
                aegis_config_tool_is_effective(
                    &environment.config,
                    tool->name)) {
                puts(tool->name);
            }
        }
    } else {
        printf(
            "%-18s %-9s %-12s %-17s %-14s %s\n",
            "Tool",
            "Risk",
            "Availability",
            "Policy",
            "State",
            "Description"
        );
        for (index = 0U; index < environment.registry.count; ++index) {
            const AegisTool *tool = &environment.registry.tools[index];
            const char *decision = aegis_config_tool_decision(
                &environment.config,
                tool->name
            );

            printf(
                "%-18s %-9s %-12s %-17s %-14s %s\n",
                tool->name,
                aegis_tool_risk_name(tool->risk_level),
                cli_tool_availability(tool),
                decision ? decision : "unknown",
                cli_tool_state(&environment.config, tool),
                tool->description
            );
        }
    }

    cli_environment_clear(&environment);
    return AEGIS_CLI_EXIT_SUCCESS;
}

static int cli_path_writable(
    const char *workspace,
    const char *path,
    int path_is_directory
)
{
    char resolved[AEGIS_CONFIG_PATH_MAX];
    char copy[AEGIS_CONFIG_PATH_MAX];
    char *separator;
    struct stat metadata;
    const char *selected = path;
    const char *directory;
    size_t length;

    if (!path || path[0] == '\0') {
        return 0;
    }
    if (path[0] != '/') {
        if (!cli_join_path(
                resolved,
                sizeof(resolved),
                workspace,
                path)) {
            return 0;
        }
        selected = resolved;
    }
    if (path_is_directory) {
        return stat(selected, &metadata) == 0 &&
            S_ISDIR(metadata.st_mode) &&
            access(selected, W_OK) == 0;
    }

    length = strlen(selected);
    if (length >= sizeof(copy)) {
        return 0;
    }
    memcpy(copy, selected, length + 1U);
    directory = copy;
    separator = strrchr(copy, '/');
    if (!separator) {
        directory = ".";
    } else if (separator == copy) {
        separator[1] = '\0';
        directory = copy;
    } else {
        *separator = '\0';
        directory = copy;
    }
    return stat(directory, &metadata) == 0 &&
        S_ISDIR(metadata.st_mode) &&
        access(directory, W_OK) == 0;
}

static int cli_config_mode_known(const char *mode)
{
    return mode &&
        (strcmp(mode, "balanced") == 0 ||
         strcmp(mode, "safe") == 0 ||
         strcmp(mode, "dev") == 0 ||
         strcmp(mode, "dangerous") == 0);
}

static int cli_cmd_config(const CliOptions *options)
{
    CliEnvironment environment;
    char error[AEGIS_CLI_ERROR_MAX];
    const char *api_key;
    int state_writable;
    int trace_writable;
    int exit_code;

    if (options->positional_count != 1U ||
        strcmp(options->positionals[0], "check") != 0) {
        return cli_error(
            options,
            AEGIS_CLI_EXIT_USAGE,
            "usage: aegis config check [options]"
        );
    }

    exit_code = cli_load_environment(
        options,
        &environment,
        error,
        sizeof(error)
    );
    if (exit_code != AEGIS_CLI_EXIT_SUCCESS) {
        cli_environment_clear(&environment);
        return cli_error(options, exit_code, "%s", error);
    }
    if (strcmp(environment.config.provider, "openrouter") != 0) {
        cli_environment_clear(&environment);
        return cli_error(
            options,
            AEGIS_CLI_EXIT_CONFIG,
            "unsupported provider: %s",
            environment.config.provider
        );
    }
    if (environment.config.model[0] == '\0' ||
        !cli_config_mode_known(environment.config.mode)) {
        cli_environment_clear(&environment);
        return cli_error(
            options,
            AEGIS_CLI_EXIT_CONFIG,
            "config contains an invalid model or mode"
        );
    }

    api_key = getenv(environment.config.api_key_env);
    state_writable = !environment.config.state_enabled ||
        cli_path_writable(
            environment.workspace,
            environment.config.state_path,
            0
        );
    trace_writable = !environment.config.trace_enabled ||
        cli_path_writable(
            environment.workspace,
            environment.config.trace_directory,
            1
        );

    if (options->json) {
        cJSON *root = cJSON_CreateObject();
        cJSON *checks = cJSON_CreateObject();
        cJSON *warnings = cJSON_CreateArray();

        if (!root || !checks || !warnings) {
            cJSON_Delete(root);
            cJSON_Delete(checks);
            cJSON_Delete(warnings);
            cli_environment_clear(&environment);
            return cli_error(
                options,
                AEGIS_CLI_EXIT_GENERAL,
                "failed to allocate JSON output"
            );
        }
        cJSON_AddStringToObject(root, "status", "success");
        cJSON_AddStringToObject(root, "command", "config");
        cJSON_AddStringToObject(
            root,
            "path",
            environment.config.config_path
        );
        cJSON_AddStringToObject(
            root,
            "profile",
            environment.config.active_profile.id
        );
        cJSON_AddItemToObject(root, "checks", checks);
        cJSON_AddBoolToObject(checks, "json", 1);
        cJSON_AddBoolToObject(checks, "provider", 1);
        cJSON_AddBoolToObject(checks, "model", 1);
        cJSON_AddBoolToObject(checks, "workspace", 1);
        cJSON_AddBoolToObject(checks, "profile", 1);
        cJSON_AddBoolToObject(checks, "policy", 1);
        cJSON_AddBoolToObject(checks, "state_writable", state_writable);
        cJSON_AddBoolToObject(checks, "trace_writable", trace_writable);
        cJSON_AddItemToObject(root, "warnings", warnings);
        if (!api_key || api_key[0] == '\0') {
            char warning[AEGIS_CLI_ERROR_MAX];

            snprintf(
                warning,
                sizeof(warning),
                "API key environment is not set: %s",
                environment.config.api_key_env
            );
            cJSON_AddItemToArray(warnings, cJSON_CreateString(warning));
            fprintf(stderr, "warning: %s\n", warning);
        }
        if (!state_writable) {
            cJSON_AddItemToArray(
                warnings,
                cJSON_CreateString("state directory is not currently writable")
            );
            fprintf(
                stderr,
                "warning: state directory is not currently writable\n"
            );
        }
        if (!trace_writable) {
            cJSON_AddItemToArray(
                warnings,
                cJSON_CreateString("trace directory is not currently writable")
            );
            fprintf(
                stderr,
                "warning: trace directory is not currently writable\n"
            );
        }
        if (!cli_json_print(root)) {
            cJSON_Delete(root);
            cli_environment_clear(&environment);
            return cli_error(
                options,
                AEGIS_CLI_EXIT_GENERAL,
                "failed to render JSON output"
            );
        }
        cJSON_Delete(root);
    } else if (options->quiet) {
        puts("Config valid.");
    } else {
        puts("Config check");
        printf("[OK] config: %s\n", environment.config.config_path);
        puts("[OK] JSON schema and profile");
        printf("[OK] provider: %s\n", environment.config.provider);
        printf("[OK] model: %s\n", environment.config.model);
        printf("[OK] workspace: %s\n", environment.workspace);
        printf("[OK] profile: %s\n", environment.config.active_profile.id);
        puts("[OK] tool registry and policy");
        printf(
            "[%s] state path: %s\n",
            state_writable ? "OK" : "WARN",
            environment.config.state_path
        );
        printf(
            "[%s] trace directory: %s\n",
            trace_writable ? "OK" : "WARN",
            environment.config.trace_directory
        );
        if (!api_key || api_key[0] == '\0') {
            printf(
                "[WARN] API key environment is not set: %s\n",
                environment.config.api_key_env
            );
        } else {
            printf(
                "[OK] API key environment: %s\n",
                environment.config.api_key_env
            );
        }
    }

    cli_environment_clear(&environment);
    return AEGIS_CLI_EXIT_SUCCESS;
}

static int cli_cmd_run(const CliOptions *options)
{
    CliEnvironment environment;
    CliTask task;
    char error[AEGIS_CLI_ERROR_MAX];
    int exit_code;

    if (!cli_load_task(options, &task, error, sizeof(error))) {
        return cli_error(
            options,
            AEGIS_CLI_EXIT_USAGE,
            "%s",
            error
        );
    }

    exit_code = cli_load_environment(
        options,
        &environment,
        error,
        sizeof(error)
    );
    if (exit_code != AEGIS_CLI_EXIT_SUCCESS) {
        free(task.owned);
        cli_environment_clear(&environment);
        return cli_error(options, exit_code, "%s", error);
    }

    if (!options->dry_run) {
        free(task.owned);
        cli_environment_clear(&environment);
        return cli_error(
            options,
            AEGIS_CLI_EXIT_GENERAL,
            "run backend is not implemented"
        );
    }

    if (strcmp(environment.config.mode, "dangerous") == 0) {
        fprintf(
            stderr,
            "warning: dangerous mode selected; dry-run executes no actions\n"
        );
    }

    if (options->json) {
        cJSON *root = cJSON_CreateObject();

        if (!root) {
            free(task.owned);
            cli_environment_clear(&environment);
            return cli_error(
                options,
                AEGIS_CLI_EXIT_GENERAL,
                "failed to allocate JSON output"
            );
        }
        cJSON_AddStringToObject(root, "status", "dry_run");
        cJSON_AddStringToObject(root, "command", "run");
        cJSON_AddStringToObject(
            root,
            "config",
            environment.config.config_path
        );
        cJSON_AddStringToObject(
            root,
            "profile",
            environment.config.active_profile.id
        );
        cJSON_AddStringToObject(root, "mode", environment.config.mode);
        cJSON_AddStringToObject(root, "workspace", environment.workspace);
        cJSON_AddNumberToObject(root, "max_steps", environment.config.max_steps);
        cJSON_AddNumberToObject(root, "task_bytes", (double)task.length);
        cJSON_AddNumberToObject(
            root,
            "effective_tools",
            (double)cli_effective_tool_count(&environment)
        );
        if (!cli_json_print(root)) {
            cJSON_Delete(root);
            free(task.owned);
            cli_environment_clear(&environment);
            return cli_error(
                options,
                AEGIS_CLI_EXIT_GENERAL,
                "failed to render JSON output"
            );
        }
        cJSON_Delete(root);
    } else if (options->quiet) {
        puts("Dry run valid.");
    } else {
        puts("Aegis run dry-run");
        printf("Config     : %s\n", environment.config.config_path);
        printf("Workspace  : %s\n", environment.workspace);
        printf("Profile    : %s\n", environment.config.active_profile.id);
        printf("Mode       : %s\n", environment.config.mode);
        printf("Max steps  : %d\n", environment.config.max_steps);
        printf("Task bytes : %zu\n", task.length);
        printf(
            "Tools      : %zu effective\n",
            cli_effective_tool_count(&environment)
        );
        if (options->verbose) {
            printf("Provider   : %s\n", environment.config.provider);
            printf("Model      : %s\n", environment.config.model);
            printf("Task       : %s\n", task.text);
        }
        puts("No model or tool action was executed.");
    }

    free(task.owned);
    cli_environment_clear(&environment);
    return AEGIS_CLI_EXIT_SUCCESS;
}

static int cli_validate_trace(
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

static int cli_cmd_replay(const CliOptions *options)
{
    const char *trace = options->trace;
    char error[AEGIS_CLI_ERROR_MAX];

    if (options->positional_count > 1U) {
        return cli_error(
            options,
            AEGIS_CLI_EXIT_USAGE,
            "replay accepts one trace path"
        );
    }
    if (options->positional_count == 1U) {
        if (trace) {
            return cli_error(
                options,
                AEGIS_CLI_EXIT_USAGE,
                "use either --trace or a positional trace path, not both"
            );
        }
        trace = options->positionals[0];
    }
    if (!trace) {
        return cli_error(
            options,
            AEGIS_CLI_EXIT_USAGE,
            "missing --trace"
        );
    }
    if (!cli_validate_trace(trace, error, sizeof(error))) {
        return cli_error(options, AEGIS_CLI_EXIT_TRACE, "%s", error);
    }
    return cli_error(
        options,
        AEGIS_CLI_EXIT_GENERAL,
        "replay backend is not implemented"
    );
}

static int cli_cmd_inspect(const CliOptions *options)
{
    const char *trace = options->trace;
    char error[AEGIS_CLI_ERROR_MAX];
    size_t source_count;

    if (options->positional_count > 1U) {
        return cli_error(
            options,
            AEGIS_CLI_EXIT_USAGE,
            "inspect accepts one trace path"
        );
    }
    if (options->positional_count == 1U) {
        if (trace) {
            return cli_error(
                options,
                AEGIS_CLI_EXIT_USAGE,
                "use either --trace or a positional trace path, not both"
            );
        }
        trace = options->positionals[0];
    }

    source_count = (trace != NULL) + (options->session != NULL);
    if (source_count == 0U) {
        return cli_error(
            options,
            AEGIS_CLI_EXIT_USAGE,
            "use either --trace or --session"
        );
    }
    if (source_count > 1U) {
        return cli_error(
            options,
            AEGIS_CLI_EXIT_USAGE,
            "use either --trace or --session, not both"
        );
    }
    if (trace && !cli_validate_trace(trace, error, sizeof(error))) {
        return cli_error(options, AEGIS_CLI_EXIT_TRACE, "%s", error);
    }
    return cli_error(
        options,
        AEGIS_CLI_EXIT_GENERAL,
        "inspect backend is not implemented"
    );
}

static int cli_load_json_file(
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

static int cli_cmd_eval(const CliOptions *options)
{
    char error[AEGIS_CLI_ERROR_MAX];

    if (options->positional_count != 0U) {
        return cli_error(
            options,
            AEGIS_CLI_EXIT_USAGE,
            "eval does not accept positional arguments"
        );
    }
    if (!options->suite) {
        return cli_error(
            options,
            AEGIS_CLI_EXIT_USAGE,
            "missing --suite"
        );
    }
    if (!cli_load_json_file(options->suite, error, sizeof(error))) {
        return cli_error(options, AEGIS_CLI_EXIT_EVAL, "%s", error);
    }
    return cli_error(
        options,
        AEGIS_CLI_EXIT_GENERAL,
        "eval backend is not implemented"
    );
}

static const char *cli_platform(void)
{
#if defined(__linux__) && defined(__x86_64__)
    return "linux-x86_64";
#elif defined(__linux__) && defined(__aarch64__)
    return "linux-aarch64";
#elif defined(__APPLE__) && defined(__aarch64__)
    return "macos-aarch64";
#elif defined(__APPLE__) && defined(__x86_64__)
    return "macos-x86_64";
#else
    return "unknown";
#endif
}

static int cli_cmd_version(const CliOptions *options)
{
    if (options->positional_count != 0U) {
        return cli_error(
            options,
            AEGIS_CLI_EXIT_USAGE,
            "version does not accept positional arguments"
        );
    }
    if (options->json) {
        cJSON *root = cJSON_CreateObject();

        if (!root) {
            return cli_error(
                options,
                AEGIS_CLI_EXIT_GENERAL,
                "failed to allocate JSON output"
            );
        }
        cJSON_AddStringToObject(root, "status", "success");
        cJSON_AddStringToObject(root, "command", "version");
        cJSON_AddStringToObject(root, "version", AEGIS_CLI_VERSION);
        cJSON_AddStringToObject(root, "platform", cli_platform());
        cJSON_AddStringToObject(
            root,
            "features",
            "init,config,tools,json,dry-run"
        );
        if (!cli_json_print(root)) {
            cJSON_Delete(root);
            return cli_error(
                options,
                AEGIS_CLI_EXIT_GENERAL,
                "failed to render JSON output"
            );
        }
        cJSON_Delete(root);
    } else {
        printf("aegis %s\n", AEGIS_CLI_VERSION);
        if (options->verbose) {
            printf("platform: %s\n", cli_platform());
            puts("features: init,config,tools,json,dry-run");
        }
    }
    return AEGIS_CLI_EXIT_SUCCESS;
}

static int cli_dispatch(const CliOptions *options)
{
    switch (options->command) {
        case AEGIS_CMD_INIT:
            return cli_cmd_init(options);
        case AEGIS_CMD_RUN:
            return cli_cmd_run(options);
        case AEGIS_CMD_REPLAY:
            return cli_cmd_replay(options);
        case AEGIS_CMD_INSPECT:
            return cli_cmd_inspect(options);
        case AEGIS_CMD_EVAL:
            return cli_cmd_eval(options);
        case AEGIS_CMD_TOOLS:
            return cli_cmd_tools(options);
        case AEGIS_CMD_CONFIG:
            return cli_cmd_config(options);
        case AEGIS_CMD_VERSION:
            return cli_cmd_version(options);
        default:
            return cli_error(
                options,
                AEGIS_CLI_EXIT_USAGE,
                "unsupported command"
            );
    }
}

int aegis_cli_main(int argc, char **argv)
{
    CliOptions options;
    char error[AEGIS_CLI_ERROR_MAX];
    int index;

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
    if (strcmp(argv[1], "--help") == 0 ||
        strcmp(argv[1], "-h") == 0) {
        cli_print_main_help(stdout);
        return AEGIS_CLI_EXIT_SUCCESS;
    }
    if (strcmp(argv[1], "--version") == 0 ||
        strcmp(argv[1], "-v") == 0) {
        options.command = AEGIS_CMD_VERSION;
        if (!cli_parse_options(
                argc,
                argv,
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
        return cli_cmd_version(&options);
    }

    options.command = aegis_command_from_string(argv[1]);
    if (options.command == AEGIS_CMD_UNKNOWN) {
        return cli_error(
            &options,
            AEGIS_CLI_EXIT_USAGE,
            "unknown command: %s",
            argv[1]
        );
    }
    if (!cli_parse_options(
            argc,
            argv,
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
        if (options.positional_count > 1U) {
            return cli_error(
                &options,
                AEGIS_CLI_EXIT_USAGE,
                "help accepts at most one command"
            );
        }
        if (cli_print_command_help(
                options.positional_count == 1U
                    ? options.positionals[0]
                    : NULL,
                stdout) != AEGIS_CLI_EXIT_SUCCESS) {
            return cli_error(
                &options,
                AEGIS_CLI_EXIT_USAGE,
                "unknown help command: %s",
                options.positionals[0]
            );
        }
        return AEGIS_CLI_EXIT_SUCCESS;
    }
    if (options.force && options.command != AEGIS_CMD_INIT) {
        return cli_error(
            &options,
            AEGIS_CLI_EXIT_USAGE,
            "--force is only valid with init"
        );
    }
    if (options.help) {
        return cli_print_command_help(
            aegis_command_name(options.command),
            stdout
        );
    }
    return cli_dispatch(&options);
}

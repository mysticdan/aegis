#define _XOPEN_SOURCE 700

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#include "aegis/cli_command.h"

static cJSON *read_config_json(const char *path)
{
    FILE *file = fopen(path, "rb");
    long length;
    char *data;
    cJSON *root;

    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    data = malloc((size_t)length + 1U);
    if (!data) {
        fclose(file);
        return NULL;
    }
    if (length && fread(data, 1U, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    data[length] = '\0';
    root = cJSON_Parse(data);
    free(data);
    return root;
}

static int path_writable_or_creatable(const char *path, int directory)
{
    char copy[AEGIS_CONFIG_PATH_MAX * 2U];
    struct stat metadata;
    char *separator;

    if (!path || strlen(path) >= sizeof(copy)) {
        return 0;
    }
    memcpy(copy, path, strlen(path) + 1U);
    if (!directory) {
        separator = strrchr(copy, '/');
        if (separator && separator != copy) {
            *separator = '\0';
        }
    }
    for (;;) {
        if (stat(copy, &metadata) == 0) {
            return S_ISDIR(metadata.st_mode) && access(copy, W_OK) == 0;
        }
        separator = strrchr(copy, '/');
        if (!separator) {
            return access(".", W_OK) == 0;
        }
        if (separator == copy) {
            copy[1] = '\0';
        } else {
            *separator = '\0';
        }
        if (strcmp(copy, "/") == 0) {
            return access(copy, W_OK) == 0;
        }
    }
}

static int runtime_path(
    const CliEnvironment *environment,
    const char *configured,
    char *output,
    size_t output_size
)
{
    if (configured[0] == '/') {
        int written = snprintf(
            output,
            output_size,
            "%s",
            configured
        );
        return written >= 0 && (size_t)written < output_size;
    }
    return cli_join_path(
        output,
        output_size,
        environment->workspace,
        configured
    );
}

static cJSON *dot_path_get(cJSON *root, const char *path)
{
    char copy[512];
    char *save = NULL;
    char *segment;
    cJSON *current = root;

    if (!path || !path[0] || strlen(path) >= sizeof(copy)) {
        return NULL;
    }
    memcpy(copy, path, strlen(path) + 1U);
    segment = strtok_r(copy, ".", &save);
    while (segment && cJSON_IsObject(current)) {
        current = cJSON_GetObjectItemCaseSensitive(current, segment);
        segment = strtok_r(NULL, ".", &save);
    }
    return segment ? NULL : current;
}

static int dot_path_set(
    cJSON *root,
    const char *path,
    cJSON *replacement
)
{
    char copy[512];
    char *save = NULL;
    char *segment;
    char *next;
    cJSON *current = root;

    if (!path || !path[0] || strlen(path) >= sizeof(copy)) {
        return 0;
    }
    memcpy(copy, path, strlen(path) + 1U);
    segment = strtok_r(copy, ".", &save);
    next = strtok_r(NULL, ".", &save);
    while (next) {
        current = cJSON_GetObjectItemCaseSensitive(current, segment);
        if (!cJSON_IsObject(current)) {
            return 0;
        }
        segment = next;
        next = strtok_r(NULL, ".", &save);
    }
    if (!segment ||
        !cJSON_GetObjectItemCaseSensitive(current, segment)) {
        return 0;
    }
    return cJSON_ReplaceItemInObjectCaseSensitive(
        current, segment, replacement);
}

static int write_validated_config(
    const char *path,
    const cJSON *root
)
{
    char temporary[AEGIS_CONFIG_PATH_MAX * 2U];
    char *rendered;
    int descriptor;
    size_t length;
    size_t offset = 0U;
    AegisConfig validation;

    if (snprintf(
            temporary, sizeof(temporary), "%s.tmp.XXXXXX", path) < 0 ||
        strlen(path) + 12U >= sizeof(temporary)) {
        return 0;
    }
    rendered = cJSON_Print(root);
    if (!rendered) {
        return 0;
    }
    descriptor = mkstemp(temporary);
    if (descriptor < 0) {
        cJSON_free(rendered);
        return 0;
    }
    length = strlen(rendered);
    while (offset < length) {
        ssize_t written = write(
            descriptor, rendered + offset, length - offset);
        if (written > 0) {
            offset += (size_t)written;
        } else if (written < 0 && errno != EINTR) {
            close(descriptor);
            unlink(temporary);
            cJSON_free(rendered);
            return 0;
        }
    }
    cJSON_free(rendered);
    if (write(descriptor, "\n", 1U) != 1 ||
        fsync(descriptor) != 0 ||
        fchmod(descriptor, 0644) != 0 ||
        close(descriptor) != 0 ||
        aegis_config_load_json(temporary, &validation) != AEGIS_OK ||
        rename(temporary, path) != 0) {
        unlink(temporary);
        return 0;
    }
    return 1;
}

static int config_is_local(
    const CliEnvironment *environment
)
{
    char prefix[AEGIS_CONFIG_PATH_MAX * 2U];
    size_t length;

    if (!cli_join_path(
            prefix,
            sizeof(prefix),
            environment->workspace,
            ".aegis/config")) {
        return 0;
    }
    length = strlen(prefix);
    return strncmp(environment->config.config_path, prefix, length) == 0 &&
        environment->config.config_path[length] == '/';
}

int aegis_cli_cmd_config(const CliOptions *options)
{
    CliEnvironment environment;
    char error[AEGIS_CLI_ERROR_MAX];
    const char *subcommand;
    int exit_code;

    if (!options || options->positional_count < 1U) {
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "usage: aegis config check|show|path|get|set");
    }
    subcommand = options->positionals[0];
    exit_code = cli_load_environment(
        options, &environment, error, sizeof(error));
    if (exit_code != AEGIS_CLI_EXIT_SUCCESS) {
        cli_environment_clear(&environment);
        return cli_error(options, exit_code, "%s", error);
    }
    if (strcmp(subcommand, "check") == 0) {
        const char *api_key = environment.config.api_key_env[0]
            ? getenv(environment.config.api_key_env)
            : NULL;
        char state_path[AEGIS_CONFIG_PATH_MAX * 2U];
        char trace_path[AEGIS_CONFIG_PATH_MAX * 2U];
        int state_ok;
        int trace_ok;
        if (options->positional_count != 1U) {
            cli_environment_clear(&environment);
            return cli_error(
                options,
                AEGIS_CLI_EXIT_USAGE,
                "usage: aegis config check"
            );
        }
        state_ok = runtime_path(
                &environment,
                environment.config.state_path,
                state_path,
                sizeof(state_path)) &&
            path_writable_or_creatable(state_path, 0);
        trace_ok = runtime_path(
                &environment,
                environment.config.trace_directory,
                trace_path,
                sizeof(trace_path)) &&
            path_writable_or_creatable(trace_path, 1);
        if (options->json) {
            cJSON *root = cJSON_CreateObject();
            cJSON *checks = cJSON_CreateObject();
            cJSON *warnings = cJSON_CreateArray();
            cJSON_AddStringToObject(
                root,
                "status",
                state_ok && trace_ok ? "success" : "error"
            );
            cJSON_AddStringToObject(root, "command", "config");
            cJSON_AddStringToObject(
                root, "path", environment.config.config_path);
            cJSON_AddStringToObject(
                root, "profile", environment.config.active_profile.id);
            cJSON_AddItemToObject(root, "checks", checks);
            cJSON_AddBoolToObject(checks, "json", 1);
            cJSON_AddBoolToObject(checks, "provider", 1);
            cJSON_AddBoolToObject(checks, "model", 1);
            cJSON_AddBoolToObject(checks, "workspace", 1);
            cJSON_AddBoolToObject(checks, "profile", 1);
            cJSON_AddBoolToObject(checks, "policy", 1);
            cJSON_AddBoolToObject(checks, "state_writable", state_ok);
            cJSON_AddBoolToObject(checks, "trace_writable", trace_ok);
            cJSON_AddItemToObject(root, "warnings", warnings);
            if (strcmp(environment.config.provider, "ollama") != 0 &&
                strcmp(environment.config.provider, "mock") != 0 &&
                (!api_key || !api_key[0])) {
                cJSON_AddItemToArray(
                    warnings,
                    cJSON_CreateString("provider API key is not set")
                );
            }
            cli_json_print(root);
            cJSON_Delete(root);
        } else {
            puts("Config check");
            printf("[OK] config: %s\n", environment.config.config_path);
            printf("[OK] provider: %s\n", environment.config.provider);
            printf("[OK] model: %s\n", environment.config.model);
            printf("[OK] profile: %s\n",
                   environment.config.active_profile.id);
            printf("[OK] tools: %zu effective\n",
                   cli_effective_tool_count(&environment));
            printf("[%s] state path: %s\n",
                   state_ok ? "OK" : "ERROR", state_path);
            printf("[%s] trace path: %s\n",
                   trace_ok ? "OK" : "ERROR", trace_path);
            if (strcmp(environment.config.provider, "ollama") != 0 &&
                strcmp(environment.config.provider, "mock") != 0 &&
                (!api_key || !api_key[0])) {
                printf("[WARN] API key environment is not set: %s\n",
                       environment.config.api_key_env);
            }
        }
        if (!state_ok || !trace_ok) {
            exit_code = AEGIS_CLI_EXIT_CONFIG;
        }
    } else if (strcmp(subcommand, "path") == 0) {
        if (options->positional_count != 1U) {
            exit_code = cli_error(
                options, AEGIS_CLI_EXIT_USAGE,
                "usage: aegis config path");
        } else if (options->json) {
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "status", "success");
            cJSON_AddStringToObject(root, "command", "config");
            cJSON_AddStringToObject(
                root, "path", environment.config.config_path);
            cli_json_print(root);
            cJSON_Delete(root);
        } else {
            puts(environment.config.config_path);
        }
    } else if (strcmp(subcommand, "show") == 0) {
        cJSON *root = read_config_json(environment.config.config_path);
        if (options->positional_count != 1U || !root) {
            exit_code = cli_error(
                options, AEGIS_CLI_EXIT_CONFIG, "cannot read config");
        } else if (options->json) {
            cJSON *output = cJSON_CreateObject();
            cJSON_AddStringToObject(output, "status", "success");
            cJSON_AddStringToObject(output, "command", "config");
            cJSON_AddItemToObject(output, "config", root);
            root = NULL;
            cli_json_print(output);
            cJSON_Delete(output);
        } else {
            char *rendered = cJSON_Print(root);
            if (rendered) {
                puts(rendered);
                cJSON_free(rendered);
            }
        }
        cJSON_Delete(root);
    } else if (strcmp(subcommand, "get") == 0) {
        cJSON *root = NULL;
        cJSON *value;
        if (options->positional_count != 2U ||
            !(root = read_config_json(environment.config.config_path)) ||
            !(value = dot_path_get(root, options->positionals[1]))) {
            cJSON_Delete(root);
            cli_environment_clear(&environment);
            return cli_error(
                options, AEGIS_CLI_EXIT_CONFIG,
                "config key not found");
        }
        if (options->json) {
            cJSON *output = cJSON_CreateObject();
            cJSON_AddStringToObject(output, "status", "success");
            cJSON_AddStringToObject(output, "command", "config");
            cJSON_AddStringToObject(
                output, "key", options->positionals[1]);
            cJSON_AddItemToObject(
                output, "value", cJSON_Duplicate(value, 1));
            cli_json_print(output);
            cJSON_Delete(output);
        } else if (cJSON_IsString(value)) {
            puts(value->valuestring);
        } else {
            char *rendered = cJSON_Print(value);
            if (rendered) {
                puts(rendered);
                cJSON_free(rendered);
            }
        }
        cJSON_Delete(root);
    } else if (strcmp(subcommand, "set") == 0) {
        cJSON *root;
        cJSON *value;
        cJSON *replacement;
        int inserted = 0;
        if (options->positional_count != 3U) {
            cli_environment_clear(&environment);
            return cli_error(
                options, AEGIS_CLI_EXIT_USAGE,
                "usage: aegis config set <dot.path> <json-value>");
        }
        if (!config_is_local(&environment)) {
            cli_environment_clear(&environment);
            return cli_error(
                options, AEGIS_CLI_EXIT_CONFIG,
                "config set only modifies workspace .aegis config");
        }
        root = read_config_json(environment.config.config_path);
        value = cJSON_Parse(options->positionals[2]);
        replacement = value ? cJSON_Duplicate(value, 1) : NULL;
        if (root && value && replacement) {
            inserted = dot_path_set(
                root,
                options->positionals[1],
                replacement
            );
        }
        if (!root || !value || !replacement || !inserted ||
            !write_validated_config(environment.config.config_path, root)) {
            if (replacement && !inserted) {
                cJSON_Delete(replacement);
            }
            cJSON_Delete(root);
            cJSON_Delete(value);
            cli_environment_clear(&environment);
            return cli_error(
                options, AEGIS_CLI_EXIT_CONFIG,
                "failed to update or validate config");
        }
        cJSON_Delete(root);
        cJSON_Delete(value);
        if (options->json) {
            cJSON *output = cJSON_CreateObject();
            cJSON_AddStringToObject(output, "status", "success");
            cJSON_AddStringToObject(output, "command", "config");
            cJSON_AddStringToObject(
                output, "key", options->positionals[1]);
            cli_json_print(output);
            cJSON_Delete(output);
        } else {
            printf("Updated %s.\n", options->positionals[1]);
        }
    } else {
        cli_environment_clear(&environment);
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "unknown config subcommand: %s", subcommand);
    }
    cli_environment_clear(&environment);
    return exit_code;
}

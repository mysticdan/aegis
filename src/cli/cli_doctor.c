#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sqlite3.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#include "aegis/cli_command.h"

static int resolve_runtime_path(
    const CliEnvironment *environment,
    const char *configured,
    char *resolved,
    size_t size
)
{
    if (configured[0] == '/') {
        int written = snprintf(resolved, size, "%s", configured);
        return written >= 0 && (size_t)written < size;
    }
    return cli_join_path(
        resolved, size, environment->workspace, configured);
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

int aegis_cli_cmd_doctor(const CliOptions *options)
{
    CliEnvironment environment;
    char error[AEGIS_CLI_ERROR_MAX];
    char state_path[AEGIS_CONFIG_PATH_MAX * 2U];
    char trace_path[AEGIS_CONFIG_PATH_MAX * 2U];
    const char *api_key;
    int exit_code;
    int state_parent_ok;
    int trace_ok;
    int provider_key_ok;

    if (options->positional_count != 0U) {
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "doctor does not accept positional arguments");
    }
    exit_code = cli_load_environment(
        options, &environment, error, sizeof(error));
    if (exit_code != AEGIS_CLI_EXIT_SUCCESS) {
        cli_environment_clear(&environment);
        return cli_error(options, exit_code, "%s", error);
    }
    if (!resolve_runtime_path(
            &environment,
            environment.config.state_path,
            state_path,
            sizeof(state_path)) ||
        !resolve_runtime_path(
            &environment,
            environment.config.trace_directory,
            trace_path,
            sizeof(trace_path))) {
        cli_environment_clear(&environment);
        return cli_error(
            options, AEGIS_CLI_EXIT_STATE, "runtime path is too long");
    }
    api_key = environment.config.api_key_env[0]
        ? getenv(environment.config.api_key_env)
        : NULL;
    provider_key_ok =
        strcmp(environment.config.provider, "ollama") == 0 ||
        strcmp(environment.config.provider, "mock") == 0 ||
        (api_key && api_key[0]);
    state_parent_ok = path_writable_or_creatable(state_path, 0);
    trace_ok = path_writable_or_creatable(trace_path, 1);

    if (options->json) {
        cJSON *root = cJSON_CreateObject();
        cJSON *checks = cJSON_CreateObject();
        cJSON_AddStringToObject(
            root,
            "status",
            !state_parent_ok || !trace_ok
                ? "error"
                : provider_key_ok ? "success" : "warning"
        );
        cJSON_AddStringToObject(root, "command", "doctor");
        cJSON_AddStringToObject(root, "version", AEGIS_CLI_VERSION);
        cJSON_AddStringToObject(root, "workspace", environment.workspace);
        cJSON_AddStringToObject(
            root, "config", environment.config.config_path);
        cJSON_AddStringToObject(
            root, "profile", environment.config.active_profile.id);
        cJSON_AddItemToObject(root, "checks", checks);
        cJSON_AddBoolToObject(checks, "config", 1);
        cJSON_AddBoolToObject(checks, "profile", 1);
        cJSON_AddBoolToObject(checks, "workspace", 1);
        cJSON_AddBoolToObject(checks, "state", state_parent_ok);
        cJSON_AddBoolToObject(checks, "trace", trace_ok);
        cJSON_AddBoolToObject(checks, "provider_key", provider_key_ok);
        cJSON_AddBoolToObject(
            checks, "sqlite", sqlite3_libversion_number() > 0);
        cJSON_AddBoolToObject(
            checks, "curl", curl_version() != NULL);
        cJSON_AddNumberToObject(
            checks,
            "effective_tools",
            (double)cli_effective_tool_count(&environment)
        );
        cli_json_print(root);
        cJSON_Delete(root);
    } else {
        puts("Aegis doctor");
        printf("[OK] binary: aegis %s\n", AEGIS_CLI_VERSION);
        printf("[OK] config: %s\n", environment.config.config_path);
        printf("[OK] profile: %s\n", environment.config.active_profile.id);
        printf("[OK] workspace: %s\n", environment.workspace);
        printf("[%s] state: %s\n", state_parent_ok ? "OK" : "ERROR", state_path);
        printf("[%s] trace: %s\n", trace_ok ? "OK" : "ERROR", trace_path);
        printf("[%s] provider credential: %s\n",
               provider_key_ok ? "OK" : "WARN",
               environment.config.api_key_env);
        printf("[OK] SQLite: %s\n", sqlite3_libversion());
        printf("[OK] libcurl: %s\n", curl_version());
        printf("[OK] tools: %zu effective\n",
               cli_effective_tool_count(&environment));
    }
    cli_environment_clear(&environment);
    return state_parent_ok && trace_ok
        ? AEGIS_CLI_EXIT_SUCCESS
        : AEGIS_CLI_EXIT_GENERAL;
}

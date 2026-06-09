#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#include "aegis/cli_command.h"

static int profile_id_valid(const char *id)
{
    const unsigned char *cursor = (const unsigned char *)id;

    if (!id || !id[0]) {
        return 0;
    }
    while (*cursor) {
        if (!( (*cursor >= 'a' && *cursor <= 'z') ||
               (*cursor >= 'A' && *cursor <= 'Z') ||
               (*cursor >= '0' && *cursor <= '9') ||
               *cursor == '_' || *cursor == '-')) {
            return 0;
        }
        ++cursor;
    }
    return 1;
}

static int profile_directory(
    const AegisConfig *config,
    char *directory,
    size_t size
)
{
    char config_copy[AEGIS_CONFIG_PATH_MAX];
    char *separator;
    char *base;

    if (!config || strlen(config->config_path) >= sizeof(config_copy)) {
        return 0;
    }
    memcpy(
        config_copy,
        config->config_path,
        strlen(config->config_path) + 1U
    );
    separator = strrchr(config_copy, '/');
    if (separator) {
        *separator = '\0';
    } else {
        memcpy(config_copy, ".", 2U);
    }
    base = strrchr(config_copy, '/');
    base = base ? base + 1 : config_copy;
    if (strcmp(base, "config") == 0) {
        if (base == config_copy) {
            memcpy(config_copy, ".", 2U);
        } else {
            base[-1] = '\0';
        }
    }
    return cli_join_path(
        directory,
        size,
        config_copy,
        config->profile_directory
    );
}

static int profile_directory_is_local(
    const CliEnvironment *environment,
    const char *directory
)
{
    char expected[AEGIS_CONFIG_PATH_MAX * 2U];

    return cli_join_path(
            expected,
            sizeof(expected),
            environment->workspace,
            ".aegis/profiles") &&
        strcmp(expected, directory) == 0;
}

static int read_json(const char *path, cJSON **root)
{
    FILE *file;
    long length;
    char *data;

    *root = NULL;
    file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    data = malloc((size_t)length + 1U);
    if (!data) {
        fclose(file);
        return 0;
    }
    if (length && fread(data, 1U, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return 0;
    }
    fclose(file);
    data[length] = '\0';
    *root = cJSON_Parse(data);
    free(data);
    return *root != NULL;
}

static int atomic_write_json(const char *path, const cJSON *root)
{
    char temporary[AEGIS_CONFIG_PATH_MAX * 2U];
    char *rendered;
    int descriptor;
    size_t length;
    size_t offset = 0U;

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
    if (write(descriptor, "\n", 1U) != 1 ||
        fsync(descriptor) != 0 ||
        fchmod(descriptor, 0644) != 0 ||
        close(descriptor) != 0 ||
        rename(temporary, path) != 0) {
        unlink(temporary);
        cJSON_free(rendered);
        return 0;
    }
    cJSON_free(rendered);
    return 1;
}

int aegis_cli_cmd_profiles(const CliOptions *options)
{
    CliEnvironment environment;
    char error[AEGIS_CLI_ERROR_MAX];
    char directory[AEGIS_CONFIG_PATH_MAX * 2U];
    const char *subcommand;
    const char *profile_id;
    int exit_code;

    if (!options || options->positional_count < 1U) {
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "usage: aegis profiles list|show|validate|new");
    }
    subcommand = options->positionals[0];
    exit_code = cli_load_environment(
        options, &environment, error, sizeof(error));
    if (exit_code != AEGIS_CLI_EXIT_SUCCESS) {
        cli_environment_clear(&environment);
        return cli_error(options, exit_code, "%s", error);
    }
    if (!profile_directory(
            &environment.config, directory, sizeof(directory))) {
        cli_environment_clear(&environment);
        return cli_error(
            options, AEGIS_CLI_EXIT_PROFILE,
            "profile directory path is too long");
    }

    if (strcmp(subcommand, "list") == 0) {
        DIR *dir;
        struct dirent *entry;
        cJSON *root = NULL;
        cJSON *profiles = NULL;

        if (options->positional_count != 1U || !(dir = opendir(directory))) {
            cli_environment_clear(&environment);
            return cli_error(
                options, AEGIS_CLI_EXIT_PROFILE,
                "cannot open profile directory");
        }
        if (options->json) {
            root = cJSON_CreateObject();
            profiles = cJSON_CreateArray();
            cJSON_AddStringToObject(root, "status", "success");
            cJSON_AddStringToObject(root, "command", "profiles");
            cJSON_AddItemToObject(root, "profiles", profiles);
        }
        while ((entry = readdir(dir)) != NULL) {
            size_t length = strlen(entry->d_name);
            if (length <= 5U ||
                strcmp(entry->d_name + length - 5U, ".json") != 0) {
                continue;
            }
            if (profiles) {
                char id[AEGIS_CONFIG_NAME_MAX];
                size_t id_length = length - 5U;
                if (id_length < sizeof(id)) {
                    memcpy(id, entry->d_name, id_length);
                    id[id_length] = '\0';
                    cJSON_AddItemToArray(profiles, cJSON_CreateString(id));
                }
            } else {
                printf("%.*s\n", (int)(length - 5U), entry->d_name);
            }
        }
        closedir(dir);
        if (root) {
            cli_json_print(root);
            cJSON_Delete(root);
        }
        cli_environment_clear(&environment);
        return AEGIS_CLI_EXIT_SUCCESS;
    }

    if (options->positional_count != 2U) {
        cli_environment_clear(&environment);
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "profiles %s requires one profile name", subcommand);
    }
    profile_id = cli_profile_id(options->positionals[1]);
    if (!profile_id_valid(profile_id)) {
        cli_environment_clear(&environment);
        return cli_error(
            options, AEGIS_CLI_EXIT_PROFILE, "invalid profile id");
    }

    if (strcmp(subcommand, "show") == 0 ||
        strcmp(subcommand, "validate") == 0) {
        AegisAgentProfile profile;
        char path[AEGIS_CONFIG_PATH_MAX * 2U];
        AegisStatus status;

        if (snprintf(
                path, sizeof(path), "%s/%s.json", directory, profile_id) < 0 ||
            strlen(directory) + strlen(profile_id) + 7U >= sizeof(path)) {
            cli_environment_clear(&environment);
            return cli_error(
                options, AEGIS_CLI_EXIT_PROFILE, "profile path is too long");
        }
        status = aegis_agent_profile_load_json(path, &profile);
        if (status != AEGIS_OK ||
            aegis_tool_registry_validate_profile(
                &environment.registry, &profile) != AEGIS_OK) {
            cli_environment_clear(&environment);
            return cli_error(
                options, AEGIS_CLI_EXIT_PROFILE,
                "profile is missing or invalid: %s", profile_id);
        }
        if (strcmp(subcommand, "validate") == 0) {
            if (options->json) {
                cJSON *root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "status", "success");
                cJSON_AddStringToObject(root, "command", "profiles");
                cJSON_AddStringToObject(root, "profile", profile.id);
                cJSON_AddBoolToObject(root, "valid", 1);
                cli_json_print(root);
                cJSON_Delete(root);
            } else {
                printf("Profile valid: %s\n", profile.id);
            }
        } else {
            cJSON *root;
            if (!read_json(path, &root)) {
                cli_environment_clear(&environment);
                return cli_error(
                    options, AEGIS_CLI_EXIT_PROFILE,
                    "cannot read profile");
            }
            if (options->json) {
                cJSON *output = cJSON_CreateObject();
                cJSON_AddStringToObject(output, "status", "success");
                cJSON_AddStringToObject(output, "command", "profiles");
                cJSON_AddStringToObject(output, "profile", profile_id);
                cJSON_AddItemToObject(output, "definition", root);
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
        }
    } else if (strcmp(subcommand, "new") == 0) {
        char source[AEGIS_CONFIG_PATH_MAX * 2U];
        char destination[AEGIS_CONFIG_PATH_MAX * 2U];
        cJSON *root;
        cJSON *identity;

        if (!profile_directory_is_local(&environment, directory)) {
            cli_environment_clear(&environment);
            return cli_error(
                options,
                AEGIS_CLI_EXIT_PROFILE,
                "profiles new requires an initialized workspace"
            );
        }
        if (snprintf(
                source, sizeof(source), "%s/coding_agent.json", directory) < 0 ||
            snprintf(
                destination, sizeof(destination), "%s/%s.json",
                directory, profile_id) < 0 ||
            access(destination, F_OK) == 0 ||
            !read_json(source, &root)) {
            cli_environment_clear(&environment);
            return cli_error(
                options, AEGIS_CLI_EXIT_PROFILE,
                "cannot create profile '%s'", profile_id);
        }
        cJSON_ReplaceItemInObject(
            root, "id", cJSON_CreateString(profile_id));
        identity = cJSON_GetObjectItemCaseSensitive(root, "identity");
        if (cJSON_IsObject(identity)) {
            cJSON_ReplaceItemInObject(
                identity, "name", cJSON_CreateString(profile_id));
        }
        if (!atomic_write_json(destination, root)) {
            cJSON_Delete(root);
            cli_environment_clear(&environment);
            return cli_error(
                options, AEGIS_CLI_EXIT_PROFILE,
                "failed to write profile");
        }
        cJSON_Delete(root);
        if (options->json) {
            cJSON *output = cJSON_CreateObject();
            cJSON_AddStringToObject(output, "status", "success");
            cJSON_AddStringToObject(output, "command", "profiles");
            cJSON_AddStringToObject(output, "created", profile_id);
            cli_json_print(output);
            cJSON_Delete(output);
        } else {
            printf("Created profile %s.\n", profile_id);
        }
    } else {
        cli_environment_clear(&environment);
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "unknown profiles subcommand: %s", subcommand);
    }
    cli_environment_clear(&environment);
    return AEGIS_CLI_EXIT_SUCCESS;
}

#define _XOPEN_SOURCE 700

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#include "aegis/cli.h"
#include "aegis/config.h"
#include "aegis/tool_registry.h"

#include "cli_init.h"

#ifndef AEGIS_DEFAULT_RESOURCE_DIR
#define AEGIS_DEFAULT_RESOURCE_DIR "."
#endif

#define INIT_FILE_LIMIT (1024U * 1024U)
#define INIT_FILE_COUNT 14U
#define INIT_DIRECTORY_COUNT 9U

typedef struct {
    const char *relative_path;
    char *content;
    size_t length;
    int exists;
    int differs;
} InitFile;

static const char *const init_directories[INIT_DIRECTORY_COUNT] = {
    ".aegis",
    ".aegis/config",
    ".aegis/profiles",
    ".aegis/prompts",
    ".aegis/state",
    ".aegis/traces",
    ".aegis/traces/safe",
    ".aegis/traces/dev",
    ".aegis/traces/dangerous"
};

static const char *const profile_names[] = {
    "minimal_agent.json",
    "coding_agent.json",
    "security_agent.json",
    "ops_agent.json",
    "assistant_agent.json"
};

static const char *const prompt_names[] = {
    "system_minimal_agent.md",
    "system_coding_agent.md",
    "system_security_agent.md",
    "system_ops_agent.md",
    "system_assistant_agent.md"
};

static int copy_text(char *destination, size_t size, const char *source)
{
    size_t length;

    if (!destination || size == 0U || !source) {
        return 0;
    }
    length = strlen(source);
    if (length >= size) {
        return 0;
    }
    memcpy(destination, source, length + 1U);
    return 1;
}

static int join_path(
    char *destination,
    size_t size,
    const char *left,
    const char *right
)
{
    size_t left_length;
    int written;

    if (!destination || !left || !right) {
        return 0;
    }
    left_length = strlen(left);
    written = snprintf(
        destination,
        size,
        "%s%s%s",
        left,
        left_length > 0U && left[left_length - 1U] == '/' ? "" : "/",
        right
    );
    return written >= 0 && (size_t)written < size;
}

static int managed_path_status(
    const char *path,
    int expect_directory,
    int *exists,
    char *error,
    size_t error_size
)
{
    struct stat metadata;

    *exists = 0;
    if (lstat(path, &metadata) != 0) {
        if (errno == ENOENT) {
            return 1;
        }
        snprintf(error, error_size, "cannot inspect path: %s", path);
        return 0;
    }
    *exists = 1;
    if (S_ISLNK(metadata.st_mode)) {
        snprintf(error, error_size, "managed path is a symlink: %s", path);
        return 0;
    }
    if (expect_directory && !S_ISDIR(metadata.st_mode)) {
        snprintf(error, error_size, "managed path is not a directory: %s", path);
        return 0;
    }
    if (!expect_directory && !S_ISREG(metadata.st_mode)) {
        snprintf(error, error_size, "managed path is not a regular file: %s", path);
        return 0;
    }
    return 1;
}

static int read_file(
    const char *path,
    char **content,
    size_t *length,
    char *error,
    size_t error_size
)
{
    struct stat metadata;
    FILE *file;
    char *buffer;
    size_t size;

    if (lstat(path, &metadata) != 0 ||
        !S_ISREG(metadata.st_mode) ||
        S_ISLNK(metadata.st_mode)) {
        snprintf(error, error_size, "resource is not a regular file: %s", path);
        return 0;
    }
    if (metadata.st_size < 0 ||
        (unsigned long long)metadata.st_size > INIT_FILE_LIMIT) {
        snprintf(error, error_size, "resource is too large: %s", path);
        return 0;
    }

    size = (size_t)metadata.st_size;
    buffer = malloc(size + 1U);
    if (!buffer) {
        snprintf(error, error_size, "out of memory while reading: %s", path);
        return 0;
    }
    file = fopen(path, "rb");
    if (!file) {
        free(buffer);
        snprintf(error, error_size, "cannot open resource: %s", path);
        return 0;
    }
    if (size > 0U && fread(buffer, 1U, size, file) != size) {
        fclose(file);
        free(buffer);
        snprintf(error, error_size, "cannot read resource: %s", path);
        return 0;
    }
    if (fclose(file) != 0) {
        free(buffer);
        snprintf(error, error_size, "cannot close resource: %s", path);
        return 0;
    }
    buffer[size] = '\0';
    *content = buffer;
    *length = size;
    return 1;
}

static int add_change(
    char changes[AEGIS_CLI_INIT_MAX_CHANGES][AEGIS_CONFIG_PATH_MAX],
    size_t *count,
    const char *path
)
{
    if (*count >= AEGIS_CLI_INIT_MAX_CHANGES ||
        !copy_text(changes[*count], AEGIS_CONFIG_PATH_MAX, path)) {
        return 0;
    }
    ++*count;
    return 1;
}

static int make_directory(
    const char *workspace,
    const char *relative_path,
    AegisCliInitResult *result,
    char *error,
    size_t error_size
)
{
    char path[AEGIS_CONFIG_PATH_MAX];
    int exists;

    if (!join_path(path, sizeof(path), workspace, relative_path) ||
        !managed_path_status(
            path,
            1,
            &exists,
            error,
            error_size)) {
        return 0;
    }
    if (exists) {
        return 1;
    }
    if (mkdir(path, 0755) != 0) {
        snprintf(error, error_size, "cannot create directory: %s", path);
        return 0;
    }
    if (!add_change(result->created, &result->created_count, relative_path)) {
        snprintf(error, error_size, "too many created paths");
        return 0;
    }
    return 1;
}

static int atomic_write_file(
    const char *path,
    const char *content,
    size_t length,
    char *error,
    size_t error_size
)
{
    char temporary[AEGIS_CONFIG_PATH_MAX];
    size_t offset = 0U;
    int descriptor;
    int written;

    written = snprintf(
            temporary,
            sizeof(temporary),
            "%s.tmp.XXXXXX",
            path);
    if (written < 0 || (size_t)written >= sizeof(temporary)) {
        snprintf(error, error_size, "path is too long: %s", path);
        return 0;
    }

    descriptor = mkstemp(temporary);
    if (descriptor < 0) {
        snprintf(error, error_size, "cannot create temporary file: %s", path);
        return 0;
    }
    if (fchmod(descriptor, 0644) != 0) {
        close(descriptor);
        unlink(temporary);
        snprintf(error, error_size, "cannot set file permissions: %s", path);
        return 0;
    }
    while (offset < length) {
        ssize_t written = write(
            descriptor,
            content + offset,
            length - offset
        );

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(descriptor);
            unlink(temporary);
            snprintf(error, error_size, "cannot write file: %s", path);
            return 0;
        }
        offset += (size_t)written;
    }
    if (fsync(descriptor) != 0 || close(descriptor) != 0) {
        unlink(temporary);
        snprintf(error, error_size, "cannot finalize file: %s", path);
        return 0;
    }
    if (rename(temporary, path) != 0) {
        unlink(temporary);
        snprintf(error, error_size, "cannot install file: %s", path);
        return 0;
    }
    return 1;
}

static cJSON *required_object(cJSON *parent, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);

    return cJSON_IsObject(item) ? item : NULL;
}

static int replace_string(cJSON *object, const char *name, const char *value)
{
    cJSON *replacement = cJSON_CreateString(value);

    if (!replacement) {
        return 0;
    }
    if (!cJSON_ReplaceItemInObjectCaseSensitive(object, name, replacement)) {
        cJSON_Delete(replacement);
        return 0;
    }
    return 1;
}

static int prefix_runtime_path(
    cJSON *object,
    const char *name,
    char *error,
    size_t error_size
)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    char path[AEGIS_CONFIG_PATH_MAX];

    if (!cJSON_IsString(item) || !item->valuestring) {
        snprintf(error, error_size, "config field is not a string: %s", name);
        return 0;
    }
    if (strncmp(item->valuestring, ".aegis/", 7U) == 0) {
        return 1;
    }
    if (snprintf(
            path,
            sizeof(path),
            ".aegis/%s",
            item->valuestring) < 0 ||
        strlen(path) >= sizeof(path) ||
        !replace_string(object, name, path)) {
        snprintf(error, error_size, "cannot normalize config path: %s", name);
        return 0;
    }
    return 1;
}

static int build_config_content(
    const char *resource_path,
    const char *profile_id,
    char **content,
    size_t *length,
    char *mode,
    size_t mode_size,
    char *profile,
    size_t profile_size,
    char *error,
    size_t error_size
)
{
    char *source = NULL;
    size_t source_length = 0U;
    char *rendered;
    cJSON *root;
    cJSON *app;
    cJSON *agent;
    cJSON *state;
    cJSON *trace;
    cJSON *logging;
    cJSON *logging_file;
    cJSON *mode_item;
    cJSON *profile_item;

    if (!read_file(
            resource_path,
            &source,
            &source_length,
            error,
            error_size)) {
        return 0;
    }
    root = cJSON_ParseWithLengthOpts(
        source,
        source_length + 1U,
        NULL,
        1
    );
    free(source);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        snprintf(error, error_size, "invalid config resource: %s", resource_path);
        return 0;
    }

    app = required_object(root, "app");
    agent = required_object(root, "agent");
    state = required_object(root, "state");
    trace = required_object(root, "trace");
    logging = required_object(root, "logging");
    logging_file = logging ? required_object(logging, "file") : NULL;
    if (!app || !agent || !state || !trace || !logging_file) {
        cJSON_Delete(root);
        snprintf(error, error_size, "config resource is incomplete: %s", resource_path);
        return 0;
    }

    if (!replace_string(agent, "profile_directory", "profiles") ||
        !prefix_runtime_path(state, "path", error, error_size) ||
        !prefix_runtime_path(trace, "directory", error, error_size) ||
        !prefix_runtime_path(logging_file, "path", error, error_size)) {
        cJSON_Delete(root);
        return 0;
    }
    if (profile_id &&
        (!replace_string(app, "default_profile", profile_id) ||
         !replace_string(agent, "default_profile", profile_id))) {
        cJSON_Delete(root);
        snprintf(error, error_size, "cannot set active profile");
        return 0;
    }

    mode_item = cJSON_GetObjectItemCaseSensitive(app, "mode");
    profile_item = cJSON_GetObjectItemCaseSensitive(app, "default_profile");
    if (!cJSON_IsString(mode_item) || !mode_item->valuestring ||
        !cJSON_IsString(profile_item) || !profile_item->valuestring ||
        !copy_text(mode, mode_size, mode_item->valuestring) ||
        !copy_text(profile, profile_size, profile_item->valuestring)) {
        cJSON_Delete(root);
        snprintf(error, error_size, "config resource has invalid identity");
        return 0;
    }

    rendered = cJSON_Print(root);
    cJSON_Delete(root);
    if (!rendered) {
        snprintf(error, error_size, "cannot render config resource");
        return 0;
    }
    *length = strlen(rendered);
    *content = rendered;
    return 1;
}

static void clear_init_files(InitFile files[INIT_FILE_COUNT])
{
    size_t index;

    for (index = 0U; index < INIT_FILE_COUNT; ++index) {
        free(files[index].content);
        files[index].content = NULL;
    }
}

static int prepare_init_files(
    const char *mode,
    const char *profile_id,
    InitFile files[INIT_FILE_COUNT],
    AegisCliInitResult *result,
    char *error,
    size_t error_size
)
{
    static const char *const config_outputs[] = {
        ".aegis/config/aegis.json",
        ".aegis/config/safe.json",
        ".aegis/config/dev.json",
        ".aegis/config/dangerous.json"
    };
    static const char *const config_sources[] = {
        NULL,
        "safe.json",
        "dev.json",
        "dangerous.json"
    };
    const char *resource_root = aegis_cli_resource_directory();
    const char *active_source = mode ? mode : "aegis";
    char relative[AEGIS_CONFIG_PATH_MAX];
    char resource_path[AEGIS_CONFIG_PATH_MAX];
    char source_name[AEGIS_CONFIG_NAME_MAX];
    size_t file_index = 0U;
    size_t index;

    memset(files, 0, sizeof(*files) * INIT_FILE_COUNT);
    for (index = 0U; index < 4U; ++index) {
        const char *source = config_sources[index];
        char ignored_mode[AEGIS_CONFIG_NAME_MAX];
        char ignored_profile[AEGIS_CONFIG_NAME_MAX];

        if (!source) {
            if (snprintf(
                    source_name,
                    sizeof(source_name),
                    "%s.json",
                    active_source) < 0 ||
                strlen(source_name) >= sizeof(source_name)) {
                snprintf(error, error_size, "invalid mode resource name");
                goto fail;
            }
            source = source_name;
        }
        if (snprintf(relative, sizeof(relative), "config/%s", source) < 0 ||
            strlen(relative) >= sizeof(relative) ||
            !join_path(
                resource_path,
                sizeof(resource_path),
                resource_root,
                relative)) {
            snprintf(error, error_size, "resource path is too long");
            goto fail;
        }
        files[file_index].relative_path = config_outputs[index];
        if (!build_config_content(
                resource_path,
                index == 0U ? profile_id : NULL,
                &files[file_index].content,
                &files[file_index].length,
                index == 0U ? result->mode : ignored_mode,
                index == 0U ? sizeof(result->mode) : sizeof(ignored_mode),
                index == 0U ? result->profile : ignored_profile,
                index == 0U ? sizeof(result->profile) : sizeof(ignored_profile),
                error,
                error_size)) {
            goto fail;
        }
        ++file_index;
    }

    for (index = 0U;
         index < sizeof(profile_names) / sizeof(profile_names[0]);
         ++index) {
        if (snprintf(
                relative,
                sizeof(relative),
                "profiles/%s",
                profile_names[index]) < 0 ||
            strlen(relative) >= sizeof(relative) ||
            !join_path(
                resource_path,
                sizeof(resource_path),
                resource_root,
                relative)) {
            snprintf(error, error_size, "profile resource path is too long");
            goto fail;
        }
        files[file_index].relative_path =
            index == 0U ? ".aegis/profiles/minimal_agent.json" :
            index == 1U ? ".aegis/profiles/coding_agent.json" :
            index == 2U ? ".aegis/profiles/security_agent.json" :
            index == 3U ? ".aegis/profiles/ops_agent.json" :
                          ".aegis/profiles/assistant_agent.json";
        if (!read_file(
                resource_path,
                &files[file_index].content,
                &files[file_index].length,
                error,
                error_size)) {
            goto fail;
        }
        ++file_index;
    }

    for (index = 0U;
         index < sizeof(prompt_names) / sizeof(prompt_names[0]);
         ++index) {
        if (snprintf(
                relative,
                sizeof(relative),
                "prompts/%s",
                prompt_names[index]) < 0 ||
            strlen(relative) >= sizeof(relative) ||
            !join_path(
                resource_path,
                sizeof(resource_path),
                resource_root,
                relative)) {
            snprintf(error, error_size, "prompt resource path is too long");
            goto fail;
        }
        files[file_index].relative_path =
            index == 0U ? ".aegis/prompts/system_minimal_agent.md" :
            index == 1U ? ".aegis/prompts/system_coding_agent.md" :
            index == 2U ? ".aegis/prompts/system_security_agent.md" :
            index == 3U ? ".aegis/prompts/system_ops_agent.md" :
                          ".aegis/prompts/system_assistant_agent.md";
        if (!read_file(
                resource_path,
                &files[file_index].content,
                &files[file_index].length,
                error,
                error_size)) {
            goto fail;
        }
        ++file_index;
    }
    return file_index == INIT_FILE_COUNT;

fail:
    clear_init_files(files);
    return 0;
}

static int validate_profile_files(
    const char *workspace,
    char *error,
    size_t error_size
)
{
    size_t index;

    for (index = 0U;
         index < sizeof(profile_names) / sizeof(profile_names[0]);
         ++index) {
        char relative[AEGIS_CONFIG_PATH_MAX];
        char path[AEGIS_CONFIG_PATH_MAX];
        AegisAgentProfile profile;

        if (snprintf(
                relative,
                sizeof(relative),
                ".aegis/profiles/%s",
                profile_names[index]) < 0 ||
            strlen(relative) >= sizeof(relative) ||
            !join_path(path, sizeof(path), workspace, relative) ||
            aegis_agent_profile_load_json(path, &profile) != AEGIS_OK) {
            snprintf(
                error,
                error_size,
                "installed profile is invalid: %s",
                profile_names[index]
            );
            return 0;
        }
    }
    return 1;
}

static int validate_install(
    const char *workspace,
    AegisCliInitResult *result,
    char *error,
    size_t error_size
)
{
    static const char *const config_names[] = {
        "aegis.json", "safe.json", "dev.json", "dangerous.json"
    };
    AegisToolRegistry registry;
    size_t index;

    aegis_tool_registry_init(&registry);
    if (aegis_tool_registry_register_defaults(&registry) != AEGIS_OK) {
        snprintf(error, error_size, "cannot initialize tool registry");
        return 0;
    }
    for (index = 0U;
         index < sizeof(config_names) / sizeof(config_names[0]);
         ++index) {
        char relative[AEGIS_CONFIG_PATH_MAX];
        char path[AEGIS_CONFIG_PATH_MAX];
        AegisConfig config;

        if (snprintf(
                relative,
                sizeof(relative),
                ".aegis/config/%s",
                config_names[index]) < 0 ||
            strlen(relative) >= sizeof(relative) ||
            !join_path(path, sizeof(path), workspace, relative) ||
            aegis_config_load_json(path, &config) != AEGIS_OK ||
            aegis_tool_registry_validate_config(&registry, &config) !=
                AEGIS_OK) {
            snprintf(
                error,
                error_size,
                "installed config is invalid: %s",
                config_names[index]
            );
            return 0;
        }
        if (index == 0U &&
            (!copy_text(result->mode, sizeof(result->mode), config.mode) ||
             !copy_text(
                 result->profile,
                 sizeof(result->profile),
                 config.active_profile.id))) {
            snprintf(error, error_size, "installed config identity is too long");
            return 0;
        }
    }
    return validate_profile_files(workspace, error, error_size);
}

static int complete_install_exists(
    const char *workspace,
    char *error,
    size_t error_size
)
{
    static const char *const managed_files[INIT_FILE_COUNT] = {
        ".aegis/config/aegis.json",
        ".aegis/config/safe.json",
        ".aegis/config/dev.json",
        ".aegis/config/dangerous.json",
        ".aegis/profiles/minimal_agent.json",
        ".aegis/profiles/coding_agent.json",
        ".aegis/profiles/security_agent.json",
        ".aegis/profiles/ops_agent.json",
        ".aegis/profiles/assistant_agent.json",
        ".aegis/prompts/system_minimal_agent.md",
        ".aegis/prompts/system_coding_agent.md",
        ".aegis/prompts/system_security_agent.md",
        ".aegis/prompts/system_ops_agent.md",
        ".aegis/prompts/system_assistant_agent.md"
    };
    size_t index;

    for (index = 0U; index < INIT_DIRECTORY_COUNT; ++index) {
        char path[AEGIS_CONFIG_PATH_MAX];
        int exists;

        if (!join_path(
                path,
                sizeof(path),
                workspace,
                init_directories[index]) ||
            !managed_path_status(
                path,
                1,
                &exists,
                error,
                error_size)) {
            return -1;
        }
        if (!exists) {
            return 0;
        }
    }
    for (index = 0U; index < INIT_FILE_COUNT; ++index) {
        char path[AEGIS_CONFIG_PATH_MAX];
        int exists;

        if (!join_path(
                path,
                sizeof(path),
                workspace,
                managed_files[index]) ||
            !managed_path_status(
                path,
                0,
                &exists,
                error,
                error_size)) {
            return -1;
        }
        if (!exists) {
            return 0;
        }
    }
    return 1;
}

const char *aegis_cli_resource_directory(void)
{
    const char *override = getenv("AEGIS_RESOURCE_DIR");

    return override && override[0] != '\0'
        ? override
        : AEGIS_DEFAULT_RESOURCE_DIR;
}

int aegis_cli_init_execute(
    const AegisCliInitRequest *request,
    AegisCliInitResult *result,
    char *error,
    size_t error_size
)
{
    InitFile files[INIT_FILE_COUNT];
    struct stat workspace_metadata;
    char *workspace;
    int complete;
    size_t index;

    if (!request || !result || !error || error_size == 0U) {
        return AEGIS_CLI_EXIT_CONFIG;
    }
    memset(result, 0, sizeof(*result));
    memset(files, 0, sizeof(files));
    if (request->profile_id &&
        strcmp(request->profile_id, "minimal_agent") != 0 &&
        strcmp(request->profile_id, "coding_agent") != 0 &&
        strcmp(request->profile_id, "security_agent") != 0 &&
        strcmp(request->profile_id, "ops_agent") != 0 &&
        strcmp(request->profile_id, "assistant_agent") != 0) {
        snprintf(
            error,
            error_size,
            "unknown profile: %s",
            request->profile_id
        );
        return AEGIS_CLI_EXIT_PROFILE;
    }

    workspace = realpath(request->workspace ? request->workspace : ".", NULL);
    if (!workspace ||
        stat(workspace, &workspace_metadata) != 0 ||
        !S_ISDIR(workspace_metadata.st_mode)) {
        free(workspace);
        snprintf(
            error,
            error_size,
            "workspace does not exist or is not a directory: %s",
            request->workspace ? request->workspace : "."
        );
        return AEGIS_CLI_EXIT_WORKSPACE;
    }
    if (!copy_text(result->workspace, sizeof(result->workspace), workspace) ||
        !join_path(
            result->root,
            sizeof(result->root),
            workspace,
            ".aegis")) {
        free(workspace);
        snprintf(error, error_size, "workspace path is too long");
        return AEGIS_CLI_EXIT_WORKSPACE;
    }

    complete = complete_install_exists(workspace, error, error_size);
    if (complete < 0) {
        free(workspace);
        return AEGIS_CLI_EXIT_WORKSPACE;
    }
    if (complete && !request->force &&
        validate_install(workspace, result, error, error_size)) {
        result->already_initialized = 1;
        free(workspace);
        return AEGIS_CLI_EXIT_SUCCESS;
    }

    if (!prepare_init_files(
            request->mode,
            request->profile_id,
            files,
            result,
            error,
            error_size)) {
        free(workspace);
        return AEGIS_CLI_EXIT_CONFIG;
    }

    for (index = 0U; index < INIT_DIRECTORY_COUNT; ++index) {
        char path[AEGIS_CONFIG_PATH_MAX];
        int exists;

        if (!join_path(
                path,
                sizeof(path),
                workspace,
                init_directories[index]) ||
            !managed_path_status(
                path,
                1,
                &exists,
                error,
                error_size)) {
            clear_init_files(files);
            free(workspace);
            return AEGIS_CLI_EXIT_WORKSPACE;
        }
    }

    for (index = 0U; index < INIT_FILE_COUNT; ++index) {
        char path[AEGIS_CONFIG_PATH_MAX];
        char *existing = NULL;
        size_t existing_length = 0U;

        if (!join_path(
                path,
                sizeof(path),
                workspace,
                files[index].relative_path) ||
            !managed_path_status(
                path,
                0,
                &files[index].exists,
                error,
                error_size)) {
            clear_init_files(files);
            free(workspace);
            return AEGIS_CLI_EXIT_WORKSPACE;
        }
        if (!files[index].exists) {
            continue;
        }
        if (!read_file(
                path,
                &existing,
                &existing_length,
                error,
                error_size)) {
            clear_init_files(files);
            free(workspace);
            return AEGIS_CLI_EXIT_CONFIG;
        }
        files[index].differs =
            existing_length != files[index].length ||
            memcmp(existing, files[index].content, existing_length) != 0;
        free(existing);
        if (files[index].differs && !request->force) {
            snprintf(
                error,
                error_size,
                "managed file conflicts with template: %s",
                files[index].relative_path
            );
            clear_init_files(files);
            free(workspace);
            return AEGIS_CLI_EXIT_CONFIG;
        }
    }

    for (index = 0U; index < INIT_DIRECTORY_COUNT; ++index) {
        if (!make_directory(
                workspace,
                init_directories[index],
                result,
                error,
                error_size)) {
            clear_init_files(files);
            free(workspace);
            return AEGIS_CLI_EXIT_CONFIG;
        }
    }
    for (index = 0U; index < INIT_FILE_COUNT; ++index) {
        char path[AEGIS_CONFIG_PATH_MAX];

        if (files[index].exists && !files[index].differs) {
            continue;
        }
        if (!join_path(
                path,
                sizeof(path),
                workspace,
                files[index].relative_path) ||
            !atomic_write_file(
                path,
                files[index].content,
                files[index].length,
                error,
                error_size)) {
            clear_init_files(files);
            free(workspace);
            return AEGIS_CLI_EXIT_CONFIG;
        }
        if (files[index].exists) {
            if (!add_change(
                    result->updated,
                    &result->updated_count,
                    files[index].relative_path)) {
                snprintf(error, error_size, "too many updated paths");
                clear_init_files(files);
                free(workspace);
                return AEGIS_CLI_EXIT_CONFIG;
            }
        } else if (!add_change(
                       result->created,
                       &result->created_count,
                       files[index].relative_path)) {
            snprintf(error, error_size, "too many created paths");
            clear_init_files(files);
            free(workspace);
            return AEGIS_CLI_EXIT_CONFIG;
        }
    }

    clear_init_files(files);
    if (!validate_install(workspace, result, error, error_size)) {
        free(workspace);
        return AEGIS_CLI_EXIT_CONFIG;
    }
    free(workspace);
    return AEGIS_CLI_EXIT_SUCCESS;
}

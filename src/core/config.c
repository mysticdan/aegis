#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__has_include)
#  if __has_include(<cjson/cJSON.h>)
#    include <cjson/cJSON.h>
#  else
#    include <cJSON.h>
#  endif
#else
#  include <cjson/cJSON.h>
#endif

#include "aegis/config.h"

#define AEGIS_CONFIG_MAX_FILE_SIZE (16U * 1024U * 1024U)

static int copy_string(char *dst, size_t dst_size, const char *src)
{
    size_t length;

    if (!dst || dst_size == 0 || !src) {
        return 0;
    }

    length = strlen(src);
    if (length >= dst_size) {
        return 0;
    }

    memcpy(dst, src, length + 1);
    return 1;
}

static int string_list_contains(
    const AegisConfigStringList *list,
    const char *value
)
{
    size_t index;

    if (!list || !value) {
        return 0;
    }

    for (index = 0; index < list->count; ++index) {
        if (strcmp(list->items[index], value) == 0) {
            return 1;
        }
    }

    return 0;
}

static AegisStatus read_text_file(const char *path, char **text)
{
    FILE *file;
    long file_size;
    size_t bytes_read;
    char *buffer;

    if (!path || !text) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }

    *text = NULL;
    errno = 0;
    file = fopen(path, "rb");
    if (!file) {
        return errno == ENOENT ? AEGIS_ERR_NOT_FOUND : AEGIS_ERR_IO;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return AEGIS_ERR_IO;
    }

    file_size = ftell(file);
    if (file_size < 0 || (unsigned long)file_size > AEGIS_CONFIG_MAX_FILE_SIZE) {
        fclose(file);
        return AEGIS_ERR_IO;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return AEGIS_ERR_IO;
    }

    buffer = malloc((size_t)file_size + 1);
    if (!buffer) {
        fclose(file);
        return AEGIS_ERR_OOM;
    }

    bytes_read = fread(buffer, 1, (size_t)file_size, file);
    if (bytes_read != (size_t)file_size || ferror(file)) {
        free(buffer);
        fclose(file);
        return AEGIS_ERR_IO;
    }

    buffer[bytes_read] = '\0';
    fclose(file);
    *text = buffer;
    return AEGIS_OK;
}

static AegisStatus load_json_root(const char *path, cJSON **root)
{
    AegisStatus status;
    char *text;
    cJSON *parsed;

    if (!path || !root) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }

    *root = NULL;
    status = read_text_file(path, &text);
    if (status != AEGIS_OK) {
        return status;
    }

    parsed = cJSON_ParseWithOpts(text, NULL, 1);
    free(text);

    if (!parsed || !cJSON_IsObject(parsed)) {
        cJSON_Delete(parsed);
        return AEGIS_ERR_PARSE;
    }

    *root = parsed;
    return AEGIS_OK;
}

static cJSON *required_object(const cJSON *parent, const char *name)
{
    cJSON *item;

    if (!parent || !name) {
        return NULL;
    }

    item = cJSON_GetObjectItemCaseSensitive(parent, name);
    return cJSON_IsObject(item) ? item : NULL;
}

static AegisStatus parse_string(
    const cJSON *object,
    const char *name,
    char *destination,
    size_t destination_size,
    int required
)
{
    cJSON *item;

    item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!item) {
        return required ? AEGIS_ERR_PARSE : AEGIS_OK;
    }
    if (!cJSON_IsString(item) || !item->valuestring ||
        !copy_string(destination, destination_size, item->valuestring)) {
        return AEGIS_ERR_PARSE;
    }

    return AEGIS_OK;
}

static AegisStatus parse_bool(
    const cJSON *object,
    const char *name,
    int *destination,
    int required
)
{
    cJSON *item;

    item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!item) {
        return required ? AEGIS_ERR_PARSE : AEGIS_OK;
    }
    if (!cJSON_IsBool(item)) {
        return AEGIS_ERR_PARSE;
    }

    *destination = cJSON_IsTrue(item);
    return AEGIS_OK;
}

static AegisStatus parse_int(
    const cJSON *object,
    const char *name,
    int *destination,
    int required,
    int minimum,
    int maximum
)
{
    cJSON *item;
    double number;
    int value;

    item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!item) {
        return required ? AEGIS_ERR_PARSE : AEGIS_OK;
    }
    if (!cJSON_IsNumber(item)) {
        return AEGIS_ERR_PARSE;
    }

    number = item->valuedouble;
    if (number < (double)minimum || number > (double)maximum) {
        return AEGIS_ERR_PARSE;
    }

    value = (int)number;
    if ((double)value != number) {
        return AEGIS_ERR_PARSE;
    }

    *destination = value;
    return AEGIS_OK;
}

static AegisStatus parse_long_long(
    const cJSON *object,
    const char *name,
    long long *destination,
    int required,
    long long minimum
)
{
    cJSON *item;
    double number;
    long long value;

    item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!item) {
        return required ? AEGIS_ERR_PARSE : AEGIS_OK;
    }
    if (!cJSON_IsNumber(item)) {
        return AEGIS_ERR_PARSE;
    }

    number = item->valuedouble;
    if (number < (double)minimum || number > (double)LLONG_MAX) {
        return AEGIS_ERR_PARSE;
    }

    value = (long long)number;
    if ((double)value != number) {
        return AEGIS_ERR_PARSE;
    }

    *destination = value;
    return AEGIS_OK;
}

static AegisStatus parse_double(
    const cJSON *object,
    const char *name,
    double *destination,
    int required,
    double minimum,
    double maximum
)
{
    cJSON *item;

    item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!item) {
        return required ? AEGIS_ERR_PARSE : AEGIS_OK;
    }
    if (!cJSON_IsNumber(item) ||
        item->valuedouble < minimum ||
        item->valuedouble > maximum) {
        return AEGIS_ERR_PARSE;
    }

    *destination = item->valuedouble;
    return AEGIS_OK;
}

static AegisStatus parse_string_list(
    const cJSON *object,
    const char *name,
    AegisConfigStringList *destination,
    size_t maximum_count,
    int required
)
{
    cJSON *array;
    cJSON *item;
    int count;
    int index;

    array = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!array) {
        return required ? AEGIS_ERR_PARSE : AEGIS_OK;
    }
    if (!cJSON_IsArray(array)) {
        return AEGIS_ERR_PARSE;
    }

    count = cJSON_GetArraySize(array);
    if (count < 0 || (size_t)count > maximum_count) {
        return AEGIS_ERR_PARSE;
    }

    memset(destination, 0, sizeof(*destination));
    for (index = 0; index < count; ++index) {
        item = cJSON_GetArrayItem(array, index);
        if (!cJSON_IsString(item) || !item->valuestring ||
            !copy_string(
                destination->items[destination->count],
                sizeof(destination->items[destination->count]),
                item->valuestring
            ) ||
            string_list_contains(destination, item->valuestring)) {
            return AEGIS_ERR_PARSE;
        }
        ++destination->count;
    }

    return AEGIS_OK;
}

static int valid_identifier(const char *value)
{
    const unsigned char *cursor;

    if (!value || value[0] == '\0') {
        return 0;
    }

    for (cursor = (const unsigned char *)value; *cursor; ++cursor) {
        if (!isalnum(*cursor) && *cursor != '_' && *cursor != '-') {
            return 0;
        }
    }

    return 1;
}

static int path_is_absolute(const char *path)
{
    return path && path[0] == '/';
}

static int path_dirname(
    const char *path,
    char *destination,
    size_t destination_size
)
{
    const char *slash;
    size_t length;

    if (!path || !destination || destination_size == 0) {
        return 0;
    }

    slash = strrchr(path, '/');
    if (!slash) {
        return copy_string(destination, destination_size, ".");
    }
    if (slash == path) {
        return copy_string(destination, destination_size, "/");
    }

    length = (size_t)(slash - path);
    if (length >= destination_size) {
        return 0;
    }

    memcpy(destination, path, length);
    destination[length] = '\0';
    return 1;
}

static const char *path_basename(const char *path)
{
    const char *slash;

    if (!path) {
        return "";
    }

    slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int join_path(
    char *destination,
    size_t destination_size,
    const char *left,
    const char *right
)
{
    int written;
    size_t left_length;

    if (!destination || !left || !right) {
        return 0;
    }

    left_length = strlen(left);
    written = snprintf(
        destination,
        destination_size,
        "%s%s%s",
        left,
        left_length > 0 && left[left_length - 1] == '/' ? "" : "/",
        right
    );

    return written >= 0 && (size_t)written < destination_size;
}

static int build_profile_path(
    const AegisConfig *cfg,
    const char *profile_id,
    char *destination,
    size_t destination_size
)
{
    char file_name[AEGIS_CONFIG_PATH_MAX];
    char config_directory[AEGIS_CONFIG_PATH_MAX];
    char project_root[AEGIS_CONFIG_PATH_MAX];
    char profile_directory[AEGIS_CONFIG_PATH_MAX];
    int written;

    if (!cfg || !valid_identifier(profile_id)) {
        return 0;
    }

    written = snprintf(
        file_name,
        sizeof(file_name),
        "%s%s",
        profile_id,
        cfg->profile_file_suffix
    );
    if (written < 0 || (size_t)written >= sizeof(file_name)) {
        return 0;
    }

    if (path_is_absolute(cfg->profile_directory)) {
        return join_path(
            destination,
            destination_size,
            cfg->profile_directory,
            file_name
        );
    }

    if (!path_dirname(
            cfg->config_path,
            config_directory,
            sizeof(config_directory))) {
        return 0;
    }

    if (strcmp(path_basename(config_directory), "config") == 0) {
        if (!path_dirname(
                config_directory,
                project_root,
                sizeof(project_root))) {
            return 0;
        }
    } else if (!copy_string(
                   project_root,
                   sizeof(project_root),
                   config_directory)) {
        return 0;
    }

    if (!join_path(
            profile_directory,
            sizeof(profile_directory),
            project_root,
            cfg->profile_directory)) {
        return 0;
    }

    return join_path(
        destination,
        destination_size,
        profile_directory,
        file_name
    );
}

static int minimum_positive(int left, int right)
{
    if (left <= 0) {
        return right;
    }
    if (right <= 0) {
        return left;
    }
    return left < right ? left : right;
}

static void default_list_add(
    AegisConfigStringList *list,
    const char *value
)
{
    if (list->count < AEGIS_CONFIG_MAX_TOOLS &&
        copy_string(
            list->items[list->count],
            sizeof(list->items[list->count]),
            value)) {
        ++list->count;
    }
}

static void default_policy_add(
    AegisConfig *config,
    const char *tool,
    const char *decision,
    const char *risk
)
{
    AegisPolicyDecision *entry;

    if (config->policy_decision_count >=
        AEGIS_CONFIG_MAX_POLICY_DECISIONS) {
        return;
    }

    entry = &config->policy_decisions[config->policy_decision_count];
    if (copy_string(entry->tool, sizeof(entry->tool), tool) &&
        copy_string(entry->decision, sizeof(entry->decision), decision) &&
        copy_string(entry->risk, sizeof(entry->risk), risk)) {
        ++config->policy_decision_count;
    }
}

void aegis_agent_profile_defaults(AegisAgentProfile *profile)
{
    if (!profile) {
        return;
    }

    memset(profile, 0, sizeof(*profile));
    copy_string(profile->schema_version, sizeof(profile->schema_version), "0.1");
    copy_string(profile->kind, sizeof(profile->kind), "agent_profile");
    copy_string(profile->prompt_type, sizeof(profile->prompt_type), "file");
    copy_string(
        profile->action_protocol,
        sizeof(profile->action_protocol),
        "json_action_v1"
    );
    profile->force_json_action = 1;
    profile->invalid_json_repair_attempts = 1;
    profile->temperature = 0.1;
    profile->top_p = 0.9;
    profile->max_tokens = 2048;
    profile->max_steps = 12;
    profile->max_tool_calls = 24;
    profile->max_context_chars = 80000;
    profile->max_observation_chars = 16000;
    profile->max_history_events = 16;
    profile->summarize_old_history = 1;
    copy_string(profile->autonomy, sizeof(profile->autonomy), "low");
    profile->config_policy_is_authoritative = 1;
    profile->request_only_needed_tools = 1;
}

void aegis_config_defaults(AegisConfig *cfg)
{
    if (!cfg) {
        return;
    }

    memset(cfg, 0, sizeof(*cfg));
    copy_string(cfg->schema_version, sizeof(cfg->schema_version), "0.1");
    copy_string(cfg->kind, sizeof(cfg->kind), "aegis_config");
    copy_string(cfg->app_name, sizeof(cfg->app_name), "aegis");
    copy_string(cfg->display_name, sizeof(cfg->display_name), "Aegis");
    copy_string(cfg->mode, sizeof(cfg->mode), "balanced");
    copy_string(
        cfg->default_profile,
        sizeof(cfg->default_profile),
        "coding_agent"
    );

    cfg->max_steps = 30;
    cfg->max_tool_calls = 80;
    cfg->max_wall_time_ms = 900000;
    cfg->retry_enabled = 1;
    cfg->max_retries = 2;
    cfg->retry_backoff_ms = 750;
    cfg->configured_max_steps = cfg->max_steps;
    cfg->configured_max_tool_calls = cfg->max_tool_calls;

    copy_string(cfg->provider, sizeof(cfg->provider), "openrouter");
    copy_string(
        cfg->provider_base_url,
        sizeof(cfg->provider_base_url),
        "https://openrouter.ai/api/v1"
    );
    copy_string(
        cfg->chat_completions_path,
        sizeof(cfg->chat_completions_path),
        "/chat/completions"
    );
    copy_string(
        cfg->api_key_env,
        sizeof(cfg->api_key_env),
        "OPENROUTER_API_KEY"
    );
    cfg->provider_timeout_ms = 120000;
    cfg->provider_connect_timeout_ms = 10000;

    copy_string(cfg->model_profile, sizeof(cfg->model_profile), "default");
    copy_string(
        cfg->model,
        sizeof(cfg->model),
        "moonshotai/kimi-k2.6:free"
    );
    cfg->temperature = 0.1;
    cfg->top_p = 0.9;
    cfg->max_tokens = 4096;
    cfg->configured_temperature = cfg->temperature;
    cfg->configured_top_p = cfg->top_p;
    cfg->configured_max_tokens = cfg->max_tokens;

    copy_string(
        cfg->profile_directory,
        sizeof(cfg->profile_directory),
        "profiles"
    );
    copy_string(
        cfg->profile_file_suffix,
        sizeof(cfg->profile_file_suffix),
        ".json"
    );
    copy_string(
        cfg->action_protocol,
        sizeof(cfg->action_protocol),
        "json_action_v1"
    );
    cfg->force_json_action = 1;
    cfg->invalid_json_repair_attempts = 2;
    copy_string(
        cfg->tool_access_strategy,
        sizeof(cfg->tool_access_strategy),
        "intersection"
    );
    copy_string(
        cfg->policy_authority,
        sizeof(cfg->policy_authority),
        "config"
    );
    copy_string(
        cfg->limit_strategy,
        sizeof(cfg->limit_strategy),
        "most_restrictive"
    );
    copy_string(cfg->model_source, sizeof(cfg->model_source), "config");

    cfg->max_history_events = 20;
    cfg->max_file_read_bytes = 65536;
    cfg->max_tool_output_bytes = 65536;
    cfg->summarize_old_history = 1;
    cfg->configured_max_history_events = cfg->max_history_events;

    copy_string(cfg->workspace_root, sizeof(cfg->workspace_root), ".");
    cfg->restrict_to_root = 1;
    cfg->deny_absolute_paths = 1;
    cfg->deny_parent_traversal = 1;
    cfg->max_file_bytes = 1048576;
    copy_string(
        cfg->blocked_paths.items[cfg->blocked_paths.count++],
        AEGIS_CONFIG_NAME_MAX,
        ".git/objects"
    );
    copy_string(
        cfg->blocked_paths.items[cfg->blocked_paths.count++],
        AEGIS_CONFIG_NAME_MAX,
        ".env"
    );
    copy_string(
        cfg->blocked_paths.items[cfg->blocked_paths.count++],
        AEGIS_CONFIG_NAME_MAX,
        ".ssh"
    );
    copy_string(
        cfg->blocked_paths.items[cfg->blocked_paths.count++],
        AEGIS_CONFIG_NAME_MAX,
        "id_rsa"
    );
    copy_string(
        cfg->blocked_paths.items[cfg->blocked_paths.count++],
        AEGIS_CONFIG_NAME_MAX,
        "id_ed25519"
    );
    copy_string(
        cfg->blocked_paths.items[cfg->blocked_paths.count++],
        AEGIS_CONFIG_NAME_MAX,
        "secrets.json"
    );

    cfg->allow_file_read = 1;
    cfg->allow_file_write = 1;
    cfg->write_requires_approval = 1;
    cfg->shell_timeout_ms = 10000;
    cfg->http_timeout_ms = 30000;

    copy_string(
        cfg->approval_mode,
        sizeof(cfg->approval_mode),
        "on_risky_action"
    );
    copy_string(
        cfg->default_decision,
        sizeof(cfg->default_decision),
        "deny_unknown"
    );
    default_list_add(&cfg->enabled_tools, "list_dir");
    default_list_add(&cfg->enabled_tools, "read_file");
    default_list_add(&cfg->enabled_tools, "write_file");
    default_list_add(&cfg->enabled_tools, "append_file");

    default_list_add(&cfg->disabled_tools, "search_file");
    default_list_add(&cfg->disabled_tools, "shell");
    default_list_add(&cfg->disabled_tools, "run_tests");
    default_list_add(&cfg->disabled_tools, "git_status");
    default_list_add(&cfg->disabled_tools, "git_diff");
    default_list_add(&cfg->disabled_tools, "git_log");
    default_list_add(&cfg->disabled_tools, "git_apply_patch");
    default_list_add(&cfg->disabled_tools, "http_get");
    default_list_add(&cfg->disabled_tools, "http_post");
    default_list_add(&cfg->disabled_tools, "ask_user");
    default_list_add(&cfg->disabled_tools, "send_message");
    default_list_add(&cfg->disabled_tools, "reminder");
    default_list_add(&cfg->disabled_tools, "read_log");
    default_list_add(&cfg->disabled_tools, "grep_log");
    default_list_add(&cfg->disabled_tools, "health_check");
    default_list_add(&cfg->disabled_tools, "mcp_tool");

    default_policy_add(cfg, "list_dir", "allow", "low");
    default_policy_add(cfg, "read_file", "allow", "low");
    default_policy_add(
        cfg, "write_file", "require_approval", "medium");
    default_policy_add(
        cfg, "append_file", "require_approval", "medium");
    default_policy_add(cfg, "search_file", "deny", "low");
    default_policy_add(cfg, "shell", "deny", "high");
    default_policy_add(cfg, "run_tests", "deny", "medium");
    default_policy_add(cfg, "git_status", "deny", "low");
    default_policy_add(cfg, "git_diff", "deny", "low");
    default_policy_add(cfg, "git_log", "deny", "low");
    default_policy_add(cfg, "git_apply_patch", "deny", "medium");
    default_policy_add(cfg, "http_get", "deny", "high");
    default_policy_add(cfg, "http_post", "deny", "critical");
    default_policy_add(cfg, "ask_user", "deny", "low");
    default_policy_add(cfg, "send_message", "deny", "critical");
    default_policy_add(cfg, "reminder", "deny", "medium");
    default_policy_add(cfg, "read_log", "deny", "low");
    default_policy_add(cfg, "grep_log", "deny", "low");
    default_policy_add(cfg, "health_check", "deny", "medium");
    default_policy_add(cfg, "mcp_tool", "deny", "high");

    cfg->sandbox_enabled = 1;
    cfg->cpu_time_seconds = 20;
    cfg->memory_bytes = 536870912LL;
    cfg->file_size_bytes = 10485760LL;
    cfg->process_count = 32;

    cfg->state_enabled = 1;
    copy_string(cfg->state_path, sizeof(cfg->state_path), "state/aegis.db");
    cfg->trace_enabled = 1;
    copy_string(cfg->trace_directory, sizeof(cfg->trace_directory), "traces");
    cfg->redact_secrets = 1;
    copy_string(cfg->logging_level, sizeof(cfg->logging_level), "info");
    cfg->eval_enabled = 1;
    cfg->max_eval_steps = 30;

    aegis_agent_profile_defaults(&cfg->active_profile);
}

static AegisStatus parse_agent_profile(
    const cJSON *root,
    const char *path,
    AegisAgentProfile *profile
)
{
    AegisStatus status;
    AegisAgentProfile parsed;
    cJSON *identity;
    cJSON *prompt;
    cJSON *protocol;
    cJSON *model_overrides;
    cJSON *limits;
    cJSON *context;
    cJSON *tools;
    cJSON *capabilities;
    cJSON *guardrails;

    aegis_agent_profile_defaults(&parsed);
    if (!copy_string(parsed.profile_path, sizeof(parsed.profile_path), path)) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }

#define PROFILE_PARSE(call)           \
    do {                              \
        status = (call);              \
        if (status != AEGIS_OK) {     \
            return status;            \
        }                             \
    } while (0)

    PROFILE_PARSE(parse_string(
        root, "schema_version", parsed.schema_version,
        sizeof(parsed.schema_version), 1));
    PROFILE_PARSE(parse_string(
        root, "kind", parsed.kind, sizeof(parsed.kind), 1));
    PROFILE_PARSE(parse_string(
        root, "id", parsed.id, sizeof(parsed.id), 1));

    if (strcmp(parsed.kind, "agent_profile") != 0 ||
        !valid_identifier(parsed.id)) {
        return AEGIS_ERR_PARSE;
    }

    identity = required_object(root, "identity");
    prompt = required_object(root, "prompt");
    protocol = required_object(root, "protocol");
    model_overrides = required_object(root, "model_overrides");
    limits = required_object(root, "limits");
    context = required_object(root, "context");
    tools = required_object(root, "tools");
    capabilities = required_object(root, "capabilities");
    guardrails = required_object(root, "guardrails");
    if (!identity || !prompt || !protocol || !model_overrides || !limits ||
        !context || !tools || !capabilities || !guardrails) {
        return AEGIS_ERR_PARSE;
    }

    PROFILE_PARSE(parse_string(
        identity, "name", parsed.name, sizeof(parsed.name), 1));
    PROFILE_PARSE(parse_string(
        identity, "role", parsed.role, sizeof(parsed.role), 1));
    PROFILE_PARSE(parse_string(
        identity, "description", parsed.description,
        sizeof(parsed.description), 1));

    PROFILE_PARSE(parse_string(
        prompt, "type", parsed.prompt_type, sizeof(parsed.prompt_type), 1));
    PROFILE_PARSE(parse_string(
        prompt, "path", parsed.prompt_path, sizeof(parsed.prompt_path), 1));
    if (strcmp(parsed.prompt_type, "file") != 0 ||
        parsed.prompt_path[0] == '\0') {
        return AEGIS_ERR_PARSE;
    }

    PROFILE_PARSE(parse_string(
        protocol, "action_protocol", parsed.action_protocol,
        sizeof(parsed.action_protocol), 1));
    PROFILE_PARSE(parse_string_list(
        protocol, "allowed_actions", &parsed.allowed_actions,
        AEGIS_CONFIG_MAX_ACTIONS, 1));
    PROFILE_PARSE(parse_bool(
        protocol, "force_json_action", &parsed.force_json_action, 1));
    PROFILE_PARSE(parse_int(
        protocol, "invalid_json_repair_attempts",
        &parsed.invalid_json_repair_attempts, 1, 0, 100));

    PROFILE_PARSE(parse_double(
        model_overrides, "temperature", &parsed.temperature, 1, 0.0, 2.0));
    PROFILE_PARSE(parse_double(
        model_overrides, "top_p", &parsed.top_p, 1, 0.0, 1.0));
    PROFILE_PARSE(parse_int(
        model_overrides, "max_tokens", &parsed.max_tokens,
        1, 1, INT_MAX));

    PROFILE_PARSE(parse_int(
        limits, "max_steps", &parsed.max_steps, 1, 1, INT_MAX));
    PROFILE_PARSE(parse_int(
        limits, "max_tool_calls", &parsed.max_tool_calls, 1, 1, INT_MAX));
    PROFILE_PARSE(parse_int(
        limits, "max_context_chars", &parsed.max_context_chars,
        1, 1, INT_MAX));
    PROFILE_PARSE(parse_int(
        limits, "max_observation_chars", &parsed.max_observation_chars,
        1, 1, INT_MAX));

    PROFILE_PARSE(parse_int(
        context, "max_history_events", &parsed.max_history_events,
        1, 1, INT_MAX));
    PROFILE_PARSE(parse_bool(
        context, "summarize_old_history",
        &parsed.summarize_old_history, 1));

    PROFILE_PARSE(parse_string_list(
        tools, "requested", &parsed.requested_tools,
        AEGIS_CONFIG_MAX_TOOLS, 1));

    PROFILE_PARSE(parse_string(
        capabilities, "autonomy", parsed.autonomy,
        sizeof(parsed.autonomy), 1));
    PROFILE_PARSE(parse_bool(
        capabilities, "can_modify_workspace",
        &parsed.can_modify_workspace, 1));
    PROFILE_PARSE(parse_bool(
        capabilities, "can_execute_commands",
        &parsed.can_execute_commands, 1));
    PROFILE_PARSE(parse_bool(
        capabilities, "can_access_network",
        &parsed.can_access_network, 1));

    PROFILE_PARSE(parse_bool(
        guardrails, "config_policy_is_authoritative",
        &parsed.config_policy_is_authoritative, 1));
    PROFILE_PARSE(parse_bool(
        guardrails, "request_only_needed_tools",
        &parsed.request_only_needed_tools, 1));

    if (!parsed.config_policy_is_authoritative) {
        return AEGIS_ERR_PARSE;
    }

    *profile = parsed;
    return AEGIS_OK;

#undef PROFILE_PARSE
}

AegisStatus aegis_agent_profile_load_json(
    const char *path,
    AegisAgentProfile *profile
)
{
    AegisStatus status;
    AegisAgentProfile parsed;
    cJSON *root;

    if (!path || !profile) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }

    status = load_json_root(path, &root);
    if (status != AEGIS_OK) {
        return status;
    }

    status = parse_agent_profile(root, path, &parsed);
    cJSON_Delete(root);
    if (status != AEGIS_OK) {
        return status;
    }

    *profile = parsed;
    return AEGIS_OK;
}

static AegisStatus parse_policy(
    const cJSON *policy,
    AegisConfig *cfg
)
{
    AegisStatus status;
    cJSON *decisions;
    cJSON *risks;
    cJSON *decision;
    cJSON *risk;
    AegisPolicyDecision *entry;

    status = parse_string(
        policy, "approval_mode", cfg->approval_mode,
        sizeof(cfg->approval_mode), 1);
    if (status != AEGIS_OK) {
        return status;
    }
    status = parse_string(
        policy, "default_decision", cfg->default_decision,
        sizeof(cfg->default_decision), 1);
    if (status != AEGIS_OK) {
        return status;
    }

    decisions = required_object(policy, "decisions");
    risks = required_object(policy, "risk_levels");
    if (!decisions || !risks) {
        return AEGIS_ERR_PARSE;
    }

    cfg->policy_decision_count = 0;
    cJSON_ArrayForEach(decision, decisions) {
        if (cfg->policy_decision_count >=
                AEGIS_CONFIG_MAX_POLICY_DECISIONS ||
            !decision->string ||
            !cJSON_IsString(decision) ||
            !decision->valuestring) {
            return AEGIS_ERR_PARSE;
        }

        risk = cJSON_GetObjectItemCaseSensitive(risks, decision->string);
        if (!cJSON_IsString(risk) || !risk->valuestring) {
            return AEGIS_ERR_PARSE;
        }

        entry = &cfg->policy_decisions[cfg->policy_decision_count];
        if (!copy_string(entry->tool, sizeof(entry->tool), decision->string) ||
            !copy_string(
                entry->decision,
                sizeof(entry->decision),
                decision->valuestring) ||
            !copy_string(entry->risk, sizeof(entry->risk), risk->valuestring)) {
            return AEGIS_ERR_PARSE;
        }
        ++cfg->policy_decision_count;
    }

    return cfg->policy_decision_count > 0
        ? AEGIS_OK
        : AEGIS_ERR_PARSE;
}

static const AegisPolicyDecision *find_policy_decision(
    const AegisConfig *cfg,
    const char *tool
)
{
    size_t index;

    if (!cfg || !tool) {
        return NULL;
    }

    for (index = 0; index < cfg->policy_decision_count; ++index) {
        if (strcmp(cfg->policy_decisions[index].tool, tool) == 0) {
            return &cfg->policy_decisions[index];
        }
    }

    return NULL;
}

static AegisStatus validate_config(const AegisConfig *cfg)
{
    size_t index;

    if (strcmp(cfg->kind, "aegis_config") != 0 ||
        !valid_identifier(cfg->default_profile) ||
        cfg->profile_directory[0] == '\0' ||
        cfg->profile_file_suffix[0] == '\0' ||
        strcmp(cfg->tool_access_strategy, "intersection") != 0 ||
        strcmp(cfg->policy_authority, "config") != 0 ||
        strcmp(cfg->limit_strategy, "most_restrictive") != 0 ||
        strcmp(cfg->model_source, "config") != 0) {
        return AEGIS_ERR_PARSE;
    }

    for (index = 0; index < cfg->enabled_tools.count; ++index) {
        if (string_list_contains(
                &cfg->disabled_tools,
                cfg->enabled_tools.items[index])) {
            return AEGIS_ERR_PARSE;
        }

        if (!find_policy_decision(
                cfg,
                cfg->enabled_tools.items[index])) {
            return AEGIS_ERR_PARSE;
        }
    }

    if (string_list_contains(&cfg->enabled_tools, "shell") &&
        !cfg->shell_enabled) {
        return AEGIS_ERR_PARSE;
    }
    if ((string_list_contains(&cfg->enabled_tools, "http_get") ||
         string_list_contains(&cfg->enabled_tools, "http_post")) &&
        (!cfg->http_enabled || !cfg->allow_network)) {
        return AEGIS_ERR_PARSE;
    }
    if (string_list_contains(&cfg->enabled_tools, "mcp_tool") &&
        !cfg->mcp_enabled) {
        return AEGIS_ERR_PARSE;
    }

    return AEGIS_OK;
}

static AegisStatus parse_config(
    const cJSON *root,
    const char *path,
    AegisConfig *cfg
)
{
    AegisStatus status;
    AegisConfig parsed;
    cJSON *app;
    cJSON *runtime;
    cJSON *retry;
    cJSON *provider_root;
    cJSON *providers;
    cJSON *provider;
    cJSON *model_root;
    cJSON *model_profiles;
    cJSON *model;
    cJSON *agent;
    cJSON *resolution;
    cJSON *context;
    cJSON *workspace;
    cJSON *tools;
    cJSON *file_tools;
    cJSON *http_tools;
    cJSON *shell_tools;
    cJSON *policy;
    cJSON *sandbox;
    cJSON *network;
    cJSON *limits;
    cJSON *state;
    cJSON *trace;
    cJSON *logging;
    cJSON *redaction;
    cJSON *mcp;
    cJSON *evaluation;
    char app_default_profile[AEGIS_CONFIG_NAME_MAX];
    int trace_redacts_secrets;
    int redaction_enabled;

    aegis_config_defaults(&parsed);
    if (!copy_string(parsed.config_path, sizeof(parsed.config_path), path)) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }

#define CONFIG_PARSE(call)            \
    do {                              \
        status = (call);              \
        if (status != AEGIS_OK) {     \
            return status;            \
        }                             \
    } while (0)

    CONFIG_PARSE(parse_string(
        root, "schema_version", parsed.schema_version,
        sizeof(parsed.schema_version), 1));
    CONFIG_PARSE(parse_string(
        root, "kind", parsed.kind, sizeof(parsed.kind), 1));

    app = required_object(root, "app");
    runtime = required_object(root, "runtime");
    provider_root = required_object(root, "provider");
    model_root = required_object(root, "model");
    agent = required_object(root, "agent");
    context = required_object(root, "context");
    workspace = required_object(root, "workspace");
    tools = required_object(root, "tools");
    policy = required_object(root, "policy");
    sandbox = required_object(root, "sandbox");
    state = required_object(root, "state");
    trace = required_object(root, "trace");
    logging = required_object(root, "logging");
    redaction = required_object(root, "redaction");
    mcp = required_object(root, "mcp");
    evaluation = required_object(root, "eval");
    if (!app || !runtime || !provider_root || !model_root || !agent ||
        !context || !workspace || !tools || !policy || !sandbox || !state ||
        !trace || !logging || !redaction || !mcp || !evaluation) {
        return AEGIS_ERR_PARSE;
    }

    CONFIG_PARSE(parse_string(
        app, "name", parsed.app_name, sizeof(parsed.app_name), 1));
    CONFIG_PARSE(parse_string(
        app, "display_name", parsed.display_name,
        sizeof(parsed.display_name), 1));
    CONFIG_PARSE(parse_string(
        app, "mode", parsed.mode, sizeof(parsed.mode), 1));
    CONFIG_PARSE(parse_string(
        app, "default_profile", app_default_profile,
        sizeof(app_default_profile), 1));

    CONFIG_PARSE(parse_int(
        runtime, "max_steps", &parsed.max_steps, 1, 1, INT_MAX));
    CONFIG_PARSE(parse_int(
        runtime, "max_tool_calls", &parsed.max_tool_calls,
        1, 1, INT_MAX));
    CONFIG_PARSE(parse_int(
        runtime, "max_wall_time_ms", &parsed.max_wall_time_ms,
        1, 1, INT_MAX));
    CONFIG_PARSE(parse_bool(
        runtime, "stop_on_tool_error", &parsed.stop_on_tool_error, 1));
    CONFIG_PARSE(parse_bool(
        runtime, "stop_on_policy_denied",
        &parsed.stop_on_policy_denied, 1));
    retry = required_object(runtime, "retry");
    if (!retry) {
        return AEGIS_ERR_PARSE;
    }
    CONFIG_PARSE(parse_bool(
        retry, "enabled", &parsed.retry_enabled, 1));
    CONFIG_PARSE(parse_int(
        retry, "max_retries", &parsed.max_retries, 1, 0, INT_MAX));
    CONFIG_PARSE(parse_int(
        retry, "backoff_ms", &parsed.retry_backoff_ms,
        1, 0, INT_MAX));
    parsed.configured_max_steps = parsed.max_steps;
    parsed.configured_max_tool_calls = parsed.max_tool_calls;

    CONFIG_PARSE(parse_string(
        provider_root, "active", parsed.provider,
        sizeof(parsed.provider), 1));
    providers = required_object(provider_root, "providers");
    provider = providers
        ? cJSON_GetObjectItemCaseSensitive(providers, parsed.provider)
        : NULL;
    if (!cJSON_IsObject(provider)) {
        return AEGIS_ERR_PARSE;
    }
    CONFIG_PARSE(parse_string(
        provider, "base_url", parsed.provider_base_url,
        sizeof(parsed.provider_base_url), 1));
    CONFIG_PARSE(parse_string(
        provider, "chat_completions_path", parsed.chat_completions_path,
        sizeof(parsed.chat_completions_path), 1));
    CONFIG_PARSE(parse_string(
        provider, "api_key_env", parsed.api_key_env,
        sizeof(parsed.api_key_env), 1));
    CONFIG_PARSE(parse_int(
        provider, "timeout_ms", &parsed.provider_timeout_ms,
        1, 1, INT_MAX));
    CONFIG_PARSE(parse_int(
        provider, "connect_timeout_ms",
        &parsed.provider_connect_timeout_ms, 1, 1, INT_MAX));

    CONFIG_PARSE(parse_string(
        model_root, "active_profile", parsed.model_profile,
        sizeof(parsed.model_profile), 1));
    model_profiles = required_object(model_root, "profiles");
    model = model_profiles
        ? cJSON_GetObjectItemCaseSensitive(
            model_profiles,
            parsed.model_profile)
        : NULL;
    if (!cJSON_IsObject(model)) {
        return AEGIS_ERR_PARSE;
    }
    CONFIG_PARSE(parse_string(
        model, "model", parsed.model, sizeof(parsed.model), 1));
    CONFIG_PARSE(parse_double(
        model, "temperature", &parsed.temperature, 1, 0.0, 2.0));
    CONFIG_PARSE(parse_double(
        model, "top_p", &parsed.top_p, 1, 0.0, 1.0));
    CONFIG_PARSE(parse_int(
        model, "max_tokens", &parsed.max_tokens, 1, 1, INT_MAX));
    CONFIG_PARSE(parse_bool(model, "stream", &parsed.stream, 1));
    parsed.configured_temperature = parsed.temperature;
    parsed.configured_top_p = parsed.top_p;
    parsed.configured_max_tokens = parsed.max_tokens;

    CONFIG_PARSE(parse_string(
        agent, "profile_directory", parsed.profile_directory,
        sizeof(parsed.profile_directory), 1));
    CONFIG_PARSE(parse_string(
        agent, "default_profile", parsed.default_profile,
        sizeof(parsed.default_profile), 1));
    CONFIG_PARSE(parse_string(
        agent, "profile_file_suffix", parsed.profile_file_suffix,
        sizeof(parsed.profile_file_suffix), 1));
    CONFIG_PARSE(parse_string(
        agent, "action_protocol", parsed.action_protocol,
        sizeof(parsed.action_protocol), 1));
    CONFIG_PARSE(parse_string_list(
        agent, "allowed_actions", &parsed.allowed_actions,
        AEGIS_CONFIG_MAX_ACTIONS, 1));
    CONFIG_PARSE(parse_bool(
        agent, "force_json_action", &parsed.force_json_action, 1));
    CONFIG_PARSE(parse_int(
        agent, "invalid_json_repair_attempts",
        &parsed.invalid_json_repair_attempts, 1, 0, 100));
    if (strcmp(app_default_profile, parsed.default_profile) != 0) {
        return AEGIS_ERR_PARSE;
    }

    resolution = required_object(agent, "resolution");
    if (!resolution) {
        return AEGIS_ERR_PARSE;
    }
    CONFIG_PARSE(parse_string(
        resolution, "tool_access", parsed.tool_access_strategy,
        sizeof(parsed.tool_access_strategy), 1));
    CONFIG_PARSE(parse_string(
        resolution, "policy_authority", parsed.policy_authority,
        sizeof(parsed.policy_authority), 1));
    CONFIG_PARSE(parse_string(
        resolution, "limit_strategy", parsed.limit_strategy,
        sizeof(parsed.limit_strategy), 1));
    CONFIG_PARSE(parse_string(
        resolution, "model_source", parsed.model_source,
        sizeof(parsed.model_source), 1));
    CONFIG_PARSE(parse_string_list(
        resolution, "allowed_profile_model_overrides",
        &parsed.allowed_profile_model_overrides,
        AEGIS_CONFIG_MAX_TOOLS, 1));

    CONFIG_PARSE(parse_int(
        context, "max_history_events", &parsed.max_history_events,
        1, 1, INT_MAX));
    CONFIG_PARSE(parse_int(
        context, "max_file_read_bytes", &parsed.max_file_read_bytes,
        1, 1, INT_MAX));
    CONFIG_PARSE(parse_int(
        context, "max_tool_output_bytes", &parsed.max_tool_output_bytes,
        1, 1, INT_MAX));
    CONFIG_PARSE(parse_bool(
        context, "summarize_old_history",
        &parsed.summarize_old_history, 1));
    parsed.configured_max_history_events = parsed.max_history_events;

    CONFIG_PARSE(parse_string(
        workspace, "root", parsed.workspace_root,
        sizeof(parsed.workspace_root), 1));
    CONFIG_PARSE(parse_bool(
        workspace, "restrict_to_root", &parsed.restrict_to_root, 1));
    CONFIG_PARSE(parse_bool(
        workspace, "deny_absolute_paths", &parsed.deny_absolute_paths, 1));
    CONFIG_PARSE(parse_bool(
        workspace, "deny_parent_traversal",
        &parsed.deny_parent_traversal, 1));
    CONFIG_PARSE(parse_bool(
        workspace, "follow_symlinks", &parsed.follow_symlinks, 1));
    CONFIG_PARSE(parse_bool(
        workspace, "allow_hidden_files", &parsed.allow_hidden_files, 1));
    CONFIG_PARSE(parse_int(
        workspace, "max_file_bytes", &parsed.max_file_bytes,
        1, 1, INT_MAX));
    CONFIG_PARSE(parse_string_list(
        workspace, "blocked_paths", &parsed.blocked_paths,
        AEGIS_CONFIG_MAX_TOOLS, 1));

    CONFIG_PARSE(parse_string_list(
        tools, "enabled", &parsed.enabled_tools,
        AEGIS_CONFIG_MAX_TOOLS, 1));
    CONFIG_PARSE(parse_string_list(
        tools, "disabled", &parsed.disabled_tools,
        AEGIS_CONFIG_MAX_TOOLS, 1));
    file_tools = required_object(tools, "file");
    http_tools = required_object(tools, "http");
    shell_tools = required_object(tools, "shell");
    if (!file_tools || !http_tools || !shell_tools) {
        return AEGIS_ERR_PARSE;
    }
    CONFIG_PARSE(parse_bool(
        file_tools, "allow_read", &parsed.allow_file_read, 1));
    CONFIG_PARSE(parse_bool(
        file_tools, "allow_write", &parsed.allow_file_write, 1));
    CONFIG_PARSE(parse_bool(
        file_tools, "write_requires_approval",
        &parsed.write_requires_approval, 1));
    CONFIG_PARSE(parse_bool(
        http_tools, "enabled", &parsed.http_enabled, 1));
    CONFIG_PARSE(parse_int(
        http_tools, "timeout_ms", &parsed.http_timeout_ms,
        1, 1, INT_MAX));
    CONFIG_PARSE(parse_bool(
        shell_tools, "enabled", &parsed.shell_enabled, 1));
    CONFIG_PARSE(parse_bool(
        shell_tools, "requires_approval",
        &parsed.shell_requires_approval, 1));
    CONFIG_PARSE(parse_int(
        shell_tools, "timeout_ms", &parsed.shell_timeout_ms,
        1, 1, INT_MAX));
    parsed.allow_shell = parsed.shell_enabled;

    CONFIG_PARSE(parse_policy(policy, &parsed));

    CONFIG_PARSE(parse_bool(
        sandbox, "enabled", &parsed.sandbox_enabled, 1));
    network = required_object(sandbox, "network");
    limits = required_object(sandbox, "limits");
    if (!network || !limits) {
        return AEGIS_ERR_PARSE;
    }
    CONFIG_PARSE(parse_bool(
        network, "enabled", &parsed.allow_network, 1));
    CONFIG_PARSE(parse_int(
        limits, "cpu_time_seconds", &parsed.cpu_time_seconds,
        1, 1, INT_MAX));
    CONFIG_PARSE(parse_long_long(
        limits, "memory_bytes", &parsed.memory_bytes, 1, 1));
    CONFIG_PARSE(parse_long_long(
        limits, "file_size_bytes", &parsed.file_size_bytes, 1, 1));
    CONFIG_PARSE(parse_int(
        limits, "process_count", &parsed.process_count,
        1, 1, INT_MAX));

    CONFIG_PARSE(parse_bool(
        state, "enabled", &parsed.state_enabled, 1));
    CONFIG_PARSE(parse_string(
        state, "path", parsed.state_path, sizeof(parsed.state_path), 1));

    CONFIG_PARSE(parse_bool(
        trace, "enabled", &parsed.trace_enabled, 1));
    CONFIG_PARSE(parse_string(
        trace, "directory", parsed.trace_directory,
        sizeof(parsed.trace_directory), 1));
    CONFIG_PARSE(parse_bool(
        trace, "redact_secrets", &trace_redacts_secrets, 1));

    CONFIG_PARSE(parse_string(
        logging, "level", parsed.logging_level,
        sizeof(parsed.logging_level), 1));
    CONFIG_PARSE(parse_bool(
        redaction, "enabled", &redaction_enabled, 1));
    parsed.redact_secrets = trace_redacts_secrets && redaction_enabled;
    CONFIG_PARSE(parse_bool(
        mcp, "enabled", &parsed.mcp_enabled, 1));
    CONFIG_PARSE(parse_bool(
        evaluation, "enabled", &parsed.eval_enabled, 1));
    CONFIG_PARSE(parse_int(
        evaluation, "max_eval_steps", &parsed.max_eval_steps,
        1, 1, INT_MAX));

    CONFIG_PARSE(validate_config(&parsed));
    *cfg = parsed;
    return AEGIS_OK;

#undef CONFIG_PARSE
}

AegisStatus aegis_config_load_json(const char *path, AegisConfig *cfg)
{
    AegisStatus status;
    AegisConfig parsed;
    cJSON *root;

    if (!path || !cfg) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }

    status = load_json_root(path, &root);
    if (status != AEGIS_OK) {
        return status;
    }

    status = parse_config(root, path, &parsed);
    cJSON_Delete(root);
    if (status != AEGIS_OK) {
        return status;
    }

    status = aegis_config_load_profile(&parsed, parsed.default_profile);
    if (status != AEGIS_OK) {
        return status;
    }

    *cfg = parsed;
    return AEGIS_OK;
}

AegisStatus aegis_config_load_preset(
    const char *preset,
    AegisConfig *cfg
)
{
    char path[AEGIS_CONFIG_PATH_MAX];
    const char *selected;
    int written;

    if (!cfg) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }

    selected = preset && preset[0] != '\0' ? preset : "aegis";
    if (!valid_identifier(selected)) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }

    written = snprintf(path, sizeof(path), "config/%s.json", selected);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }

    return aegis_config_load_json(path, cfg);
}

AegisStatus aegis_config_load_profile(
    AegisConfig *cfg,
    const char *profile_id
)
{
    AegisStatus status;
    AegisAgentProfile profile;
    AegisConfig updated;
    char path[AEGIS_CONFIG_PATH_MAX];

    if (!cfg || !valid_identifier(profile_id)) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    if (!build_profile_path(cfg, profile_id, path, sizeof(path))) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }

    status = aegis_agent_profile_load_json(path, &profile);
    if (status != AEGIS_OK) {
        return status;
    }
    if (strcmp(profile.id, profile_id) != 0 ||
        strcmp(profile.action_protocol, cfg->action_protocol) != 0) {
        return AEGIS_ERR_PARSE;
    }

    updated = *cfg;
    updated.active_profile = profile;
    updated.max_steps = minimum_positive(
        updated.configured_max_steps,
        profile.max_steps
    );
    updated.max_tool_calls = minimum_positive(
        updated.configured_max_tool_calls,
        profile.max_tool_calls
    );
    updated.max_history_events = minimum_positive(
        updated.configured_max_history_events,
        profile.max_history_events
    );

    updated.temperature = updated.configured_temperature;
    updated.top_p = updated.configured_top_p;
    updated.max_tokens = updated.configured_max_tokens;
    if (string_list_contains(
            &updated.allowed_profile_model_overrides,
            "temperature")) {
        updated.temperature = profile.temperature;
    }
    if (string_list_contains(
            &updated.allowed_profile_model_overrides,
            "top_p")) {
        updated.top_p = profile.top_p;
    }
    if (string_list_contains(
            &updated.allowed_profile_model_overrides,
            "max_tokens")) {
        updated.max_tokens = minimum_positive(
            updated.configured_max_tokens,
            profile.max_tokens
        );
    }

    *cfg = updated;
    return AEGIS_OK;
}

int aegis_config_tool_enabled(
    const AegisConfig *cfg,
    const char *tool
)
{
    if (!cfg || !tool) {
        return 0;
    }

    return string_list_contains(&cfg->enabled_tools, tool) &&
        !string_list_contains(&cfg->disabled_tools, tool);
}

int aegis_config_profile_requests_tool(
    const AegisConfig *cfg,
    const char *tool
)
{
    if (!cfg || !tool) {
        return 0;
    }

    return string_list_contains(
        &cfg->active_profile.requested_tools,
        tool
    );
}

const char *aegis_config_tool_decision(
    const AegisConfig *cfg,
    const char *tool
)
{
    const AegisPolicyDecision *entry;

    if (!cfg || !tool) {
        return NULL;
    }

    entry = find_policy_decision(cfg, tool);
    if (entry) {
        return entry->decision;
    }

    return cfg->default_decision;
}

const char *aegis_config_tool_risk(
    const AegisConfig *cfg,
    const char *tool
)
{
    const AegisPolicyDecision *entry;

    if (!cfg || !tool) {
        return NULL;
    }

    entry = find_policy_decision(cfg, tool);
    return entry ? entry->risk : NULL;
}

int aegis_config_tool_is_effective(
    const AegisConfig *cfg,
    const char *tool
)
{
    const char *decision;

    if (!aegis_config_tool_enabled(cfg, tool) ||
        !aegis_config_profile_requests_tool(cfg, tool)) {
        return 0;
    }

    decision = aegis_config_tool_decision(cfg, tool);
    if (!decision || strcmp(decision, "deny") == 0 ||
        strcmp(decision, "deny_unknown") == 0) {
        return 0;
    }

    if (strcmp(tool, "read_file") == 0 && !cfg->allow_file_read) {
        return 0;
    }
    if ((strcmp(tool, "write_file") == 0 ||
         strcmp(tool, "append_file") == 0) &&
        !cfg->allow_file_write) {
        return 0;
    }
    if (strcmp(tool, "shell") == 0 && !cfg->shell_enabled) {
        return 0;
    }
    if ((strcmp(tool, "http_get") == 0 ||
         strcmp(tool, "http_post") == 0) &&
        (!cfg->http_enabled || !cfg->allow_network)) {
        return 0;
    }
    if (strcmp(tool, "mcp_tool") == 0 && !cfg->mcp_enabled) {
        return 0;
    }

    return 1;
}

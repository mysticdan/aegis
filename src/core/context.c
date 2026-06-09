#define _XOPEN_SOURCE 700

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "aegis/context.h"

static int valid_role(AegisContextRole role)
{
    return role >= AEGIS_CONTEXT_ROLE_SYSTEM &&
        role <= AEGIS_CONTEXT_ROLE_TOOL;
}

static int valid_kind(AegisContextEventKind kind)
{
    return kind >= AEGIS_CONTEXT_EVENT_MESSAGE &&
        kind <= AEGIS_CONTEXT_EVENT_FILE_READ;
}

static char *duplicate_prefix(const char *value, size_t length)
{
    char *copy;

    if (!value) {
        return NULL;
    }

    copy = malloc(length + 1U);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, value, length);
    copy[length] = '\0';
    return copy;
}

static char *duplicate_string(const char *value)
{
    return value ? duplicate_prefix(value, strlen(value)) : NULL;
}

static size_t utf8_prefix_length(const char *value, size_t limit)
{
    size_t length;
    size_t prefix;

    length = strlen(value);
    if (length <= limit) {
        return length;
    }

    prefix = limit;
    while (prefix > 0U &&
           (((unsigned char)value[prefix] & 0xc0U) == 0x80U)) {
        --prefix;
    }
    return prefix;
}

static int add_size(size_t *total, size_t value)
{
    if (*total > SIZE_MAX - value) {
        return 0;
    }
    *total += value;
    return 1;
}

static int string_cost(const char *value, size_t *cost)
{
    return !value || add_size(cost, strlen(value));
}

static int message_cost(
    const char *name,
    const char *path,
    const char *content,
    size_t *cost
)
{
    *cost = 0U;
    return string_cost(name, cost) &&
        string_cost(path, cost) &&
        string_cost(content, cost);
}

static void clear_message(AegisContextMessage *message)
{
    if (!message) {
        return;
    }
    free(message->name);
    free(message->path);
    free(message->content);
    memset(message, 0, sizeof(*message));
}

static void clear_tool(AegisContextTool *tool)
{
    if (!tool) {
        return;
    }
    free(tool->name);
    free(tool->description);
    free(tool->schema_json);
    free(tool->policy_decision);
    free(tool->risk);
    memset(tool, 0, sizeof(*tool));
}

void aegis_context_init(AegisContext *context)
{
    if (context) {
        memset(context, 0, sizeof(*context));
    }
}

void aegis_context_clear(AegisContext *context)
{
    size_t index;

    if (!context) {
        return;
    }

    for (index = 0; index < context->message_count; ++index) {
        clear_message(&context->messages[index]);
    }
    for (index = 0; index < context->tool_count; ++index) {
        clear_tool(&context->tools[index]);
    }
    free(context->messages);
    free(context->tools);
    memset(context, 0, sizeof(*context));
}

static AegisStatus reserve_messages(AegisContext *context, size_t count)
{
    AegisContextMessage *resized;

    if (count > SIZE_MAX / sizeof(*resized)) {
        return AEGIS_ERR_OOM;
    }
    resized = realloc(context->messages, count * sizeof(*resized));
    if (!resized) {
        return AEGIS_ERR_OOM;
    }
    context->messages = resized;
    return AEGIS_OK;
}

static AegisStatus reserve_tools(AegisContext *context, size_t count)
{
    AegisContextTool *resized;

    if (count > SIZE_MAX / sizeof(*resized)) {
        return AEGIS_ERR_OOM;
    }
    resized = realloc(context->tools, count * sizeof(*resized));
    if (!resized) {
        return AEGIS_ERR_OOM;
    }
    context->tools = resized;
    return AEGIS_OK;
}

static AegisStatus append_message(
    AegisContext *context,
    AegisContextRole role,
    AegisContextEventKind kind,
    const char *name,
    const char *path,
    const char *content
)
{
    AegisContextMessage message;
    AegisStatus status;
    size_t cost;

    if (!content || !valid_role(role) || !valid_kind(kind) ||
        !message_cost(name, path, content, &cost)) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }

    memset(&message, 0, sizeof(message));
    message.role = role;
    message.kind = kind;
    message.name = duplicate_string(name);
    message.path = duplicate_string(path);
    message.content = duplicate_string(content);
    if ((name && !message.name) || (path && !message.path) ||
        !message.content) {
        clear_message(&message);
        return AEGIS_ERR_OOM;
    }

    status = reserve_messages(context, context->message_count + 1U);
    if (status != AEGIS_OK) {
        clear_message(&message);
        return status;
    }

    context->messages[context->message_count++] = message;
    if (!add_size(&context->total_chars, cost)) {
        --context->message_count;
        clear_message(&message);
        return AEGIS_ERR_OOM;
    }
    return AEGIS_OK;
}

static AegisStatus append_tool(
    AegisContext *context,
    const AegisTool *tool,
    const char *decision,
    const char *risk
)
{
    AegisContextTool output;
    AegisStatus status;
    size_t cost = 0U;

    if (!tool || !tool->name || !tool->description || !tool->schema_json ||
        !decision || !risk ||
        !string_cost(tool->name, &cost) ||
        !string_cost(tool->description, &cost) ||
        !string_cost(tool->schema_json, &cost) ||
        !string_cost(decision, &cost) ||
        !string_cost(risk, &cost)) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }

    memset(&output, 0, sizeof(output));
    output.name = duplicate_string(tool->name);
    output.description = duplicate_string(tool->description);
    output.schema_json = duplicate_string(tool->schema_json);
    output.policy_decision = duplicate_string(decision);
    output.risk = duplicate_string(risk);
    if (!output.name || !output.description || !output.schema_json ||
        !output.policy_decision || !output.risk) {
        clear_tool(&output);
        return AEGIS_ERR_OOM;
    }

    status = reserve_tools(context, context->tool_count + 1U);
    if (status != AEGIS_OK) {
        clear_tool(&output);
        return status;
    }

    context->tools[context->tool_count++] = output;
    if (!add_size(&context->total_chars, cost)) {
        --context->tool_count;
        clear_tool(&output);
        return AEGIS_ERR_OOM;
    }
    return AEGIS_OK;
}

static int path_has_parent_component(const char *path)
{
    const char *cursor = path;

    while (*cursor != '\0') {
        const char *start;
        size_t length;

        while (*cursor == '/') {
            ++cursor;
        }
        start = cursor;
        while (*cursor != '\0' && *cursor != '/') {
            ++cursor;
        }
        length = (size_t)(cursor - start);
        if (length == 2U && start[0] == '.' && start[1] == '.') {
            return 1;
        }
    }
    return 0;
}

static int path_is_within(const char *root, const char *path)
{
    size_t root_length = strlen(root);

    if (strcmp(root, path) == 0) {
        return 1;
    }
    if (root_length == 1U && root[0] == '/') {
        return path[0] == '/';
    }
    return strncmp(root, path, root_length) == 0 &&
        path[root_length] == '/';
}

static AegisStatus derive_project_root(
    const char *config_path,
    char *root,
    size_t root_size
)
{
    char directory[AEGIS_CONFIG_PATH_MAX];
    char *resolved;
    char *separator;
    char *base;
    size_t length;

    if (!config_path || config_path[0] == '\0' || !root || root_size == 0U ||
        strlen(config_path) >= sizeof(directory)) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }

    memcpy(directory, config_path, strlen(config_path) + 1U);
    separator = strrchr(directory, '/');
    if (!separator) {
        memcpy(directory, ".", 2U);
    } else if (separator == directory) {
        separator[1] = '\0';
    } else {
        *separator = '\0';
    }

    base = strrchr(directory, '/');
    base = base ? base + 1 : directory;
    if (strcmp(base, "config") == 0) {
        if (base == directory) {
            memcpy(directory, ".", 2U);
        } else {
            base[-1] = '\0';
            if (directory[0] == '\0') {
                memcpy(directory, "/", 2U);
            }
        }
    }

    resolved = realpath(directory, NULL);
    if (!resolved) {
        return errno == ENOENT ? AEGIS_ERR_NOT_FOUND : AEGIS_ERR_IO;
    }
    length = strlen(resolved);
    if (length >= root_size) {
        free(resolved);
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    memcpy(root, resolved, length + 1U);
    free(resolved);
    return AEGIS_OK;
}

static AegisStatus load_system_prompt(
    const AegisConfig *config,
    size_t limit,
    char **content
)
{
    char project_root[AEGIS_CONFIG_PATH_MAX];
    char joined[AEGIS_CONFIG_PATH_MAX * 2U];
    char *resolved;
    const char *prompt_path;
    struct stat metadata;
    AegisStatus status;
    FILE *file;
    char *buffer;
    size_t size;
    int written;

    *content = NULL;
    prompt_path = config->active_profile.prompt_path;
    if (prompt_path[0] == '\0') {
        return AEGIS_ERR_NOT_FOUND;
    }
    if (prompt_path[0] == '/' || path_has_parent_component(prompt_path)) {
        return AEGIS_ERR_PATH_ESCAPE;
    }

    status = derive_project_root(
        config->config_path,
        project_root,
        sizeof(project_root)
    );
    if (status != AEGIS_OK) {
        return status;
    }

    written = snprintf(
        joined,
        sizeof(joined),
        "%s/%s",
        project_root,
        prompt_path
    );
    if (written < 0 || (size_t)written >= sizeof(joined)) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    resolved = realpath(joined, NULL);
    if (!resolved) {
        return errno == ENOENT ? AEGIS_ERR_NOT_FOUND : AEGIS_ERR_IO;
    }
    if (!path_is_within(project_root, resolved)) {
        free(resolved);
        return AEGIS_ERR_PATH_ESCAPE;
    }
    if (stat(resolved, &metadata) != 0) {
        free(resolved);
        return AEGIS_ERR_IO;
    }
    if (!S_ISREG(metadata.st_mode)) {
        free(resolved);
        return AEGIS_ERR_IO;
    }
    if (metadata.st_size < 0 || (uintmax_t)metadata.st_size > limit) {
        free(resolved);
        return AEGIS_ERR_RUNTIME;
    }

    size = (size_t)metadata.st_size;
    buffer = malloc(size + 1U);
    if (!buffer) {
        free(resolved);
        return AEGIS_ERR_OOM;
    }

    file = fopen(resolved, "rb");
    free(resolved);
    if (!file) {
        free(buffer);
        return AEGIS_ERR_IO;
    }
    if (size > 0U && fread(buffer, 1U, size, file) != size) {
        fclose(file);
        free(buffer);
        return AEGIS_ERR_IO;
    }
    if (fclose(file) != 0) {
        free(buffer);
        return AEGIS_ERR_IO;
    }
    buffer[size] = '\0';
    *content = buffer;
    return AEGIS_OK;
}

static int event_is_included(
    const AegisConfig *config,
    const AegisContextEvent *event
)
{
    switch (event->kind) {
        case AEGIS_CONTEXT_EVENT_MESSAGE:
            return 1;
        case AEGIS_CONTEXT_EVENT_OBSERVATION:
            return config->include_recent_observations &&
                config->active_profile.include_recent_observations;
        case AEGIS_CONTEXT_EVENT_FILE_READ:
            return config->include_recent_file_reads &&
                config->active_profile.include_recent_file_reads;
        default:
            return 0;
    }
}

static size_t event_content_limit(
    const AegisConfig *config,
    const AegisContextEvent *event
)
{
    size_t profile_limit =
        (size_t)config->active_profile.max_observation_chars;
    size_t config_limit;

    if (event->kind == AEGIS_CONTEXT_EVENT_OBSERVATION) {
        config_limit = (size_t)config->max_tool_output_bytes;
    } else if (event->kind == AEGIS_CONTEXT_EVENT_FILE_READ) {
        config_limit = (size_t)config->max_file_read_bytes;
    } else {
        return SIZE_MAX;
    }
    return profile_limit < config_limit ? profile_limit : config_limit;
}

static AegisStatus copy_history_event(
    const AegisConfig *config,
    const AegisContextEvent *event,
    AegisContextMessage *message,
    int *truncated
)
{
    size_t source_length;
    size_t copied_length;

    memset(message, 0, sizeof(*message));
    source_length = strlen(event->content);
    copied_length = utf8_prefix_length(
        event->content,
        event_content_limit(config, event)
    );

    message->role = event->role;
    message->kind = event->kind;
    message->name = duplicate_string(event->name);
    message->path = duplicate_string(event->path);
    message->content = duplicate_prefix(event->content, copied_length);
    if ((event->name && !message->name) ||
        (event->path && !message->path) ||
        !message->content) {
        clear_message(message);
        return AEGIS_ERR_OOM;
    }
    if (copied_length < source_length) {
        *truncated = 1;
    }
    return AEGIS_OK;
}

static void reverse_messages(AegisContextMessage *messages, size_t count)
{
    size_t left = 0U;
    size_t right = count;

    while (left < right && left < --right) {
        AegisContextMessage temporary = messages[left];
        messages[left] = messages[right];
        messages[right] = temporary;
        ++left;
    }
}

static void clear_message_array(
    AegisContextMessage *messages,
    size_t count
)
{
    size_t index;

    for (index = 0U; index < count; ++index) {
        clear_message(&messages[index]);
    }
    free(messages);
}

static AegisStatus collect_history(
    const AegisConfig *config,
    const AegisContextBuildInput *input,
    AegisContextMessage **messages,
    size_t *count,
    size_t *dropped,
    int *truncated
)
{
    AegisContextMessage *selected;
    size_t maximum;
    size_t selected_count = 0U;
    size_t index;

    *messages = NULL;
    *count = 0U;
    maximum = (size_t)config->max_history_events;
    if (input->history_count == 0U) {
        return AEGIS_OK;
    }

    selected = calloc(
        input->history_count < maximum ? input->history_count : maximum,
        sizeof(*selected)
    );
    if (!selected) {
        return AEGIS_ERR_OOM;
    }

    index = input->history_count;
    while (index > 0U) {
        const AegisContextEvent *event = &input->history[--index];
        AegisStatus status;

        if (!valid_role(event->role) || !valid_kind(event->kind) ||
            !event->content) {
            clear_message_array(selected, selected_count);
            return AEGIS_ERR_INVALID_ARGUMENT;
        }
        if (!event_is_included(config, event)) {
            continue;
        }
        if (selected_count >= maximum) {
            ++*dropped;
            *truncated = 1;
            continue;
        }

        status = copy_history_event(
            config,
            event,
            &selected[selected_count],
            truncated
        );
        if (status != AEGIS_OK) {
            clear_message_array(selected, selected_count);
            return status;
        }
        ++selected_count;
    }

    reverse_messages(selected, selected_count);
    *messages = selected;
    *count = selected_count;
    return AEGIS_OK;
}

static AegisStatus append_effective_tools(
    AegisContext *context,
    const AegisConfig *config,
    const AegisToolRegistry *registry
)
{
    size_t index;

    if (!config->include_tool_schemas ||
        !config->active_profile.include_tool_schemas) {
        return AEGIS_OK;
    }

    for (index = 0U; index < registry->count; ++index) {
        const AegisTool *tool = &registry->tools[index];
        const char *decision;
        const char *risk;
        AegisStatus status;

        if (tool->availability != AEGIS_TOOL_READY ||
            !aegis_config_tool_is_effective(config, tool->name)) {
            continue;
        }
        decision = aegis_config_tool_decision(config, tool->name);
        risk = aegis_config_tool_risk(config, tool->name);
        if (!decision || !risk) {
            return AEGIS_ERR_PARSE;
        }
        status = append_tool(context, tool, decision, risk);
        if (status != AEGIS_OK) {
            return status;
        }
    }
    return AEGIS_OK;
}

static AegisStatus append_history_with_budget(
    AegisContext *context,
    AegisContextMessage *history,
    size_t history_count,
    size_t current_cost,
    size_t maximum
)
{
    size_t first = 0U;
    size_t index;
    size_t history_cost = 0U;

    for (index = 0U; index < history_count; ++index) {
        size_t cost;

        if (!message_cost(
                history[index].name,
                history[index].path,
                history[index].content,
                &cost) ||
            !add_size(&history_cost, cost)) {
            return AEGIS_ERR_OOM;
        }
    }

    while (first < history_count &&
           (history_cost > maximum - context->total_chars - current_cost)) {
        size_t cost;

        if (!message_cost(
                history[first].name,
                history[first].path,
                history[first].content,
                &cost)) {
            return AEGIS_ERR_OOM;
        }
        history_cost -= cost;
        ++first;
        ++context->dropped_history_count;
        context->truncated = 1;
    }

    for (index = first; index < history_count; ++index) {
        AegisStatus status = append_message(
            context,
            history[index].role,
            history[index].kind,
            history[index].name,
            history[index].path,
            history[index].content
        );
        if (status != AEGIS_OK) {
            return status;
        }
    }
    return AEGIS_OK;
}

AegisStatus aegis_context_build(
    AegisContext *out,
    const AegisConfig *config,
    const AegisToolRegistry *registry,
    const AegisContextBuildInput *input
)
{
    static const char system_name[] = "system_prompt";
    static const char workspace_name[] = "workspace_summary";
    static const char history_name[] = "history_summary";
    AegisContext built;
    AegisContextMessage *history = NULL;
    size_t history_count = 0U;
    size_t maximum;
    size_t current_cost;
    size_t mandatory_cost;
    char *system_prompt = NULL;
    int include_workspace;
    AegisStatus status;

    if (!out || !config || !registry || !input ||
        !aegis_message_is_valid(input->current_message) ||
        (input->history_count > 0U && !input->history) ||
        config->active_profile.max_context_chars <= 0 ||
        config->max_history_events <= 0 ||
        config->active_profile.max_observation_chars <= 0 ||
        config->max_file_read_bytes <= 0 ||
        config->max_tool_output_bytes <= 0) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }

    maximum = (size_t)config->active_profile.max_context_chars;
    aegis_context_init(&built);
    status = append_effective_tools(&built, config, registry);
    if (status != AEGIS_OK) {
        goto fail;
    }

    if (config->include_system_prompt &&
        config->active_profile.include_system_prompt) {
        status = load_system_prompt(config, maximum, &system_prompt);
        if (status != AEGIS_OK) {
            goto fail;
        }
    }

    include_workspace =
        config->include_workspace_summary &&
        config->active_profile.include_workspace_summary &&
        input->workspace_summary &&
        input->workspace_summary[0] != '\0';

    mandatory_cost = built.total_chars;
    if (system_prompt &&
        (!message_cost(system_name, NULL, system_prompt, &current_cost) ||
         !add_size(&mandatory_cost, current_cost))) {
        status = AEGIS_ERR_OOM;
        goto fail;
    }
    if (include_workspace &&
        (!message_cost(
            workspace_name,
            NULL,
            input->workspace_summary,
            &current_cost) ||
         !add_size(&mandatory_cost, current_cost))) {
        status = AEGIS_ERR_OOM;
        goto fail;
    }
    if (!message_cost(
            NULL,
            NULL,
            input->current_message->text,
            &current_cost) ||
        !add_size(&mandatory_cost, current_cost)) {
        status = AEGIS_ERR_OOM;
        goto fail;
    }
    if (mandatory_cost > maximum) {
        status = AEGIS_ERR_RUNTIME;
        goto fail;
    }

    if (system_prompt) {
        status = append_message(
            &built,
            AEGIS_CONTEXT_ROLE_SYSTEM,
            AEGIS_CONTEXT_EVENT_MESSAGE,
            system_name,
            NULL,
            system_prompt
        );
        if (status != AEGIS_OK) {
            goto fail;
        }
    }
    if (include_workspace) {
        status = append_message(
            &built,
            AEGIS_CONTEXT_ROLE_SYSTEM,
            AEGIS_CONTEXT_EVENT_MESSAGE,
            workspace_name,
            NULL,
            input->workspace_summary
        );
        if (status != AEGIS_OK) {
            goto fail;
        }
    }

    if (input->history_summary &&
        input->history_summary[0] != '\0' &&
        config->summarize_old_history &&
        config->active_profile.summarize_old_history) {
        size_t summary_cost;

        if (!message_cost(
                history_name,
                NULL,
                input->history_summary,
                &summary_cost)) {
            status = AEGIS_ERR_OOM;
            goto fail;
        }
        if (summary_cost <= maximum - built.total_chars - current_cost) {
            status = append_message(
                &built,
                AEGIS_CONTEXT_ROLE_SYSTEM,
                AEGIS_CONTEXT_EVENT_MESSAGE,
                history_name,
                NULL,
                input->history_summary
            );
            if (status != AEGIS_OK) {
                goto fail;
            }
        } else {
            built.truncated = 1;
        }
    }

    status = collect_history(
        config,
        input,
        &history,
        &history_count,
        &built.dropped_history_count,
        &built.truncated
    );
    if (status != AEGIS_OK) {
        goto fail;
    }
    status = append_history_with_budget(
        &built,
        history,
        history_count,
        current_cost,
        maximum
    );
    if (status != AEGIS_OK) {
        goto fail;
    }

    status = append_message(
        &built,
        AEGIS_CONTEXT_ROLE_USER,
        AEGIS_CONTEXT_EVENT_MESSAGE,
        NULL,
        NULL,
        input->current_message->text
    );
    if (status != AEGIS_OK) {
        goto fail;
    }
    if (built.total_chars > maximum) {
        status = AEGIS_ERR_RUNTIME;
        goto fail;
    }

    free(system_prompt);
    clear_message_array(history, history_count);
    aegis_context_clear(out);
    *out = built;
    return AEGIS_OK;

fail:
    free(system_prompt);
    clear_message_array(history, history_count);
    aegis_context_clear(&built);
    return status;
}

const char *aegis_context_role_name(AegisContextRole role)
{
    switch (role) {
        case AEGIS_CONTEXT_ROLE_SYSTEM:
            return "system";
        case AEGIS_CONTEXT_ROLE_USER:
            return "user";
        case AEGIS_CONTEXT_ROLE_ASSISTANT:
            return "assistant";
        case AEGIS_CONTEXT_ROLE_TOOL:
            return "tool";
        default:
            return NULL;
    }
}

const char *aegis_context_event_kind_name(AegisContextEventKind kind)
{
    switch (kind) {
        case AEGIS_CONTEXT_EVENT_MESSAGE:
            return "message";
        case AEGIS_CONTEXT_EVENT_OBSERVATION:
            return "observation";
        case AEGIS_CONTEXT_EVENT_FILE_READ:
            return "file_read";
        default:
            return NULL;
    }
}

#include <stdlib.h>
#include <string.h>

#include "aegis/str.h"
#include "aegis/tool_registry.h"

static int config_list_contains(
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

void aegis_tool_context_from_config(
    AegisToolContext *context,
    const AegisConfig *config,
    const char *workspace_root,
    int approval_granted
)
{
    if (!context) {
        return;
    }

    memset(context, 0, sizeof(*context));
    context->config = config;
    context->workspace_root = workspace_root
        ? workspace_root
        : (config ? config->workspace_root : ".");
    context->approval_granted = approval_granted != 0;
    if (!config) {
        return;
    }

    context->allow_write = config->allow_file_write;
    context->allow_shell = config->shell_enabled;
    context->allow_network = config->allow_network;
    context->max_output_bytes = (size_t)config->max_tool_output_bytes;
}

const char *aegis_tool_args_get(const AegisToolArgs *args, const char *key)
{
    size_t index;

    if (!args || !key) {
        return NULL;
    }

    for (index = 0; index < args->count; ++index) {
        if (args->items[index].key &&
            strcmp(args->items[index].key, key) == 0) {
            return args->items[index].value;
        }
    }

    return NULL;
}

void aegis_tool_result_init(AegisToolResult *result)
{
    if (result) {
        memset(result, 0, sizeof(*result));
    }
}

void aegis_tool_result_clear(AegisToolResult *result)
{
    if (!result) {
        return;
    }

    free(result->stdout_data);
    free(result->stderr_data);
    free(result->error_message);
    memset(result, 0, sizeof(*result));
}

AegisStatus aegis_tool_result_set_stdout(
    AegisToolResult *result,
    const char *text
)
{
    if (!result) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }

    free(result->stdout_data);
    free(result->error_message);
    result->stdout_data = aegis_strdup(text ? text : "");
    result->error_message = NULL;
    result->ok = result->stdout_data != NULL;
    result->exit_code = result->ok ? 0 : 1;
    result->output_bytes = result->stdout_data
        ? strlen(result->stdout_data)
        : 0;
    return result->stdout_data ? AEGIS_OK : AEGIS_ERR_OOM;
}

AegisStatus aegis_tool_result_set_error(
    AegisToolResult *result,
    const char *text
)
{
    if (!result) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }

    free(result->stdout_data);
    free(result->error_message);
    result->stdout_data = NULL;
    result->error_message = aegis_strdup(text ? text : "");
    result->ok = 0;
    result->exit_code = 1;
    result->output_bytes = 0;
    return result->error_message ? AEGIS_OK : AEGIS_ERR_OOM;
}

const char *aegis_tool_risk_name(AegisRiskLevel risk_level)
{
    switch (risk_level) {
        case AEGIS_RISK_LOW:
            return "low";
        case AEGIS_RISK_MEDIUM:
            return "medium";
        case AEGIS_RISK_HIGH:
            return "high";
        case AEGIS_RISK_CRITICAL:
            return "critical";
        default:
            return NULL;
    }
}

void aegis_tool_registry_init(AegisToolRegistry *registry)
{
    if (registry) {
        memset(registry, 0, sizeof(*registry));
    }
}

AegisStatus aegis_tool_registry_register(
    AegisToolRegistry *registry,
    AegisTool tool
)
{
    if (!registry || !tool.name || !tool.description ||
        !tool.schema_json || !tool.execute ||
        !aegis_tool_risk_name(tool.risk_level)) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    if (aegis_tool_registry_find(registry, tool.name)) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    if (registry->count >= AEGIS_TOOL_REGISTRY_MAX) {
        return AEGIS_ERR_RUNTIME;
    }

    registry->tools[registry->count++] = tool;
    return AEGIS_OK;
}

const AegisTool *aegis_tool_registry_find(
    const AegisToolRegistry *registry,
    const char *name
)
{
    size_t index;

    if (!registry || !name) {
        return NULL;
    }

    for (index = 0; index < registry->count; ++index) {
        if (strcmp(registry->tools[index].name, name) == 0) {
            return &registry->tools[index];
        }
    }

    return NULL;
}

AegisStatus aegis_tool_registry_validate_profile(
    const AegisToolRegistry *registry,
    const AegisAgentProfile *profile
)
{
    size_t index;

    if (!registry || !profile) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }

    for (index = 0; index < profile->requested_tools.count; ++index) {
        if (!aegis_tool_registry_find(
                registry,
                profile->requested_tools.items[index])) {
            return AEGIS_ERR_PARSE;
        }
    }

    return AEGIS_OK;
}

AegisStatus aegis_tool_registry_validate_config(
    const AegisToolRegistry *registry,
    const AegisConfig *config
)
{
    size_t index;
    const AegisTool *tool;
    const char *risk;
    int enabled;
    int disabled;

    if (!registry || !config) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    if (registry->count != AEGIS_TOOL_COUNT ||
        config->enabled_tools.count + config->disabled_tools.count !=
            AEGIS_TOOL_COUNT) {
        return AEGIS_ERR_PARSE;
    }

    for (index = 0; index < registry->count; ++index) {
        tool = &registry->tools[index];
        enabled = config_list_contains(&config->enabled_tools, tool->name);
        disabled = config_list_contains(&config->disabled_tools, tool->name);
        if (enabled == disabled) {
            return AEGIS_ERR_PARSE;
        }
        if (!aegis_config_tool_decision(config, tool->name)) {
            return AEGIS_ERR_PARSE;
        }

        risk = aegis_config_tool_risk(config, tool->name);
        if (!risk || strcmp(risk, aegis_tool_risk_name(tool->risk_level)) != 0) {
            return AEGIS_ERR_PARSE;
        }
        if (enabled && tool->availability != AEGIS_TOOL_READY) {
            return AEGIS_ERR_PARSE;
        }
    }

    for (index = 0; index < config->enabled_tools.count; ++index) {
        if (!aegis_tool_registry_find(
                registry,
                config->enabled_tools.items[index])) {
            return AEGIS_ERR_PARSE;
        }
    }
    for (index = 0; index < config->disabled_tools.count; ++index) {
        if (!aegis_tool_registry_find(
                registry,
                config->disabled_tools.items[index])) {
            return AEGIS_ERR_PARSE;
        }
    }

    return aegis_tool_registry_validate_profile(
        registry,
        &config->active_profile
    );
}

AegisStatus aegis_tool_registry_execute(
    const AegisToolRegistry *registry,
    const char *name,
    const AegisToolArgs *args,
    const AegisToolContext *context,
    AegisToolResult *out
)
{
    const AegisTool *tool;
    const char *decision;
    const char *risk;
    AegisStatus status;

    if (!registry || !name || !context || !context->config || !out) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }

    tool = aegis_tool_registry_find(registry, name);
    if (!tool) {
        return AEGIS_ERR_NOT_FOUND;
    }
    if (tool->availability != AEGIS_TOOL_READY) {
        aegis_tool_result_set_error(out, "tool is registered but not implemented");
        return AEGIS_ERR_NOT_IMPLEMENTED;
    }
    if (!aegis_config_tool_is_effective(context->config, name)) {
        aegis_tool_result_set_error(out, "tool denied by config or profile");
        return AEGIS_ERR_POLICY_DENIED;
    }

    risk = aegis_config_tool_risk(context->config, name);
    if (!risk || strcmp(risk, aegis_tool_risk_name(tool->risk_level)) != 0) {
        aegis_tool_result_set_error(out, "tool risk does not match config");
        return AEGIS_ERR_PARSE;
    }

    decision = aegis_config_tool_decision(context->config, name);
    if (!decision || strcmp(decision, "deny") == 0 ||
        strcmp(decision, "deny_unknown") == 0) {
        aegis_tool_result_set_error(out, "tool denied by policy");
        return AEGIS_ERR_POLICY_DENIED;
    }
    if (strcmp(decision, "require_approval") == 0 &&
        !context->approval_granted) {
        aegis_tool_result_set_error(out, "tool requires approval");
        return AEGIS_ERR_POLICY_DENIED;
    }
    if (strcmp(decision, "allow") != 0 &&
        strcmp(decision, "require_approval") != 0) {
        aegis_tool_result_set_error(out, "unknown policy decision");
        return AEGIS_ERR_PARSE;
    }

    status = tool->execute(args, context, out);
    if (status == AEGIS_OK && context->max_output_bytes > 0 &&
        out->output_bytes > context->max_output_bytes) {
        aegis_tool_result_set_error(out, "tool output exceeds configured limit");
        return AEGIS_ERR_RUNTIME;
    }

    return status;
}

AegisStatus aegis_tool_registry_register_defaults(
    AegisToolRegistry *registry
)
{
    AegisTool tools[AEGIS_TOOL_COUNT];
    size_t index;
    AegisStatus status;

    if (!registry) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }

    tools[0] = aegis_tool_list_dir();
    tools[1] = aegis_tool_read_file();
    tools[2] = aegis_tool_write_file();
    tools[3] = aegis_tool_append_file();
    tools[4] = aegis_tool_search_file();
    tools[5] = aegis_tool_shell();
    tools[6] = aegis_tool_run_tests();
    tools[7] = aegis_tool_git_status();
    tools[8] = aegis_tool_git_diff();
    tools[9] = aegis_tool_git_log();
    tools[10] = aegis_tool_git_apply_patch();
    tools[11] = aegis_tool_http_get();
    tools[12] = aegis_tool_http_post();
    tools[13] = aegis_tool_ask_user();
    tools[14] = aegis_tool_send_message();
    tools[15] = aegis_tool_reminder();
    tools[16] = aegis_tool_read_log();
    tools[17] = aegis_tool_grep_log();
    tools[18] = aegis_tool_health_check();
    tools[19] = aegis_tool_mcp_tool();

    for (index = 0; index < AEGIS_TOOL_COUNT; ++index) {
        status = aegis_tool_registry_register(registry, tools[index]);
        if (status != AEGIS_OK) {
            return status;
        }
    }

    return AEGIS_OK;
}

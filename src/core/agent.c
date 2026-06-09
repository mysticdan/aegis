#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#include "aegis/action.h"
#include "aegis/agent.h"
#include "aegis/context.h"
#include "aegis/provider.h"
#include "aegis/str.h"
#include "aegis/tool_registry.h"

typedef struct {
    AegisContextEvent event;
    char *name;
    char *content;
} OwnedEvent;

static long monotonic_milliseconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return now.tv_sec * 1000L + now.tv_nsec / 1000000L;
}

static void clear_events(OwnedEvent *events, size_t count)
{
    size_t index;

    if (!events) {
        return;
    }
    for (index = 0U; index < count; ++index) {
        free(events[index].name);
        free(events[index].content);
    }
    free(events);
}

static AegisStatus append_event(
    OwnedEvent *events,
    size_t capacity,
    size_t *count,
    AegisContextRole role,
    AegisContextEventKind kind,
    const char *name,
    const char *content
)
{
    OwnedEvent *target;

    if (!events || !count || *count >= capacity || !content) {
        return AEGIS_ERR_RUNTIME;
    }
    target = &events[(*count)++];
    memset(target, 0, sizeof(*target));
    target->name = name ? aegis_strdup(name) : NULL;
    target->content = aegis_strdup(content);
    if ((name && !target->name) || !target->content) {
        free(target->name);
        free(target->content);
        memset(target, 0, sizeof(*target));
        --*count;
        return AEGIS_ERR_OOM;
    }
    target->event.role = role;
    target->event.kind = kind;
    target->event.name = target->name;
    target->event.content = target->content;
    return AEGIS_OK;
}

static void copy_events(
    const OwnedEvent *owned,
    size_t count,
    AegisContextEvent *events
)
{
    size_t index;

    for (index = 0U; index < count; ++index) {
        events[index] = owned[index].event;
    }
}

static char *json_payload_string(const char *key, const char *value)
{
    cJSON *root = cJSON_CreateObject();
    char *rendered;

    if (!root) {
        return NULL;
    }
    cJSON_AddStringToObject(root, key, value ? value : "");
    rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return rendered;
}

static void record_event(
    AegisState *state,
    AegisTrace *trace,
    const char *session_id,
    int step,
    const char *kind,
    const char *payload
)
{
    if (trace && trace->file) {
        (void)aegis_trace_event(trace, session_id, step, kind, payload);
    }
    if (state && state->database) {
        (void)aegis_state_add_event(state, session_id, step, kind, payload);
    }
}

static AegisStatus persist_reminder(
    void *userdata,
    const char *session_id,
    const char *message,
    const char *due
)
{
    return aegis_state_add_reminder(
        (AegisState *)userdata,
        session_id,
        message,
        due
    );
}

static int approval_granted(
    const AegisMessage *message,
    const AegisTool *tool,
    const char *decision
)
{
    char answer[32];

    if (strcmp(decision, "require_approval") != 0) {
        return 1;
    }
    if (message->auto_approve && tool->risk_level != AEGIS_RISK_CRITICAL) {
        return 1;
    }
    if (message->no_input || !isatty(STDIN_FILENO)) {
        return 0;
    }
    fprintf(
        stderr,
        "Approval required for tool '%s' (risk=%s). Approve? [y/N] ",
        tool->name,
        aegis_tool_risk_name(tool->risk_level)
    );
    fflush(stderr);
    if (!fgets(answer, sizeof(answer), stdin)) {
        return 0;
    }
    return answer[0] == 'y' || answer[0] == 'Y';
}

static AegisStatus tool_args_from_json(
    cJSON *arguments,
    AegisToolArgs *args,
    AegisKv **items_out,
    char ***owned_out
)
{
    AegisKv *items;
    char **owned;
    cJSON *entry;
    size_t count;
    size_t index = 0U;

    if (!cJSON_IsObject(arguments) || !args || !items_out || !owned_out) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    count = (size_t)cJSON_GetArraySize(arguments);
    items = count ? calloc(count, sizeof(*items)) : NULL;
    owned = count ? calloc(count, sizeof(*owned)) : NULL;
    if (count && (!items || !owned)) {
        free(items);
        free(owned);
        return AEGIS_ERR_OOM;
    }
    cJSON_ArrayForEach(entry, arguments) {
        items[index].key = entry->string;
        if (cJSON_IsString(entry)) {
            items[index].value = entry->valuestring;
        } else {
            owned[index] = cJSON_PrintUnformatted(entry);
            if (!owned[index]) {
                size_t cleanup;
                for (cleanup = 0U; cleanup < index; ++cleanup) {
                    cJSON_free(owned[cleanup]);
                }
                free(items);
                free(owned);
                return AEGIS_ERR_OOM;
            }
            items[index].value = owned[index];
        }
        ++index;
    }
    args->items = items;
    args->count = count;
    *items_out = items;
    *owned_out = owned;
    return AEGIS_OK;
}

static void clear_tool_args(
    AegisKv *items,
    char **owned,
    size_t count
)
{
    size_t index;

    for (index = 0U; index < count; ++index) {
        cJSON_free(owned[index]);
    }
    free(owned);
    free(items);
}

static char *format_observation(
    const char *tool,
    AegisStatus status,
    const AegisToolResult *result
)
{
    cJSON *root = cJSON_CreateObject();
    char *rendered;

    if (!root) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "tool", tool);
    cJSON_AddBoolToObject(root, "ok", status == AEGIS_OK && result->ok);
    cJSON_AddNumberToObject(root, "exit_code", result->exit_code);
    cJSON_AddNumberToObject(root, "output_bytes", (double)result->output_bytes);
    if (result->stdout_data) {
        cJSON_AddStringToObject(root, "stdout", result->stdout_data);
    }
    if (result->stderr_data) {
        cJSON_AddStringToObject(root, "stderr", result->stderr_data);
    }
    if (result->error_message) {
        cJSON_AddStringToObject(root, "error", result->error_message);
    }
    if (status != AEGIS_OK) {
        cJSON_AddStringToObject(root, "status", aegis_status_string(status));
    }
    rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return rendered;
}

static char *format_tool_call(
    const AegisAction *action,
    const AegisTool *tool,
    const char *decision
)
{
    cJSON *root = cJSON_CreateObject();
    char *rendered;

    if (!root || !action) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddStringToObject(root, "tool", action->tool);
    cJSON_AddItemToObject(
        root,
        "arguments",
        action->arguments
            ? cJSON_Duplicate(action->arguments, 1)
            : cJSON_CreateObject()
    );
    cJSON_AddStringToObject(
        root,
        "decision",
        decision ? decision : "deny_unknown"
    );
    cJSON_AddStringToObject(
        root,
        "risk",
        tool ? aegis_tool_risk_name(tool->risk_level) : "unknown"
    );
    rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return rendered;
}

static char *format_approval(
    const char *tool,
    int granted,
    int automatic
)
{
    cJSON *root = cJSON_CreateObject();
    char *rendered;

    if (!root) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "tool", tool);
    cJSON_AddBoolToObject(root, "granted", granted);
    cJSON_AddBoolToObject(root, "automatic", automatic);
    rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return rendered;
}

static AegisStatus call_provider(
    const AegisConfig *config,
    const AegisProvider *provider,
    const AegisLLMRequest *request,
    AegisLLMResponse *response
)
{
    int attempt;
    AegisStatus status = AEGIS_ERR_PROVIDER;

    for (attempt = 0; attempt <= config->max_retries; ++attempt) {
        struct timespec delay;

        aegis_llm_response_init(response);
        status = provider->generate(provider, request, response);
        if (status == AEGIS_OK) {
            return AEGIS_OK;
        }
        if (status == AEGIS_ERR_INTERRUPTED) {
            return status;
        }
        if (!config->retry_enabled || attempt == config->max_retries) {
            break;
        }
        aegis_llm_response_free(response);
        delay.tv_sec = config->retry_backoff_ms / 1000;
        delay.tv_nsec =
            (long)(config->retry_backoff_ms % 1000) * 1000000L;
        nanosleep(&delay, NULL);
    }
    return status == AEGIS_ERR_OOM ? status : AEGIS_ERR_PROVIDER;
}

AegisStatus aegis_agent_run(
    const AegisConfig *config,
    const AegisMessage *message,
    AegisState *state,
    AegisTrace *trace,
    AegisResponse *response
)
{
    AegisToolRegistry registry;
    AegisProvider provider;
    OwnedEvent *owned_history;
    AegisContextEvent *history;
    size_t history_capacity;
    size_t history_count = 0U;
    int tool_calls = 0;
    int repair_attempts = 0;
    int step;
    int iteration;
    long started;
    AegisStatus status;

    if (!config || !message || !response) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    aegis_tool_registry_init(&registry);
    status = aegis_tool_registry_register_defaults(&registry);
    if (status != AEGIS_OK) {
        return status;
    }
    status = aegis_provider_create(config, &provider);
    if (status != AEGIS_OK) {
        return AEGIS_ERR_PROVIDER;
    }

    history_capacity = 3U + (size_t)config->max_steps * 3U;
    owned_history = calloc(history_capacity, sizeof(*owned_history));
    history = calloc(history_capacity, sizeof(*history));
    if (!owned_history || !history) {
        free(owned_history);
        free(history);
        return AEGIS_ERR_OOM;
    }
    started = monotonic_milliseconds();

    for (iteration = 1; iteration <= config->max_steps; ++iteration) {
        AegisMessage current = *message;
        AegisContextBuildInput input;
        AegisContext context;
        AegisLLMRequest request;
        AegisLLMResponse llm;
        AegisAction action;
        const char *workspace_summary;
        char continue_prompt[] =
            "Continue the task using the previous tool observation. "
            "Return exactly one JSON action.";
        char *payload;

        if (message->is_cancelled &&
            message->is_cancelled(message->adapter_userdata)) {
            clear_events(owned_history, history_count);
            free(history);
            return AEGIS_ERR_INTERRUPTED;
        }
        if (config->max_wall_time_ms > 0 &&
            monotonic_milliseconds() - started >=
                config->max_wall_time_ms) {
            clear_events(owned_history, history_count);
            free(history);
            return AEGIS_ERR_RUNTIME;
        }
        step = message->initial_step + iteration;
        if (history_count > 0U) {
            current.text = continue_prompt;
        }
        copy_events(owned_history, history_count, history);
        workspace_summary = message->workspace
            ? message->workspace
            : config->workspace_root;
        memset(&input, 0, sizeof(input));
        input.current_message = &current;
        input.history = history;
        input.history_count = history_count;
        input.workspace_summary = workspace_summary;
        aegis_context_init(&context);
        status = aegis_context_build(&context, config, &registry, &input);
        if (status != AEGIS_OK) {
            clear_events(owned_history, history_count);
            free(history);
            return status;
        }

        request.config = config;
        request.context = &context;
        request.model = config->model;
        request.session_id = message->session_id;
        request.is_cancelled = message->is_cancelled;
        request.cancel_userdata = message->adapter_userdata;
        aegis_llm_response_init(&llm);
        record_event(
            state,
            trace,
            message->session_id,
            step,
            "model_request",
            "{\"status\":\"sent\"}"
        );
        status = call_provider(config, &provider, &request, &llm);
        if (status != AEGIS_OK) {
            payload = json_payload_string(
                "error",
                llm.error_message ? llm.error_message : aegis_status_string(status)
            );
            record_event(
                state, trace, message->session_id, step, "model_error",
                payload ? payload : "{}");
            cJSON_free(payload);
            aegis_llm_response_free(&llm);
            aegis_context_clear(&context);
            clear_events(owned_history, history_count);
            free(history);
            return status == AEGIS_ERR_INTERRUPTED
                ? status
                : AEGIS_ERR_PROVIDER;
        }
        payload = json_payload_string("content", llm.content);
        record_event(
            state, trace, message->session_id, step, "model_response",
            payload ? payload : "{}");
        cJSON_free(payload);

        aegis_action_init(&action);
        status = aegis_action_parse(llm.content, &action);
        if (status != AEGIS_OK) {
            const char repair[] =
                "The previous model response was invalid. Return only one "
                "valid json_action_v1 object with type final or tool_call.";

            ++repair_attempts;
            if (history_count == 0U) {
                status = append_event(
                    owned_history,
                    history_capacity,
                    &history_count,
                    AEGIS_CONTEXT_ROLE_USER,
                    AEGIS_CONTEXT_EVENT_MESSAGE,
                    NULL,
                    message->text
                );
            }
            if (status == AEGIS_OK) {
                status = append_event(
                    owned_history, history_capacity, &history_count,
                    AEGIS_CONTEXT_ROLE_ASSISTANT,
                    AEGIS_CONTEXT_EVENT_MESSAGE,
                    NULL,
                    llm.content
                );
            }
            if (status == AEGIS_OK) {
                status = append_event(
                    owned_history, history_capacity, &history_count,
                    AEGIS_CONTEXT_ROLE_TOOL, AEGIS_CONTEXT_EVENT_OBSERVATION,
                    "protocol_error", repair);
            }
            aegis_llm_response_free(&llm);
            aegis_context_clear(&context);
            if (status != AEGIS_OK) {
                clear_events(owned_history, history_count);
                free(history);
                return status;
            }
            if (repair_attempts > config->invalid_json_repair_attempts) {
                clear_events(owned_history, history_count);
                free(history);
                return AEGIS_ERR_PARSE;
            }
            continue;
        }
        repair_attempts = 0;

        if (action.type == AEGIS_ACTION_FINAL) {
            if (!aegis_response_set_text(response, action.message)) {
                status = AEGIS_ERR_OOM;
            } else {
                response->steps = step;
                snprintf(response->status, sizeof(response->status), "success");
                status = AEGIS_OK;
            }
            payload = json_payload_string("message", action.message);
            record_event(
                state, trace, message->session_id, step, "final",
                payload ? payload : "{}");
            cJSON_free(payload);
            aegis_action_clear(&action);
            aegis_llm_response_free(&llm);
            aegis_context_clear(&context);
            clear_events(owned_history, history_count);
            free(history);
            return status;
        }

        if (++tool_calls > config->max_tool_calls) {
            aegis_action_clear(&action);
            aegis_llm_response_free(&llm);
            aegis_context_clear(&context);
            clear_events(owned_history, history_count);
            free(history);
            return AEGIS_ERR_MAX_STEPS;
        } else {
            const AegisTool *tool =
                aegis_tool_registry_find(&registry, action.tool);
            const char *decision =
                aegis_config_tool_decision(config, action.tool);
            AegisToolArgs args = {0};
            AegisKv *items = NULL;
            char **owned_values = NULL;
            AegisToolContext tool_context;
            AegisToolResult result;
            char *observation;
            char *tool_payload;
            int approved = 0;

            aegis_tool_result_init(&result);
            tool_payload = format_tool_call(&action, tool, decision);
            record_event(
                state,
                trace,
                message->session_id,
                step,
                "tool_call",
                tool_payload ? tool_payload : "{}"
            );
            cJSON_free(tool_payload);
            if (!tool || !decision) {
                status = AEGIS_ERR_POLICY_DENIED;
                observation = aegis_strdup(
                    "{\"ok\":false,\"error\":\"unknown or unconfigured tool\"}");
            } else {
                approved = approval_granted(message, tool, decision);
                if (strcmp(decision, "require_approval") == 0) {
                    char *approval_payload = format_approval(
                        action.tool,
                        approved,
                        message->auto_approve &&
                            tool->risk_level != AEGIS_RISK_CRITICAL
                    );
                    record_event(
                        state,
                        trace,
                        message->session_id,
                        step,
                        "approval",
                        approval_payload ? approval_payload : "{}"
                    );
                    cJSON_free(approval_payload);
                }
                if (strcmp(decision, "require_approval") == 0 && !approved) {
                    aegis_tool_result_clear(&result);
                    aegis_action_clear(&action);
                    aegis_llm_response_free(&llm);
                    aegis_context_clear(&context);
                    clear_events(owned_history, history_count);
                    free(history);
                    return message->is_cancelled &&
                        message->is_cancelled(message->adapter_userdata)
                        ? AEGIS_ERR_INTERRUPTED
                        : AEGIS_ERR_APPROVAL_REJECTED;
                }
                status = tool_args_from_json(
                    action.arguments, &args, &items, &owned_values);
                if (status == AEGIS_OK) {
                    aegis_tool_context_from_config(
                        &tool_context,
                        config,
                        message->workspace,
                        approved
                    );
                    tool_context.ask_user = message->ask_user;
                    tool_context.send_message = message->send_message;
                    tool_context.adapter_userdata = message->adapter_userdata;
                    tool_context.persist_reminder =
                        state ? persist_reminder : NULL;
                    tool_context.is_cancelled = message->is_cancelled;
                    tool_context.state_userdata = state;
                    tool_context.session_id = message->session_id;
                    status = aegis_tool_registry_execute(
                        &registry,
                        action.tool,
                        &args,
                        &tool_context,
                        &result
                    );
                }
                observation = format_observation(action.tool, status, &result);
            }
            record_event(
                state,
                trace,
                message->session_id,
                step,
                "tool_result",
                observation ? observation : "{}"
            );
            clear_tool_args(items, owned_values, args.count);
            aegis_tool_result_clear(&result);
            if (!observation) {
                status = AEGIS_ERR_OOM;
            }
            if (status == AEGIS_ERR_INTERRUPTED) {
                free(observation);
                aegis_action_clear(&action);
                aegis_llm_response_free(&llm);
                aegis_context_clear(&context);
                clear_events(owned_history, history_count);
                free(history);
                return status;
            }
            if (status != AEGIS_OK &&
                ((status == AEGIS_ERR_POLICY_DENIED &&
                  config->stop_on_policy_denied) ||
                 (status != AEGIS_ERR_POLICY_DENIED &&
                  config->stop_on_tool_error))) {
                free(observation);
                aegis_action_clear(&action);
                aegis_llm_response_free(&llm);
                aegis_context_clear(&context);
                clear_events(owned_history, history_count);
                free(history);
                return status == AEGIS_ERR_POLICY_DENIED
                    ? status
                    : AEGIS_ERR_TOOL;
            }
            if ((history_count == 0U &&
                 append_event(
                    owned_history,
                    history_capacity,
                    &history_count,
                    AEGIS_CONTEXT_ROLE_USER,
                    AEGIS_CONTEXT_EVENT_MESSAGE,
                    NULL,
                    message->text) != AEGIS_OK) ||
                append_event(
                    owned_history, history_capacity, &history_count,
                    AEGIS_CONTEXT_ROLE_ASSISTANT,
                    AEGIS_CONTEXT_EVENT_MESSAGE,
                    NULL,
                    llm.content) != AEGIS_OK ||
                append_event(
                    owned_history, history_capacity, &history_count,
                    AEGIS_CONTEXT_ROLE_TOOL,
                    AEGIS_CONTEXT_EVENT_OBSERVATION,
                    action.tool,
                    observation ? observation : "{}") != AEGIS_OK) {
                free(observation);
                aegis_action_clear(&action);
                aegis_llm_response_free(&llm);
                aegis_context_clear(&context);
                clear_events(owned_history, history_count);
                free(history);
                return AEGIS_ERR_OOM;
            }
            free(observation);
        }
        response->steps = step;
        aegis_action_clear(&action);
        aegis_llm_response_free(&llm);
        aegis_context_clear(&context);
    }

    clear_events(owned_history, history_count);
    free(history);
    snprintf(response->status, sizeof(response->status), "max_steps");
    response->steps = message->initial_step + config->max_steps;
    return AEGIS_ERR_MAX_STEPS;
}

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <curl/curl.h>
#include <cjson/cJSON.h>

#include "aegis/provider.h"
#include "aegis/str.h"

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
    size_t maximum;
} HttpBuffer;

typedef enum {
    PROVIDER_OPENAI,
    PROVIDER_OLLAMA,
    PROVIDER_ANTHROPIC,
    PROVIDER_GEMINI
} ProviderStyle;

static int transfer_cancelled(
    void *userdata,
    curl_off_t download_total,
    curl_off_t download_now,
    curl_off_t upload_total,
    curl_off_t upload_now
)
{
    const AegisLLMRequest *request = userdata;

    (void)download_total;
    (void)download_now;
    (void)upload_total;
    (void)upload_now;
    return request && request->is_cancelled &&
        request->is_cancelled(request->cancel_userdata);
}

static size_t receive_data(
    char *data,
    size_t size,
    size_t count,
    void *userdata
)
{
    HttpBuffer *buffer = userdata;
    size_t bytes = size * count;
    size_t required;
    char *resized;

    if (size != 0U && bytes / size != count) {
        return 0U;
    }
    if (bytes > buffer->maximum - buffer->length) {
        return 0U;
    }
    required = buffer->length + bytes + 1U;
    if (required > buffer->capacity) {
        size_t capacity = buffer->capacity ? buffer->capacity : 4096U;

        while (capacity < required) {
            if (capacity > SIZE_MAX / 2U) {
                return 0U;
            }
            capacity *= 2U;
        }
        resized = realloc(buffer->data, capacity);
        if (!resized) {
            return 0U;
        }
        buffer->data = resized;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->length, data, bytes);
    buffer->length += bytes;
    buffer->data[buffer->length] = '\0';
    return bytes;
}

static const char *provider_role_name(AegisContextRole role)
{
    switch (role) {
        case AEGIS_CONTEXT_ROLE_SYSTEM: return "system";
        case AEGIS_CONTEXT_ROLE_ASSISTANT: return "assistant";
        /*
         * Context events do not carry provider-specific tool-call IDs.
         * Render observations as user messages so every compatible endpoint
         * can accept the follow-up turn without inventing an ID.
         */
        case AEGIS_CONTEXT_ROLE_TOOL: return "user";
        default: return "user";
    }
}

static cJSON *build_openai_tools(const AegisContext *context)
{
    cJSON *tools = cJSON_CreateArray();
    size_t index;

    if (!tools) {
        return NULL;
    }
    for (index = 0U; index < context->tool_count; ++index) {
        const AegisContextTool *source = &context->tools[index];
        cJSON *entry = cJSON_CreateObject();
        cJSON *function = cJSON_CreateObject();
        cJSON *parameters = cJSON_Parse(source->schema_json);

        if (!entry || !function || !parameters) {
            cJSON_Delete(entry);
            cJSON_Delete(function);
            cJSON_Delete(parameters);
            cJSON_Delete(tools);
            return NULL;
        }
        cJSON_AddStringToObject(entry, "type", "function");
        cJSON_AddStringToObject(function, "name", source->name);
        cJSON_AddStringToObject(function, "description", source->description);
        cJSON_AddItemToObject(function, "parameters", parameters);
        cJSON_AddItemToObject(entry, "function", function);
        cJSON_AddItemToArray(tools, entry);
    }
    return tools;
}

static cJSON *build_openai_body(const AegisLLMRequest *request)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *messages = cJSON_CreateArray();
    size_t index;

    if (!root || !messages) {
        cJSON_Delete(root);
        cJSON_Delete(messages);
        return NULL;
    }
    cJSON_AddStringToObject(root, "model", request->model);
    cJSON_AddNumberToObject(root, "temperature", request->config->temperature);
    cJSON_AddNumberToObject(root, "top_p", request->config->top_p);
    cJSON_AddNumberToObject(root, "max_tokens", request->config->max_tokens);
    cJSON_AddBoolToObject(root, "stream", 0);
    if (request->config->force_json_action) {
        cJSON *format = cJSON_CreateObject();

        if (!format) {
            cJSON_Delete(root);
            return NULL;
        }
        cJSON_AddStringToObject(format, "type", "json_object");
        cJSON_AddItemToObject(root, "response_format", format);
    }
    cJSON_AddItemToObject(root, "messages", messages);
    for (index = 0U; index < request->context->message_count; ++index) {
        const AegisContextMessage *source = &request->context->messages[index];
        cJSON *message = cJSON_CreateObject();

        if (!message) {
            cJSON_Delete(root);
            return NULL;
        }
        cJSON_AddStringToObject(
            message, "role", provider_role_name(source->role));
        cJSON_AddStringToObject(message, "content", source->content);
        cJSON_AddItemToArray(messages, message);
    }
    if (request->context->tool_count > 0U) {
        cJSON *tools = build_openai_tools(request->context);

        if (!tools) {
            cJSON_Delete(root);
            return NULL;
        }
        cJSON_AddItemToObject(root, "tools", tools);
        cJSON_AddStringToObject(root, "tool_choice", "auto");
    }
    return root;
}

static cJSON *build_ollama_body(const AegisLLMRequest *request)
{
    cJSON *root = build_openai_body(request);
    cJSON *options;

    if (!root) {
        return NULL;
    }
    cJSON_DeleteItemFromObject(root, "temperature");
    cJSON_DeleteItemFromObject(root, "top_p");
    cJSON_DeleteItemFromObject(root, "max_tokens");
    cJSON_DeleteItemFromObject(root, "tool_choice");
    cJSON_DeleteItemFromObject(root, "response_format");
    options = cJSON_CreateObject();
    if (!options) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddNumberToObject(options, "temperature", request->config->temperature);
    cJSON_AddNumberToObject(options, "top_p", request->config->top_p);
    cJSON_AddNumberToObject(options, "num_predict", request->config->max_tokens);
    cJSON_AddItemToObject(root, "options", options);
    cJSON_AddStringToObject(root, "format", "json");
    return root;
}

static cJSON *build_anthropic_body(const AegisLLMRequest *request)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *messages = cJSON_CreateArray();
    cJSON *system_parts = cJSON_CreateArray();
    size_t index;

    if (!root || !messages || !system_parts) {
        cJSON_Delete(root);
        cJSON_Delete(messages);
        cJSON_Delete(system_parts);
        return NULL;
    }
    cJSON_AddStringToObject(root, "model", request->model);
    cJSON_AddNumberToObject(root, "max_tokens", request->config->max_tokens);
    cJSON_AddNumberToObject(root, "temperature", request->config->temperature);
    cJSON_AddNumberToObject(root, "top_p", request->config->top_p);
    cJSON_AddItemToObject(root, "system", system_parts);
    cJSON_AddItemToObject(root, "messages", messages);
    for (index = 0U; index < request->context->message_count; ++index) {
        const AegisContextMessage *source = &request->context->messages[index];
        cJSON *message;

        if (source->role == AEGIS_CONTEXT_ROLE_SYSTEM) {
            cJSON *part = cJSON_CreateObject();
            if (!part) {
                cJSON_Delete(root);
                return NULL;
            }
            cJSON_AddStringToObject(part, "type", "text");
            cJSON_AddStringToObject(part, "text", source->content);
            cJSON_AddItemToArray(system_parts, part);
            continue;
        }
        message = cJSON_CreateObject();
        if (!message) {
            cJSON_Delete(root);
            return NULL;
        }
        cJSON_AddStringToObject(
            message,
            "role",
            source->role == AEGIS_CONTEXT_ROLE_ASSISTANT
                ? "assistant"
                : "user"
        );
        cJSON_AddStringToObject(message, "content", source->content);
        cJSON_AddItemToArray(messages, message);
    }
    if (request->context->tool_count > 0U) {
        cJSON *tools = cJSON_CreateArray();

        if (!tools) {
            cJSON_Delete(root);
            return NULL;
        }
        for (index = 0U; index < request->context->tool_count; ++index) {
            const AegisContextTool *source = &request->context->tools[index];
            cJSON *tool = cJSON_CreateObject();
            cJSON *schema = cJSON_Parse(source->schema_json);

            if (!tool || !schema) {
                cJSON_Delete(tool);
                cJSON_Delete(schema);
                cJSON_Delete(tools);
                cJSON_Delete(root);
                return NULL;
            }
            cJSON_AddStringToObject(tool, "name", source->name);
            cJSON_AddStringToObject(tool, "description", source->description);
            cJSON_AddItemToObject(tool, "input_schema", schema);
            cJSON_AddItemToArray(tools, tool);
        }
        cJSON_AddItemToObject(root, "tools", tools);
    }
    return root;
}

static cJSON *build_gemini_body(const AegisLLMRequest *request)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *contents = cJSON_CreateArray();
    cJSON *system = cJSON_CreateObject();
    cJSON *system_parts = cJSON_CreateArray();
    cJSON *generation = cJSON_CreateObject();
    size_t index;

    if (!root || !contents || !system || !system_parts || !generation) {
        cJSON_Delete(root);
        cJSON_Delete(contents);
        cJSON_Delete(system);
        cJSON_Delete(system_parts);
        cJSON_Delete(generation);
        return NULL;
    }
    cJSON_AddItemToObject(system, "parts", system_parts);
    cJSON_AddItemToObject(root, "systemInstruction", system);
    cJSON_AddItemToObject(root, "contents", contents);
    cJSON_AddNumberToObject(generation, "temperature", request->config->temperature);
    cJSON_AddNumberToObject(generation, "topP", request->config->top_p);
    cJSON_AddNumberToObject(
        generation, "maxOutputTokens", request->config->max_tokens);
    cJSON_AddStringToObject(generation, "responseMimeType", "application/json");
    cJSON_AddItemToObject(root, "generationConfig", generation);
    for (index = 0U; index < request->context->message_count; ++index) {
        const AegisContextMessage *source = &request->context->messages[index];
        cJSON *part = cJSON_CreateObject();

        if (!part) {
            cJSON_Delete(root);
            return NULL;
        }
        cJSON_AddStringToObject(part, "text", source->content);
        if (source->role == AEGIS_CONTEXT_ROLE_SYSTEM) {
            cJSON_AddItemToArray(system_parts, part);
        } else {
            cJSON *content = cJSON_CreateObject();
            cJSON *parts = cJSON_CreateArray();

            if (!content || !parts) {
                cJSON_Delete(content);
                cJSON_Delete(parts);
                cJSON_Delete(part);
                cJSON_Delete(root);
                return NULL;
            }
            cJSON_AddStringToObject(
                content,
                "role",
                source->role == AEGIS_CONTEXT_ROLE_ASSISTANT
                    ? "model"
                    : "user"
            );
            cJSON_AddItemToArray(parts, part);
            cJSON_AddItemToObject(content, "parts", parts);
            cJSON_AddItemToArray(contents, content);
        }
    }
    if (request->context->tool_count > 0U) {
        cJSON *tools = cJSON_CreateArray();
        cJSON *bundle = cJSON_CreateObject();
        cJSON *declarations = cJSON_CreateArray();

        if (!tools || !bundle || !declarations) {
            cJSON_Delete(tools);
            cJSON_Delete(bundle);
            cJSON_Delete(declarations);
            cJSON_Delete(root);
            return NULL;
        }
        for (index = 0U; index < request->context->tool_count; ++index) {
            const AegisContextTool *source = &request->context->tools[index];
            cJSON *declaration = cJSON_CreateObject();
            cJSON *schema = cJSON_Parse(source->schema_json);

            if (!declaration || !schema) {
                cJSON_Delete(declaration);
                cJSON_Delete(schema);
                cJSON_Delete(tools);
                cJSON_Delete(bundle);
                cJSON_Delete(declarations);
                cJSON_Delete(root);
                return NULL;
            }
            cJSON_AddStringToObject(declaration, "name", source->name);
            cJSON_AddStringToObject(
                declaration, "description", source->description);
            cJSON_AddItemToObject(declaration, "parameters", schema);
            cJSON_AddItemToArray(declarations, declaration);
        }
        cJSON_AddItemToObject(bundle, "functionDeclarations", declarations);
        cJSON_AddItemToArray(tools, bundle);
        cJSON_AddItemToObject(root, "tools", tools);
    }
    return root;
}

static char *join_url(
    const AegisConfig *config,
    ProviderStyle style
)
{
    char path[AEGIS_CONFIG_PATH_MAX + AEGIS_CONFIG_TEXT_MAX];
    const char *configured = config->chat_completions_path;
    const char *placeholder;
    int written;
    size_t length;
    char *url;

    placeholder = strstr(configured, "{model}");
    if (style == PROVIDER_GEMINI && placeholder) {
        size_t prefix = (size_t)(placeholder - configured);

        written = snprintf(
            path,
            sizeof(path),
            "%.*s%s%s",
            (int)prefix,
            configured,
            config->model,
            placeholder + strlen("{model}")
        );
    } else if (style == PROVIDER_GEMINI &&
               (!configured[0] ||
                strcmp(configured, "/chat/completions") == 0)) {
        written = snprintf(
            path, sizeof(path), "/v1beta/models/%s:generateContent",
            config->model);
    } else {
        written = snprintf(path, sizeof(path), "%s", configured);
    }
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return NULL;
    }
    length = strlen(config->provider_base_url) + strlen(path) + 2U;
    url = malloc(length);
    if (!url) {
        return NULL;
    }
    snprintf(
        url,
        length,
        "%s%s%s",
        config->provider_base_url,
        config->provider_base_url[strlen(config->provider_base_url) - 1U] == '/' ||
            path[0] == '/' ? "" : "/",
        path
    );
    return url;
}

static char *action_from_tool_call(
    const char *name,
    cJSON *arguments
)
{
    cJSON *action = cJSON_CreateObject();
    char *rendered;

    if (!action || !name || !arguments) {
        cJSON_Delete(action);
        return NULL;
    }
    cJSON_AddStringToObject(action, "type", "tool_call");
    cJSON_AddStringToObject(action, "tool", name);
    cJSON_AddItemToObject(action, "arguments", cJSON_Duplicate(arguments, 1));
    rendered = cJSON_PrintUnformatted(action);
    cJSON_Delete(action);
    return rendered;
}

static AegisStatus parse_openai_response(
    cJSON *root,
    AegisLLMResponse *response
)
{
    cJSON *choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
    cJSON *choice = cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : NULL;
    cJSON *message = choice
        ? cJSON_GetObjectItemCaseSensitive(choice, "message")
        : NULL;
    cJSON *content = message
        ? cJSON_GetObjectItemCaseSensitive(message, "content")
        : NULL;
    cJSON *tool_calls = message
        ? cJSON_GetObjectItemCaseSensitive(message, "tool_calls")
        : NULL;
    cJSON *finish = choice
        ? cJSON_GetObjectItemCaseSensitive(choice, "finish_reason")
        : NULL;
    cJSON *usage = cJSON_GetObjectItemCaseSensitive(root, "usage");

    if (cJSON_IsArray(tool_calls) && cJSON_GetArraySize(tool_calls) > 0) {
        cJSON *call = cJSON_GetArrayItem(tool_calls, 0);
        cJSON *function = cJSON_GetObjectItemCaseSensitive(call, "function");
        cJSON *name = cJSON_GetObjectItemCaseSensitive(function, "name");
        cJSON *arguments = cJSON_GetObjectItemCaseSensitive(function, "arguments");
        cJSON *parsed_arguments = cJSON_IsString(arguments)
            ? cJSON_Parse(arguments->valuestring)
            : cJSON_Duplicate(arguments, 1);

        if (!cJSON_IsString(name) || !cJSON_IsObject(parsed_arguments)) {
            cJSON_Delete(parsed_arguments);
            return AEGIS_ERR_PARSE;
        }
        response->content = action_from_tool_call(
            name->valuestring, parsed_arguments);
        cJSON_Delete(parsed_arguments);
    } else if (cJSON_IsString(content)) {
        response->content = aegis_strdup(content->valuestring);
    }
    if (!response->content) {
        return AEGIS_ERR_PARSE;
    }
    if (cJSON_IsString(finish)) {
        snprintf(
            response->finish_reason,
            sizeof(response->finish_reason),
            "%s",
            finish->valuestring
        );
    }
    if (cJSON_IsObject(usage)) {
        cJSON *prompt = cJSON_GetObjectItemCaseSensitive(usage, "prompt_tokens");
        cJSON *completion =
            cJSON_GetObjectItemCaseSensitive(usage, "completion_tokens");
        if (cJSON_IsNumber(prompt)) {
            response->prompt_tokens = (long)prompt->valuedouble;
        }
        if (cJSON_IsNumber(completion)) {
            response->completion_tokens = (long)completion->valuedouble;
        }
    }
    return AEGIS_OK;
}

static AegisStatus parse_ollama_response(
    cJSON *root,
    AegisLLMResponse *response
)
{
    cJSON *message = cJSON_GetObjectItemCaseSensitive(root, "message");
    cJSON *content = cJSON_GetObjectItemCaseSensitive(message, "content");
    cJSON *tool_calls = cJSON_GetObjectItemCaseSensitive(message, "tool_calls");
    cJSON *done_reason = cJSON_GetObjectItemCaseSensitive(root, "done_reason");

    if (cJSON_IsArray(tool_calls) && cJSON_GetArraySize(tool_calls) > 0) {
        cJSON *call = cJSON_GetArrayItem(tool_calls, 0);
        cJSON *function = cJSON_GetObjectItemCaseSensitive(call, "function");
        cJSON *name = cJSON_GetObjectItemCaseSensitive(function, "name");
        cJSON *arguments = cJSON_GetObjectItemCaseSensitive(function, "arguments");

        if (!cJSON_IsString(name) || !cJSON_IsObject(arguments)) {
            return AEGIS_ERR_PARSE;
        }
        response->content = action_from_tool_call(name->valuestring, arguments);
    } else if (cJSON_IsString(content)) {
        response->content = aegis_strdup(content->valuestring);
    }
    if (!response->content) {
        return AEGIS_ERR_PARSE;
    }
    if (cJSON_IsString(done_reason)) {
        snprintf(
            response->finish_reason,
            sizeof(response->finish_reason),
            "%s",
            done_reason->valuestring
        );
    }
    return AEGIS_OK;
}

static AegisStatus parse_anthropic_response(
    cJSON *root,
    AegisLLMResponse *response
)
{
    cJSON *content = cJSON_GetObjectItemCaseSensitive(root, "content");
    cJSON *block;
    cJSON *stop = cJSON_GetObjectItemCaseSensitive(root, "stop_reason");
    int index;

    if (!cJSON_IsArray(content)) {
        return AEGIS_ERR_PARSE;
    }
    cJSON_ArrayForEach(block, content) {
        cJSON *type = cJSON_GetObjectItemCaseSensitive(block, "type");

        if (cJSON_IsString(type) && strcmp(type->valuestring, "tool_use") == 0) {
            cJSON *name = cJSON_GetObjectItemCaseSensitive(block, "name");
            cJSON *input = cJSON_GetObjectItemCaseSensitive(block, "input");
            if (cJSON_IsString(name) && cJSON_IsObject(input)) {
                response->content =
                    action_from_tool_call(name->valuestring, input);
                break;
            }
        }
    }
    if (!response->content) {
        cJSON_ArrayForEach(block, content) {
            cJSON *type = cJSON_GetObjectItemCaseSensitive(block, "type");

            if (!cJSON_IsString(type) ||
                strcmp(type->valuestring, "text") != 0) {
                continue;
            }
            cJSON *text = cJSON_GetObjectItemCaseSensitive(block, "text");
            if (cJSON_IsString(text)) {
                response->content = aegis_strdup(text->valuestring);
                break;
            }
        }
    }
    index = cJSON_GetArraySize(content);
    (void)index;
    if (!response->content) {
        return AEGIS_ERR_PARSE;
    }
    if (cJSON_IsString(stop)) {
        snprintf(
            response->finish_reason,
            sizeof(response->finish_reason),
            "%s",
            stop->valuestring
        );
    }
    return AEGIS_OK;
}

static AegisStatus parse_gemini_response(
    cJSON *root,
    AegisLLMResponse *response
)
{
    cJSON *candidates = cJSON_GetObjectItemCaseSensitive(root, "candidates");
    cJSON *candidate = cJSON_IsArray(candidates)
        ? cJSON_GetArrayItem(candidates, 0)
        : NULL;
    cJSON *content = candidate
        ? cJSON_GetObjectItemCaseSensitive(candidate, "content")
        : NULL;
    cJSON *parts = content
        ? cJSON_GetObjectItemCaseSensitive(content, "parts")
        : NULL;
    cJSON *part;

    if (!cJSON_IsArray(parts)) {
        return AEGIS_ERR_PARSE;
    }
    cJSON_ArrayForEach(part, parts) {
        cJSON *call =
            cJSON_GetObjectItemCaseSensitive(part, "functionCall");

        if (cJSON_IsObject(call)) {
            cJSON *name = cJSON_GetObjectItemCaseSensitive(call, "name");
            cJSON *args = cJSON_GetObjectItemCaseSensitive(call, "args");
            if (cJSON_IsString(name) && cJSON_IsObject(args)) {
                response->content =
                    action_from_tool_call(name->valuestring, args);
                break;
            }
        }
    }
    if (!response->content) {
        cJSON_ArrayForEach(part, parts) {
            cJSON *text =
                cJSON_GetObjectItemCaseSensitive(part, "text");
            if (cJSON_IsString(text)) {
                response->content = aegis_strdup(text->valuestring);
                break;
            }
        }
    }
    return response->content ? AEGIS_OK : AEGIS_ERR_PARSE;
}

static AegisStatus generate_http(
    const AegisProvider *provider,
    const AegisLLMRequest *request,
    AegisLLMResponse *response,
    ProviderStyle style
)
{
    const AegisConfig *config;
    const char *api_key;
    CURL *curl;
    CURLcode curl_status;
    struct curl_slist *headers = NULL;
    cJSON *body;
    cJSON *parsed;
    char *serialized;
    char *url;
    HttpBuffer received = {0};
    struct timespec started;
    struct timespec ended;
    AegisStatus status;

    if (!provider || !provider->config || !request || !request->context ||
        !request->model || !response) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    if (request->is_cancelled &&
        request->is_cancelled(request->cancel_userdata)) {
        return AEGIS_ERR_INTERRUPTED;
    }
    config = provider->config;
    api_key = config->api_key_env[0] ? getenv(config->api_key_env) : NULL;
    if (style != PROVIDER_OLLAMA && (!api_key || !api_key[0])) {
        response->error_message = aegis_strdup("provider API key is not set");
        return AEGIS_ERR_NOT_FOUND;
    }

    if (style == PROVIDER_OLLAMA) {
        body = build_ollama_body(request);
    } else if (style == PROVIDER_ANTHROPIC) {
        body = build_anthropic_body(request);
    } else if (style == PROVIDER_GEMINI) {
        body = build_gemini_body(request);
    } else {
        body = build_openai_body(request);
    }
    serialized = body ? cJSON_PrintUnformatted(body) : NULL;
    cJSON_Delete(body);
    url = join_url(config, style);
    if (!serialized || !url) {
        cJSON_free(serialized);
        free(url);
        return AEGIS_ERR_OOM;
    }

    curl = curl_easy_init();
    if (!curl) {
        cJSON_free(serialized);
        free(url);
        return AEGIS_ERR_RUNTIME;
    }
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (style == PROVIDER_ANTHROPIC) {
        char key_header[AEGIS_CONFIG_TEXT_MAX * 2U];
        snprintf(key_header, sizeof(key_header), "x-api-key: %s", api_key);
        headers = curl_slist_append(headers, key_header);
        headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
    } else if (style == PROVIDER_GEMINI) {
        char key_header[AEGIS_CONFIG_TEXT_MAX * 2U];
        snprintf(key_header, sizeof(key_header), "x-goog-api-key: %s", api_key);
        headers = curl_slist_append(headers, key_header);
    } else if (style != PROVIDER_OLLAMA) {
        char authorization[AEGIS_CONFIG_TEXT_MAX * 2U];
        snprintf(
            authorization, sizeof(authorization), "Authorization: Bearer %s",
            api_key);
        headers = curl_slist_append(headers, authorization);
    }
    if (strcmp(config->provider, "openrouter") == 0) {
        const char *referer = getenv("AEGIS_OPENROUTER_REFERER");
        const char *title = getenv("AEGIS_OPENROUTER_TITLE");
        char referer_header[AEGIS_CONFIG_URL_MAX + 32U];
        char title_header[AEGIS_CONFIG_TEXT_MAX + 32U];

        snprintf(
            referer_header,
            sizeof(referer_header),
            "HTTP-Referer: %s",
            referer && referer[0] ? referer : "http://localhost"
        );
        snprintf(
            title_header,
            sizeof(title_header),
            "X-OpenRouter-Title: %s",
            title && title[0] ? title : "Aegis-C"
        );
        headers = curl_slist_append(headers, referer_header);
        headers = curl_slist_append(headers, title_header);
    }

    received.maximum = (size_t)config->max_tool_output_bytes * 16U;
    if (received.maximum < 1024U * 1024U) {
        received.maximum = 1024U * 1024U;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, serialized);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(serialized));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &received);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)config->provider_timeout_ms);
    curl_easy_setopt(
        curl,
        CURLOPT_CONNECTTIMEOUT_MS,
        (long)config->provider_connect_timeout_ms
    );
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, transfer_cancelled);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, request);
    clock_gettime(CLOCK_MONOTONIC, &started);
    curl_status = curl_easy_perform(curl);
    clock_gettime(CLOCK_MONOTONIC, &ended);
    response->latency_ms =
        (ended.tv_sec - started.tv_sec) * 1000L +
        (ended.tv_nsec - started.tv_nsec) / 1000000L;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response->http_status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    cJSON_free(serialized);
    free(url);

    if (curl_status != CURLE_OK) {
        if (curl_status == CURLE_ABORTED_BY_CALLBACK &&
            request->is_cancelled &&
            request->is_cancelled(request->cancel_userdata)) {
            free(received.data);
            return AEGIS_ERR_INTERRUPTED;
        }
        response->error_message = aegis_strdup(curl_easy_strerror(curl_status));
        free(received.data);
        return AEGIS_ERR_IO;
    }
    response->raw_json = received.data
        ? received.data
        : aegis_strdup("");
    if (!response->raw_json) {
        return AEGIS_ERR_OOM;
    }
    parsed = cJSON_Parse(response->raw_json);
    if (!parsed) {
        return AEGIS_ERR_PARSE;
    }
    if (response->http_status < 200 || response->http_status >= 300) {
        cJSON *error = cJSON_GetObjectItemCaseSensitive(parsed, "error");
        cJSON *message = cJSON_IsObject(error)
            ? cJSON_GetObjectItemCaseSensitive(error, "message")
            : NULL;
        response->error_message = aegis_strdup(
            cJSON_IsString(message) ? message->valuestring : "provider error");
        cJSON_Delete(parsed);
        return AEGIS_ERR_RUNTIME;
    }
    if (style == PROVIDER_OLLAMA) {
        status = parse_ollama_response(parsed, response);
    } else if (style == PROVIDER_ANTHROPIC) {
        status = parse_anthropic_response(parsed, response);
    } else if (style == PROVIDER_GEMINI) {
        status = parse_gemini_response(parsed, response);
    } else {
        status = parse_openai_response(parsed, response);
    }
    cJSON_Delete(parsed);
    return status;
}

AegisStatus aegis_provider_generate_openai(
    const AegisProvider *provider,
    const AegisLLMRequest *request,
    AegisLLMResponse *response
)
{
    return generate_http(provider, request, response, PROVIDER_OPENAI);
}

AegisStatus aegis_provider_generate_ollama(
    const AegisProvider *provider,
    const AegisLLMRequest *request,
    AegisLLMResponse *response
)
{
    return generate_http(provider, request, response, PROVIDER_OLLAMA);
}

AegisStatus aegis_provider_generate_anthropic(
    const AegisProvider *provider,
    const AegisLLMRequest *request,
    AegisLLMResponse *response
)
{
    return generate_http(provider, request, response, PROVIDER_ANTHROPIC);
}

AegisStatus aegis_provider_generate_gemini(
    const AegisProvider *provider,
    const AegisLLMRequest *request,
    AegisLLMResponse *response
)
{
    return generate_http(provider, request, response, PROVIDER_GEMINI);
}

AegisStatus aegis_provider_generate_mock(
    const AegisProvider *provider,
    const AegisLLMRequest *request,
    AegisLLMResponse *response
)
{
    const char *configured = getenv("AEGIS_MOCK_RESPONSE");
    const char *sequence = getenv("AEGIS_MOCK_RESPONSES");
    cJSON *responses = NULL;
    cJSON *selected = NULL;
    size_t response_index = 0U;
    size_t index;

    (void)provider;
    if (!request || !response) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    if (request->is_cancelled &&
        request->is_cancelled(request->cancel_userdata)) {
        return AEGIS_ERR_INTERRUPTED;
    }
    if (sequence && sequence[0]) {
        responses = cJSON_Parse(sequence);
        if (!cJSON_IsArray(responses) ||
            cJSON_GetArraySize(responses) == 0) {
            cJSON_Delete(responses);
            response->error_message =
                aegis_strdup("AEGIS_MOCK_RESPONSES must be a JSON array");
            return AEGIS_ERR_PARSE;
        }
        for (index = 0U;
             index < request->context->message_count;
             ++index) {
            if (request->context->messages[index].role ==
                AEGIS_CONTEXT_ROLE_ASSISTANT) {
                ++response_index;
            }
        }
        if (response_index >=
            (size_t)cJSON_GetArraySize(responses)) {
            response_index =
                (size_t)cJSON_GetArraySize(responses) - 1U;
        }
        selected = cJSON_GetArrayItem(responses, (int)response_index);
        if (cJSON_IsString(selected)) {
            response->content = aegis_strdup(selected->valuestring);
        } else if (cJSON_IsObject(selected)) {
            char *rendered = cJSON_PrintUnformatted(selected);
            response->content = rendered ? aegis_strdup(rendered) : NULL;
            cJSON_free(rendered);
        }
        cJSON_Delete(responses);
    } else {
        response->content = aegis_strdup(
            configured && configured[0]
                ? configured
                : "{\"type\":\"final\",\"message\":"
                  "\"Mock provider completed the task.\"}"
        );
    }
    response->raw_json = aegis_strdup(response->content);
    snprintf(response->finish_reason, sizeof(response->finish_reason), "stop");
    return response->content && response->raw_json ? AEGIS_OK : AEGIS_ERR_OOM;
}

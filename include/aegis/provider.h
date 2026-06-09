#ifndef AEGIS_PROVIDER_H
#define AEGIS_PROVIDER_H

#include <stddef.h>

#include "aegis/config.h"
#include "aegis/context.h"
#include "aegis/error.h"

typedef struct {
    const AegisConfig *config;
    const AegisContext *context;
    const char *model;
    const char *session_id;
    AegisCancelFn is_cancelled;
    void *cancel_userdata;
} AegisLLMRequest;

typedef struct {
    char *content;
    char *raw_json;
    char *error_message;
    char finish_reason[32];
    long http_status;
    long latency_ms;
    long prompt_tokens;
    long completion_tokens;
} AegisLLMResponse;

typedef struct AegisProvider AegisProvider;

typedef AegisStatus (*AegisGenerateFn)(
    const AegisProvider *provider,
    const AegisLLMRequest *request,
    AegisLLMResponse *response
);

struct AegisProvider {
    const char *name;
    const AegisConfig *config;
    AegisGenerateFn generate;
};

void aegis_llm_response_init(AegisLLMResponse *response);
void aegis_llm_response_free(AegisLLMResponse *response);
AegisStatus aegis_provider_create(
    const AegisConfig *config,
    AegisProvider *provider
);
AegisProvider aegis_provider_openrouter(const AegisConfig *config);
AegisProvider aegis_provider_openai_compat(const AegisConfig *config);
AegisProvider aegis_provider_ollama(const AegisConfig *config);
AegisProvider aegis_provider_anthropic(const AegisConfig *config);
AegisProvider aegis_provider_gemini(const AegisConfig *config);
AegisProvider aegis_provider_mock(const AegisConfig *config);
AegisStatus aegis_provider_generate_openai(
    const AegisProvider *provider,
    const AegisLLMRequest *request,
    AegisLLMResponse *response
);
AegisStatus aegis_provider_generate_ollama(
    const AegisProvider *provider,
    const AegisLLMRequest *request,
    AegisLLMResponse *response
);
AegisStatus aegis_provider_generate_anthropic(
    const AegisProvider *provider,
    const AegisLLMRequest *request,
    AegisLLMResponse *response
);
AegisStatus aegis_provider_generate_gemini(
    const AegisProvider *provider,
    const AegisLLMRequest *request,
    AegisLLMResponse *response
);
AegisStatus aegis_provider_generate_mock(
    const AegisProvider *provider,
    const AegisLLMRequest *request,
    AegisLLMResponse *response
);

#endif

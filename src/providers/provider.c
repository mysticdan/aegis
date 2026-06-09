#include <stdlib.h>
#include <string.h>

#include "aegis/provider.h"

void aegis_llm_response_init(AegisLLMResponse *response)
{
    if (response) {
        memset(response, 0, sizeof(*response));
    }
}

void aegis_llm_response_free(AegisLLMResponse *response)
{
    if (!response) {
        return;
    }
    free(response->content);
    free(response->raw_json);
    free(response->error_message);
    memset(response, 0, sizeof(*response));
}

AegisStatus aegis_provider_create(
    const AegisConfig *config,
    AegisProvider *provider
)
{
    if (!config || !provider) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    if (strcmp(config->provider, "openrouter") == 0) {
        *provider = aegis_provider_openrouter(config);
    } else if (strcmp(config->provider, "openai_compat") == 0 ||
               strcmp(config->provider, "openai") == 0) {
        *provider = aegis_provider_openai_compat(config);
    } else if (strcmp(config->provider, "ollama") == 0) {
        *provider = aegis_provider_ollama(config);
    } else if (strcmp(config->provider, "anthropic") == 0) {
        *provider = aegis_provider_anthropic(config);
    } else if (strcmp(config->provider, "gemini") == 0) {
        *provider = aegis_provider_gemini(config);
    } else if (strcmp(config->provider, "mock") == 0) {
        *provider = aegis_provider_mock(config);
    } else {
        return AEGIS_ERR_NOT_FOUND;
    }
    return provider->generate ? AEGIS_OK : AEGIS_ERR_RUNTIME;
}

#include "aegis/provider.h"

AegisProvider aegis_provider_openrouter(const AegisConfig *config)
{
    return (AegisProvider) {
        .name = "openrouter",
        .config = config,
        .generate = aegis_provider_generate_openai
    };
}

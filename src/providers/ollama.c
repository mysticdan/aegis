#include "aegis/provider.h"

AegisProvider aegis_provider_ollama(const AegisConfig *config)
{
    return (AegisProvider) {
        .name = "ollama",
        .config = config,
        .generate = aegis_provider_generate_ollama
    };
}

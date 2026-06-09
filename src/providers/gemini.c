#include "aegis/provider.h"

AegisProvider aegis_provider_gemini(const AegisConfig *config)
{
    return (AegisProvider) {
        .name = "gemini",
        .config = config,
        .generate = aegis_provider_generate_gemini
    };
}

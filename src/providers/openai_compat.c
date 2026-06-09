#include "aegis/provider.h"

AegisProvider aegis_provider_openai_compat(const AegisConfig *config)
{
    return (AegisProvider) {
        .name = "openai_compat",
        .config = config,
        .generate = aegis_provider_generate_openai
    };
}

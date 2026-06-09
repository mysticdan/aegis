#include "aegis/provider.h"

AegisProvider aegis_provider_anthropic(const AegisConfig *config)
{
    return (AegisProvider) {
        .name = "anthropic",
        .config = config,
        .generate = aegis_provider_generate_anthropic
    };
}

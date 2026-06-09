#include "aegis/provider.h"

AegisProvider aegis_provider_mock(const AegisConfig *config)
{
    return (AegisProvider) {
        .name = "mock",
        .config = config,
        .generate = aegis_provider_generate_mock
    };
}

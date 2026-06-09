#ifndef AEGIS_TOOL_REGISTRY_H
#define AEGIS_TOOL_REGISTRY_H

#include "aegis/tool.h"

#define AEGIS_TOOL_REGISTRY_MAX 64

typedef struct {
    AegisTool tools[AEGIS_TOOL_REGISTRY_MAX];
    size_t count;
} AegisToolRegistry;

void aegis_tool_registry_init(AegisToolRegistry *registry);
AegisStatus aegis_tool_registry_register(AegisToolRegistry *registry, AegisTool tool);
const AegisTool *aegis_tool_registry_find(const AegisToolRegistry *registry, const char *name);
AegisStatus aegis_tool_registry_execute(
    const AegisToolRegistry *registry,
    const char *name,
    const AegisToolArgs *args,
    const AegisToolContext *ctx,
    AegisToolResult *out
);
AegisStatus aegis_tool_registry_register_defaults(
    AegisToolRegistry *registry
);
AegisStatus aegis_tool_registry_validate_config(
    const AegisToolRegistry *registry,
    const AegisConfig *config
);
AegisStatus aegis_tool_registry_validate_profile(
    const AegisToolRegistry *registry,
    const AegisAgentProfile *profile
);
const char *aegis_tool_risk_name(AegisRiskLevel risk_level);

#endif

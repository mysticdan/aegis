#ifndef AEGIS_MCP_H
#define AEGIS_MCP_H

#include "aegis/tool.h"

AegisStatus aegis_mcp_request(
    const AegisToolContext *context,
    const char *server,
    const char *method,
    const char *params_json,
    AegisToolResult *result
);

#endif

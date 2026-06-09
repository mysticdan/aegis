#ifndef AEGIS_TOOL_HTTP_H
#define AEGIS_TOOL_HTTP_H

#include "aegis/tool.h"

AegisStatus aegis_tool_http_request(
    const AegisToolContext *context,
    const char *method,
    const char *url,
    const char *body,
    AegisToolResult *result
);

#endif

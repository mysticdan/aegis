#ifndef AEGIS_AGENT_H
#define AEGIS_AGENT_H

#include "aegis/error.h"
#include "aegis/message.h"
#include "aegis/response.h"
#include "aegis/config.h"

typedef struct {
    int step_count;
    int finished;
} AegisAgentState;

AegisStatus aegis_agent_run(
    const AegisConfig *cfg,
    const AegisMessage *msg,
    AegisResponse *out
);

#endif

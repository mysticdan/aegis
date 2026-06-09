#ifndef AEGIS_ACTION_H
#define AEGIS_ACTION_H

#include <cjson/cJSON.h>

#include "aegis/error.h"

#define AEGIS_ACTION_TOOL_MAX 64

typedef enum {
    AEGIS_ACTION_INVALID = 0,
    AEGIS_ACTION_FINAL,
    AEGIS_ACTION_TOOL_CALL
} AegisActionType;

typedef struct {
    AegisActionType type;
    char tool[AEGIS_ACTION_TOOL_MAX];
    char *message;
    cJSON *arguments;
} AegisAction;

void aegis_action_init(AegisAction *action);
void aegis_action_clear(AegisAction *action);
AegisStatus aegis_action_parse(const char *text, AegisAction *action);

#endif

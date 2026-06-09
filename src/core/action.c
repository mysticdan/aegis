#include <stdlib.h>
#include <string.h>

#include "aegis/action.h"
#include "aegis/str.h"

void aegis_action_init(AegisAction *action)
{
    if (action) {
        memset(action, 0, sizeof(*action));
    }
}

void aegis_action_clear(AegisAction *action)
{
    if (!action) {
        return;
    }
    free(action->message);
    cJSON_Delete(action->arguments);
    memset(action, 0, sizeof(*action));
}

AegisStatus aegis_action_parse(const char *text, AegisAction *action)
{
    cJSON *root;
    cJSON *type;
    cJSON *value;
    const char *end = NULL;

    if (!text || !action) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    root = cJSON_ParseWithOpts(text, &end, 1);
    if (!root || !cJSON_IsObject(root) || !end || *end != '\0') {
        cJSON_Delete(root);
        return AEGIS_ERR_PARSE;
    }

    type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || !type->valuestring) {
        cJSON_Delete(root);
        return AEGIS_ERR_PARSE;
    }

    aegis_action_clear(action);
    if (strcmp(type->valuestring, "final") == 0) {
        value = cJSON_GetObjectItemCaseSensitive(root, "message");
        if (cJSON_GetArraySize(root) != 2 ||
            !cJSON_IsString(value) || !value->valuestring) {
            cJSON_Delete(root);
            return AEGIS_ERR_PARSE;
        }
        action->message = aegis_strdup(value->valuestring);
        if (!action->message) {
            cJSON_Delete(root);
            return AEGIS_ERR_OOM;
        }
        action->type = AEGIS_ACTION_FINAL;
    } else if (strcmp(type->valuestring, "tool_call") == 0) {
        cJSON *arguments;

        value = cJSON_GetObjectItemCaseSensitive(root, "tool");
        arguments = cJSON_GetObjectItemCaseSensitive(root, "arguments");
        if (cJSON_GetArraySize(root) != 3 ||
            !cJSON_IsString(value) || !value->valuestring ||
            value->valuestring[0] == '\0' ||
            !cJSON_IsObject(arguments) ||
            strlen(value->valuestring) >= sizeof(action->tool)) {
            cJSON_Delete(root);
            return AEGIS_ERR_PARSE;
        }
        memcpy(action->tool, value->valuestring, strlen(value->valuestring) + 1U);
        action->arguments = cJSON_Duplicate(arguments, 1);
        if (!action->arguments) {
            cJSON_Delete(root);
            return AEGIS_ERR_OOM;
        }
        action->type = AEGIS_ACTION_TOOL_CALL;
    } else {
        cJSON_Delete(root);
        return AEGIS_ERR_PARSE;
    }

    cJSON_Delete(root);
    return AEGIS_OK;
}

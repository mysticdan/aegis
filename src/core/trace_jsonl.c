#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cjson/cJSON.h>

#include "aegis/trace.h"

static long long unix_milliseconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        return 0;
    }
    return (long long)now.tv_sec * 1000LL + now.tv_nsec / 1000000L;
}

static int contains_case_insensitive(
    const char *value,
    const char *needle
)
{
    size_t needle_length;

    if (!value || !needle) {
        return 0;
    }
    needle_length = strlen(needle);
    while (*value) {
        size_t index;

        for (index = 0U; index < needle_length; ++index) {
            if (!value[index] ||
                tolower((unsigned char)value[index]) !=
                    tolower((unsigned char)needle[index])) {
                break;
            }
        }
        if (index == needle_length) {
            return 1;
        }
        ++value;
    }
    return needle_length == 0U;
}

static int sensitive_key(const char *key)
{
    return key &&
        (contains_case_insensitive(key, "secret") ||
         contains_case_insensitive(key, "token") ||
         contains_case_insensitive(key, "password") ||
         contains_case_insensitive(key, "api_key") ||
         contains_case_insensitive(key, "authorization"));
}

static char *redacted_copy(const char *value, const char *secret)
{
    static const char marker[] = "[REDACTED]";
    const char *match;
    size_t secret_length;
    size_t count = 0U;
    size_t length;
    char *output;
    char *cursor;

    if (!value) {
        return NULL;
    }
    if (!secret || !secret[0]) {
        output = malloc(strlen(value) + 1U);
        if (output) {
            memcpy(output, value, strlen(value) + 1U);
        }
        return output;
    }
    secret_length = strlen(secret);
    for (match = value; (match = strstr(match, secret)) != NULL;
         match += secret_length) {
        ++count;
    }
    length = strlen(value) - count * secret_length +
        count * strlen(marker);
    output = malloc(length + 1U);
    if (!output) {
        return NULL;
    }
    cursor = output;
    while ((match = strstr(value, secret)) != NULL) {
        size_t prefix = (size_t)(match - value);
        memcpy(cursor, value, prefix);
        cursor += prefix;
        memcpy(cursor, marker, strlen(marker));
        cursor += strlen(marker);
        value = match + secret_length;
    }
    memcpy(cursor, value, strlen(value) + 1U);
    return output;
}

static int redact_json(cJSON *item, const char *secret)
{
    cJSON *child;

    if (!item) {
        return 1;
    }
    if (cJSON_IsString(item) && item->valuestring) {
        char *replacement = sensitive_key(item->string)
            ? redacted_copy("[REDACTED]", NULL)
            : redacted_copy(item->valuestring, secret);
        if (!replacement) {
            return 0;
        }
        cJSON_SetValuestring(item, replacement);
        free(replacement);
        return 1;
    }
    cJSON_ArrayForEach(child, item) {
        if (!redact_json(child, secret)) {
            return 0;
        }
    }
    return 1;
}

AegisStatus aegis_trace_open(AegisTrace *trace, const char *path)
{
    if (!trace || !path || !path[0] || strlen(path) >= sizeof(trace->path)) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    memset(trace, 0, sizeof(*trace));
    memcpy(trace->path, path, strlen(path) + 1U);
    trace->file = fopen(path, "a+b");
    if (!trace->file) {
        return AEGIS_ERR_IO;
    }
    if (fseek(trace->file, 0, SEEK_SET) == 0) {
        int value;
        while ((value = fgetc(trace->file)) != EOF) {
            if (value == '\n') {
                ++trace->sequence;
            }
        }
        clearerr(trace->file);
        (void)fseek(trace->file, 0, SEEK_END);
    }
    return AEGIS_OK;
}

void aegis_trace_set_redaction(
    AegisTrace *trace,
    int enabled,
    const char *secret
)
{
    if (!trace) {
        return;
    }
    trace->redact_secrets = enabled != 0;
    trace->secret[0] = '\0';
    if (secret && strlen(secret) < sizeof(trace->secret)) {
        memcpy(trace->secret, secret, strlen(secret) + 1U);
    }
}

AegisStatus aegis_trace_event(
    AegisTrace *trace,
    const char *session_id,
    int step,
    const char *type,
    const char *payload_json
)
{
    cJSON *root;
    cJSON *payload = NULL;
    char *line;

    if (!trace || !trace->file || !session_id || !type) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    if (payload_json) {
        payload = cJSON_Parse(payload_json);
        if (!payload) {
            payload = cJSON_CreateString(payload_json);
        }
    } else {
        payload = cJSON_CreateObject();
    }
    if (trace->redact_secrets &&
        !redact_json(payload, trace->secret)) {
        cJSON_Delete(payload);
        return AEGIS_ERR_OOM;
    }
    root = cJSON_CreateObject();
    if (!root || !payload) {
        cJSON_Delete(root);
        cJSON_Delete(payload);
        return AEGIS_ERR_OOM;
    }
    cJSON_AddNumberToObject(root, "schema_version", 1);
    cJSON_AddNumberToObject(root, "sequence", (double)++trace->sequence);
    cJSON_AddNumberToObject(root, "timestamp_ms", (double)unix_milliseconds());
    cJSON_AddStringToObject(root, "session_id", session_id);
    cJSON_AddNumberToObject(root, "step", step);
    cJSON_AddStringToObject(root, "type", type);
    cJSON_AddItemToObject(root, "payload", payload);

    line = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!line) {
        return AEGIS_ERR_OOM;
    }
    if (fprintf(trace->file, "%s\n", line) < 0 || fflush(trace->file) != 0) {
        cJSON_free(line);
        return AEGIS_ERR_IO;
    }
    cJSON_free(line);
    return AEGIS_OK;
}

void aegis_trace_close(AegisTrace *trace)
{
    if (!trace) {
        return;
    }
    if (trace->file) {
        fclose(trace->file);
    }
    memset(trace, 0, sizeof(*trace));
}

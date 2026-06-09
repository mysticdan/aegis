#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aegis/trace_reader.h"

void aegis_trace_document_clear(AegisTraceDocument *document)
{
    size_t index;

    if (!document) {
        return;
    }
    for (index = 0U; index < document->count; ++index) {
        cJSON_Delete(document->events[index]);
    }
    free(document->events);
    memset(document, 0, sizeof(*document));
}

AegisStatus aegis_trace_document_load(
    const char *path,
    AegisTraceDocument *document
)
{
    FILE *file;
    char *line = NULL;
    size_t line_capacity = 0U;
    ssize_t length;
    size_t capacity = 0U;
    unsigned long previous_sequence = 0U;

    if (!path || !document) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    memset(document, 0, sizeof(*document));
    file = fopen(path, "rb");
    if (!file) {
        return AEGIS_ERR_NOT_FOUND;
    }
    while ((length = getline(&line, &line_capacity, file)) >= 0) {
        cJSON *event;
        const char *end = NULL;

        if ((size_t)length > 1024U * 1024U) {
            free(line);
            fclose(file);
            aegis_trace_document_clear(document);
            return AEGIS_ERR_PARSE;
        }
        event = cJSON_ParseWithOpts(line, &end, 0);
        if (!event || !cJSON_IsObject(event)) {
            cJSON_Delete(event);
            free(line);
            fclose(file);
            aegis_trace_document_clear(document);
            return AEGIS_ERR_PARSE;
        }
        while (end && (*end == '\r' || *end == '\n' ||
                       *end == ' ' || *end == '\t')) {
            ++end;
        }
        if (!end || *end) {
            cJSON_Delete(event);
            free(line);
            fclose(file);
            aegis_trace_document_clear(document);
            return AEGIS_ERR_PARSE;
        }
        {
            cJSON *schema =
                cJSON_GetObjectItemCaseSensitive(event, "schema_version");
            cJSON *sequence =
                cJSON_GetObjectItemCaseSensitive(event, "sequence");
            cJSON *timestamp =
                cJSON_GetObjectItemCaseSensitive(event, "timestamp_ms");
            cJSON *session =
                cJSON_GetObjectItemCaseSensitive(event, "session_id");
            cJSON *step =
                cJSON_GetObjectItemCaseSensitive(event, "step");
            cJSON *type =
                cJSON_GetObjectItemCaseSensitive(event, "type");
            cJSON *payload =
                cJSON_GetObjectItemCaseSensitive(event, "payload");
            unsigned long current_sequence;

            current_sequence = cJSON_IsNumber(sequence)
                ? (unsigned long)sequence->valuedouble
                : 0U;
            if (!cJSON_IsNumber(schema) || schema->valueint != 1 ||
                !cJSON_IsNumber(sequence) ||
                sequence->valuedouble < 1.0 ||
                sequence->valuedouble != (double)current_sequence ||
                !cJSON_IsNumber(timestamp) ||
                !cJSON_IsString(session) || !session->valuestring[0] ||
                !cJSON_IsNumber(step) || step->valuedouble < 0.0 ||
                !cJSON_IsString(type) || !type->valuestring[0] ||
                !payload) {
                cJSON_Delete(event);
                free(line);
                fclose(file);
                aegis_trace_document_clear(document);
                return AEGIS_ERR_PARSE;
            }
            if (current_sequence <= previous_sequence) {
                cJSON_Delete(event);
                free(line);
                fclose(file);
                aegis_trace_document_clear(document);
                return AEGIS_ERR_PARSE;
            }
            previous_sequence = current_sequence;
        }
        if (document->count == capacity) {
            size_t next = capacity ? capacity * 2U : 32U;
            cJSON **resized = realloc(
                document->events, next * sizeof(*resized));
            if (!resized) {
                cJSON_Delete(event);
                free(line);
                fclose(file);
                aegis_trace_document_clear(document);
                return AEGIS_ERR_OOM;
            }
            document->events = resized;
            capacity = next;
        }
        document->events[document->count++] = event;
    }
    free(line);
    if (ferror(file) || fclose(file) != 0 || document->count == 0U) {
        aegis_trace_document_clear(document);
        return AEGIS_ERR_PARSE;
    }
    return AEGIS_OK;
}

#ifndef AEGIS_CONTEXT_H
#define AEGIS_CONTEXT_H

#include <stddef.h>

#include "aegis/config.h"
#include "aegis/error.h"
#include "aegis/message.h"
#include "aegis/tool_registry.h"

typedef enum {
    AEGIS_CONTEXT_ROLE_SYSTEM = 0,
    AEGIS_CONTEXT_ROLE_USER = 1,
    AEGIS_CONTEXT_ROLE_ASSISTANT = 2,
    AEGIS_CONTEXT_ROLE_TOOL = 3
} AegisContextRole;

typedef enum {
    AEGIS_CONTEXT_EVENT_MESSAGE = 0,
    AEGIS_CONTEXT_EVENT_OBSERVATION = 1,
    AEGIS_CONTEXT_EVENT_FILE_READ = 2
} AegisContextEventKind;

/* Borrowed event supplied by the caller. */
typedef struct {
    AegisContextRole role;
    AegisContextEventKind kind;
    const char *name;
    const char *path;
    const char *content;
} AegisContextEvent;

typedef struct {
    const AegisMessage *current_message;
    const AegisContextEvent *history;
    size_t history_count;
    const char *workspace_summary;
    const char *history_summary;
} AegisContextBuildInput;

typedef struct {
    AegisContextRole role;
    AegisContextEventKind kind;
    char *name;
    char *path;
    char *content;
} AegisContextMessage;

typedef struct {
    char *name;
    char *description;
    char *schema_json;
    char *policy_decision;
    char *risk;
} AegisContextTool;

typedef struct {
    AegisContextMessage *messages;
    size_t message_count;
    AegisContextTool *tools;
    size_t tool_count;
    size_t total_chars;
    int truncated;
    size_t dropped_history_count;
} AegisContext;

/*
 * Initialize before the first build. A successful build replaces the previous
 * owned output; a failed build leaves it unchanged. clear() is repeatable.
 */
void aegis_context_init(AegisContext *context);
void aegis_context_clear(AegisContext *context);
AegisStatus aegis_context_build(
    AegisContext *out,
    const AegisConfig *config,
    const AegisToolRegistry *registry,
    const AegisContextBuildInput *input
);
const char *aegis_context_role_name(AegisContextRole role);
const char *aegis_context_event_kind_name(AegisContextEventKind kind);

#endif

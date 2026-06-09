#ifndef AEGIS_TOOL_H
#define AEGIS_TOOL_H

#include <stddef.h>

#include "aegis/config.h"
#include "aegis/error.h"
#include "aegis/message.h"

#define AEGIS_TOOL_COUNT 20

#define AEGIS_TOOL_LIST_DIR "list_dir"
#define AEGIS_TOOL_READ_FILE "read_file"
#define AEGIS_TOOL_WRITE_FILE "write_file"
#define AEGIS_TOOL_APPEND_FILE "append_file"
#define AEGIS_TOOL_SEARCH_FILE "search_file"
#define AEGIS_TOOL_SHELL "shell"
#define AEGIS_TOOL_RUN_TESTS "run_tests"
#define AEGIS_TOOL_GIT_STATUS "git_status"
#define AEGIS_TOOL_GIT_DIFF "git_diff"
#define AEGIS_TOOL_GIT_LOG "git_log"
#define AEGIS_TOOL_GIT_APPLY_PATCH "git_apply_patch"
#define AEGIS_TOOL_HTTP_GET "http_get"
#define AEGIS_TOOL_HTTP_POST "http_post"
#define AEGIS_TOOL_ASK_USER "ask_user"
#define AEGIS_TOOL_SEND_MESSAGE "send_message"
#define AEGIS_TOOL_REMINDER "reminder"
#define AEGIS_TOOL_READ_LOG "read_log"
#define AEGIS_TOOL_GREP_LOG "grep_log"
#define AEGIS_TOOL_HEALTH_CHECK "health_check"
#define AEGIS_TOOL_MCP "mcp_tool"

typedef AegisStatus (*AegisPersistReminderFn)(
    void *userdata,
    const char *session_id,
    const char *message,
    const char *due
);
typedef int (*AegisToolCancelledFn)(void *userdata);

typedef enum {
    AEGIS_RISK_LOW = 0,
    AEGIS_RISK_MEDIUM = 1,
    AEGIS_RISK_HIGH = 2,
    AEGIS_RISK_CRITICAL = 3
} AegisRiskLevel;

typedef enum {
    AEGIS_TOOL_READY = 0,
    AEGIS_TOOL_STUB = 1
} AegisToolAvailability;

typedef struct {
    const char *key;
    const char *value;
} AegisKv;

typedef struct {
    const AegisKv *items;
    size_t count;
} AegisToolArgs;

typedef struct {
    const AegisConfig *config;
    const char *workspace_root;
    int approval_granted;

    /* Derived compatibility flags populated from config. */
    int allow_write;
    int allow_shell;
    int allow_network;
    size_t max_output_bytes;
    AegisAskUserFn ask_user;
    AegisSendMessageFn send_message;
    void *adapter_userdata;
    AegisPersistReminderFn persist_reminder;
    AegisToolCancelledFn is_cancelled;
    void *state_userdata;
    const char *session_id;
} AegisToolContext;

typedef struct {
    int ok;
    int exit_code;
    char *stdout_data;
    char *stderr_data;
    char *error_message;
    size_t output_bytes;
    long duration_ms;
} AegisToolResult;

typedef AegisStatus (*AegisToolExecuteFn)(
    const AegisToolArgs *args,
    const AegisToolContext *ctx,
    AegisToolResult *out
);

typedef struct {
    const char *name;
    const char *description;
    const char *schema_json;
    AegisRiskLevel risk_level;
    AegisToolAvailability availability;
    AegisToolExecuteFn execute;
} AegisTool;

void aegis_tool_context_from_config(
    AegisToolContext *context,
    const AegisConfig *config,
    const char *workspace_root,
    int approval_granted
);
const char *aegis_tool_args_get(const AegisToolArgs *args, const char *key);
void aegis_tool_result_init(AegisToolResult *result);
void aegis_tool_result_clear(AegisToolResult *result);
AegisStatus aegis_tool_result_set_stdout(AegisToolResult *result, const char *text);
AegisStatus aegis_tool_result_set_error(AegisToolResult *result, const char *text);

AegisTool aegis_tool_read_file(void);
AegisTool aegis_tool_write_file(void);
AegisTool aegis_tool_append_file(void);
AegisTool aegis_tool_list_dir(void);
AegisTool aegis_tool_search_file(void);
AegisTool aegis_tool_shell(void);
AegisTool aegis_tool_run_tests(void);
AegisTool aegis_tool_http_get(void);
AegisTool aegis_tool_http_post(void);
AegisTool aegis_tool_git_status(void);
AegisTool aegis_tool_git_diff(void);
AegisTool aegis_tool_git_log(void);
AegisTool aegis_tool_git_apply_patch(void);
AegisTool aegis_tool_ask_user(void);
AegisTool aegis_tool_send_message(void);
AegisTool aegis_tool_reminder(void);
AegisTool aegis_tool_read_log(void);
AegisTool aegis_tool_grep_log(void);
AegisTool aegis_tool_health_check(void);
AegisTool aegis_tool_mcp_tool(void);

#endif

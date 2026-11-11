#ifndef AEGIS_MESSAGE_H
#define AEGIS_MESSAGE_H

#include <stddef.h>

typedef int (*AegisAskUserFn)(
    void *userdata,
    const char *question,
    char **answer
);
typedef int (*AegisSendMessageFn)(
    void *userdata,
    const char *message
);
typedef int (*AegisCancelFn)(void *userdata);

/* Synchronous, borrowed progress data; valid during callback only. */
typedef enum {
    AEGIS_PROGRESS_MODEL_REQUEST = 0,
    AEGIS_PROGRESS_MODEL_RESPONSE,
    AEGIS_PROGRESS_REASONING_SUMMARY,
    AEGIS_PROGRESS_MODEL_ERROR,
    AEGIS_PROGRESS_INVALID_RESPONSE,
    AEGIS_PROGRESS_TOOL_CALL,
    AEGIS_PROGRESS_APPROVAL,
    AEGIS_PROGRESS_TOOL_RESULT
} AegisProgressKind;

typedef struct {
    AegisProgressKind kind;
    int step;
    const char *tool;
    const char *payload_json;
    const char *decision;
    const char *risk;
    const char *detail;
    long duration_ms;
    size_t output_bytes;
    int exit_code;
    int success;
} AegisProgressEvent;

typedef void (*AegisProgressFn)(void *userdata, const AegisProgressEvent *event);

typedef struct {
    const char *channel;
    const char *user_id;
    const char *session_id;
    const char *text;
    const char *workspace;
    const char *profile;
    const char *trace_path;
    int auto_approve;
    int no_input;
    int initial_step;
    AegisAskUserFn ask_user;
    AegisSendMessageFn send_message;
    AegisCancelFn is_cancelled;
    AegisProgressFn progress;
    void *progress_userdata;
    void *adapter_userdata;
} AegisMessage;

int aegis_message_is_valid(const AegisMessage *msg);

#endif

#ifndef AEGIS_MESSAGE_H
#define AEGIS_MESSAGE_H

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
    void *adapter_userdata;
} AegisMessage;

int aegis_message_is_valid(const AegisMessage *msg);

#endif

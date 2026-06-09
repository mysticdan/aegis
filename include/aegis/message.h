#ifndef AEGIS_MESSAGE_H
#define AEGIS_MESSAGE_H

typedef struct {
    const char *channel;
    const char *user_id;
    const char *session_id;
    const char *text;
    const char *workspace;
    const char *profile;
} AegisMessage;

int aegis_message_is_valid(const AegisMessage *msg);

#endif

#include "aegis/message.h"

static int has_text(const char *value)
{
    return value && value[0] != '\0';
}

int aegis_message_is_valid(const AegisMessage *msg)
{
    return msg &&
        has_text(msg->channel) &&
        has_text(msg->user_id) &&
        has_text(msg->session_id) &&
        has_text(msg->text);
}

#include "aegis/message.h"

int aegis_message_is_valid(const AegisMessage *msg) {
    return msg && msg->channel && msg->user_id && msg->session_id && msg->text;
}

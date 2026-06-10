#ifndef AEGIS_RESPONSE_H
#define AEGIS_RESPONSE_H

typedef struct {
    char *text;
    char *error_message;
    int exit_code;
    char session_id[96];
    char trace_path[4096];
    char status[32];
    int steps;
} AegisResponse;

void aegis_response_init(AegisResponse *response);
void aegis_response_free(AegisResponse *response);
int aegis_response_set_text(AegisResponse *response, const char *text);

#endif

#ifndef AEGIS_RESPONSE_H
#define AEGIS_RESPONSE_H

typedef struct {
    const char *text;
    int exit_code;
} AegisResponse;

void aegis_response_init(AegisResponse *response);
void aegis_response_free(AegisResponse *response);
int aegis_response_set_text(AegisResponse *response, const char *text);

#endif
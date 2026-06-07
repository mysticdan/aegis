#include <stdlib.h>
#include "aegis/response.h"
#include "aegis/str.h"  

void aegis_response_init(AegisResponse *response) {
    if (!response) return;
    response->text = NULL;
    response->exit_code = 0;
}

void aegis_response_free(AegisResponse *response) {
    if (!response) return;
    free(response->text);
    response->text = NULL;
    response->exit_code = 0;
}

int aegis_response_set_text(AegisResponse *response, const char *text) {
    if (!response) return 0;
    free(response->text);
    response->text = aegis_strdup(text);
    return response->text != NULL;
}
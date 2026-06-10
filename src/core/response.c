#include <stdlib.h>
#include <string.h>
#include "aegis/response.h"
#include "aegis/str.h"  

void aegis_response_init(AegisResponse *response) {
    if (!response) return;
    memset(response, 0, sizeof(*response));
}

void aegis_response_free(AegisResponse *response) {
    if (!response) return;
    free(response->text);
    free(response->error_message);
    memset(response, 0, sizeof(*response));
}

int aegis_response_set_text(AegisResponse *response, const char *text) {
    if (!response) return 0;
    free(response->text);
    response->text = aegis_strdup(text);
    return response->text != NULL;
}

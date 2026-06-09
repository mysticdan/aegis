#ifndef AEGIS_RUNTIME_H
#define AEGIS_RUNTIME_H

#include "aegis/error.h"
#include "aegis/config.h"
#include "aegis/message.h"
#include "aegis/response.h"

typedef struct AegisRuntime AegisRuntime;

AegisRuntime *aegis_runtime_new(const char *config_path);
AegisRuntime *aegis_runtime_new_with_config(const AegisConfig *config);
void aegis_runtime_free(AegisRuntime *runtime);
AegisStatus aegis_runtime_handle_message(AegisRuntime *runtime, const AegisMessage *msg, AegisResponse *response);

#endif

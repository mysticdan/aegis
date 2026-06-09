#include "aegis/runtime.h"

struct AegisRuntime {
    AegisConfig config;
    char *config_path;
};

AegisRuntime *aegis_runtime_new(const char *config_path) {
    
    return runtime;
}
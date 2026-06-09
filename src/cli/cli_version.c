#include <stdio.h>

#include <cjson/cJSON.h>

#include "aegis/cli_command.h"

static const char *platform_name(void)
{
#if defined(__linux__) && defined(__x86_64__)
    return "linux-x86_64";
#elif defined(__linux__) && defined(__aarch64__)
    return "linux-aarch64";
#elif defined(__APPLE__) && defined(__aarch64__)
    return "macos-aarch64";
#elif defined(__APPLE__) && defined(__x86_64__)
    return "macos-x86_64";
#else
    return "unknown";
#endif
}

int aegis_cli_cmd_version(const CliOptions *options)
{
    if (options->positional_count != 0U) {
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "version does not accept positional arguments");
    }
    if (options->json) {
        cJSON *root = cJSON_CreateObject();
        cJSON *features = cJSON_CreateArray();
        cJSON_AddStringToObject(root, "status", "success");
        cJSON_AddStringToObject(root, "command", "version");
        cJSON_AddStringToObject(root, "version", AEGIS_CLI_VERSION);
        cJSON_AddStringToObject(root, "platform", platform_name());
        cJSON_AddItemToObject(root, "features", features);
        cJSON_AddItemToArray(features, cJSON_CreateString("runtime"));
        cJSON_AddItemToArray(features, cJSON_CreateString("providers"));
        cJSON_AddItemToArray(features, cJSON_CreateString("tools"));
        cJSON_AddItemToArray(features, cJSON_CreateString("sqlite"));
        cJSON_AddItemToArray(features, cJSON_CreateString("trace"));
        cJSON_AddItemToArray(features, cJSON_CreateString("mcp"));
        cli_json_print(root);
        cJSON_Delete(root);
    } else {
        printf("aegis %s\n", AEGIS_CLI_VERSION);
        if (options->verbose) {
            printf("platform: %s\n", platform_name());
            puts("features: runtime,providers,tools,sqlite,trace,mcp");
        }
    }
    return AEGIS_CLI_EXIT_SUCCESS;
}

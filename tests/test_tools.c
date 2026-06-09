#define _XOPEN_SOURCE 700

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#include "aegis/config.h"
#include "aegis/tool_registry.h"

static AegisToolArgs args_one(AegisKv *items, const char *key, const char *value)
{
    items[0].key = key;
    items[0].value = value;
    return (AegisToolArgs) { .items = items, .count = 1 };
}

static AegisToolArgs args_two(
    AegisKv *items,
    const char *key1,
    const char *value1,
    const char *key2,
    const char *value2
)
{
    items[0].key = key1;
    items[0].value = value1;
    items[1].key = key2;
    items[1].value = value2;
    return (AegisToolArgs) { .items = items, .count = 2 };
}

static void write_fixture(const char *path, const char *content)
{
    FILE *file = fopen(path, "wb");
    size_t length = strlen(content);

    assert(file != NULL);
    assert(fwrite(content, 1, length, file) == length);
    assert(fclose(file) == 0);
}

static void reset_result(AegisToolResult *result)
{
    aegis_tool_result_clear(result);
    aegis_tool_result_init(result);
}

static void test_catalog(AegisToolRegistry *registry)
{
    size_t index;
    size_t ready_count = 0;
    cJSON *schema;

    assert(registry->count == AEGIS_TOOL_COUNT);
    for (index = 0; index < registry->count; ++index) {
        schema = cJSON_ParseWithOpts(
            registry->tools[index].schema_json,
            NULL,
            1
        );
        assert(schema != NULL);
        assert(cJSON_IsObject(schema));
        cJSON_Delete(schema);

        if (registry->tools[index].availability == AEGIS_TOOL_READY) {
            ++ready_count;
        }
    }
    assert(ready_count == 4);
    assert(aegis_tool_registry_register(
        registry,
        aegis_tool_read_file()
    ) == AEGIS_ERR_INVALID_ARGUMENT);
}

static void test_configs(AegisToolRegistry *registry)
{
    static const char *presets[] = {
        "aegis", "safe", "dev", "dangerous"
    };
    static const char *profiles[] = {
        "minimal_agent",
        "coding_agent",
        "security_agent",
        "ops_agent",
        "assistant_agent"
    };
    AegisConfig config;
    AegisAgentProfile profile;
    char path[AEGIS_CONFIG_PATH_MAX];
    size_t index;

    aegis_config_defaults(&config);
    assert(aegis_tool_registry_validate_config(
        registry,
        &config
    ) == AEGIS_OK);

    for (index = 0; index < sizeof(presets) / sizeof(presets[0]); ++index) {
        assert(aegis_config_load_preset(presets[index], &config) == AEGIS_OK);
        assert(aegis_tool_registry_validate_config(
            registry,
            &config
        ) == AEGIS_OK);
    }

    for (index = 0; index < sizeof(profiles) / sizeof(profiles[0]); ++index) {
        assert(snprintf(
            path,
            sizeof(path),
            "profiles/%s.json",
            profiles[index]
        ) > 0);
        assert(aegis_agent_profile_load_json(path, &profile) == AEGIS_OK);
        assert(aegis_tool_registry_validate_profile(
            registry,
            &profile
        ) == AEGIS_OK);
    }
}

static void test_policy(AegisToolRegistry *registry, const char *workspace)
{
    AegisConfig config;
    AegisToolContext context;
    AegisToolResult result;
    AegisKv items[2];
    AegisToolArgs arguments;

    aegis_tool_result_init(&result);
    assert(aegis_config_load_preset("aegis", &config) == AEGIS_OK);
    arguments = args_two(items, "path", "approval.txt", "content", "ok");

    aegis_tool_context_from_config(&context, &config, workspace, 0);
    assert(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_WRITE_FILE,
        &arguments,
        &context,
        &result
    ) == AEGIS_ERR_POLICY_DENIED);

    reset_result(&result);
    aegis_tool_context_from_config(&context, &config, workspace, 1);
    assert(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_WRITE_FILE,
        &arguments,
        &context,
        &result
    ) == AEGIS_OK);

    reset_result(&result);
    assert(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_SHELL,
        NULL,
        &context,
        &result
    ) == AEGIS_ERR_NOT_IMPLEMENTED);

    reset_result(&result);
    assert(aegis_config_load_preset("safe", &config) == AEGIS_OK);
    aegis_tool_context_from_config(&context, &config, workspace, 1);
    assert(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_WRITE_FILE,
        &arguments,
        &context,
        &result
    ) == AEGIS_ERR_POLICY_DENIED);
    aegis_tool_result_clear(&result);
}

static void test_file_tools(AegisToolRegistry *registry, const char *workspace)
{
    AegisConfig config;
    AegisToolContext context;
    AegisToolResult result;
    AegisKv items[2];
    AegisToolArgs arguments;
    char path[AEGIS_CONFIG_PATH_MAX];

    assert(aegis_config_load_preset("dev", &config) == AEGIS_OK);
    aegis_tool_context_from_config(&context, &config, workspace, 1);
    aegis_tool_result_init(&result);

    arguments = args_two(items, "path", "note.txt", "content", "hello");
    assert(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_WRITE_FILE,
        &arguments,
        &context,
        &result
    ) == AEGIS_OK);

    reset_result(&result);
    arguments = args_two(items, "path", "note.txt", "content", " world");
    assert(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_APPEND_FILE,
        &arguments,
        &context,
        &result
    ) == AEGIS_OK);

    reset_result(&result);
    arguments = args_one(items, "path", "note.txt");
    assert(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_READ_FILE,
        &arguments,
        &context,
        &result
    ) == AEGIS_OK);
    assert(strcmp(result.stdout_data, "hello world") == 0);

    reset_result(&result);
    snprintf(path, sizeof(path), "%s/.env", workspace);
    write_fixture(path, "secret");
    arguments = args_one(items, "path", ".");
    assert(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_LIST_DIR,
        &arguments,
        &context,
        &result
    ) == AEGIS_OK);
    assert(strstr(result.stdout_data, "note.txt\n") != NULL);
    assert(strstr(result.stdout_data, ".env") == NULL);

    reset_result(&result);
    arguments = args_one(items, "path", ".env");
    assert(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_READ_FILE,
        &arguments,
        &context,
        &result
    ) == AEGIS_ERR_PATH_ESCAPE);

    reset_result(&result);
    arguments = args_one(items, "path", "../outside");
    assert(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_READ_FILE,
        &arguments,
        &context,
        &result
    ) == AEGIS_ERR_PATH_ESCAPE);

    reset_result(&result);
    arguments = args_one(items, "path", "/etc/passwd");
    assert(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_READ_FILE,
        &arguments,
        &context,
        &result
    ) == AEGIS_ERR_PATH_ESCAPE);

    snprintf(path, sizeof(path), "%s/.hidden", workspace);
    write_fixture(path, "hidden");
    config.allow_hidden_files = 0;
    reset_result(&result);
    arguments = args_one(items, "path", ".hidden");
    assert(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_READ_FILE,
        &arguments,
        &context,
        &result
    ) == AEGIS_ERR_PATH_ESCAPE);

    snprintf(path, sizeof(path), "%s/link", workspace);
    assert(symlink("/etc/passwd", path) == 0);
    reset_result(&result);
    arguments = args_one(items, "path", "link");
    assert(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_READ_FILE,
        &arguments,
        &context,
        &result
    ) == AEGIS_ERR_PATH_ESCAPE);

    config.allow_hidden_files = 1;
    context.max_output_bytes = 4;
    reset_result(&result);
    arguments = args_one(items, "path", "note.txt");
    assert(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_READ_FILE,
        &arguments,
        &context,
        &result
    ) == AEGIS_ERR_RUNTIME);

    aegis_tool_result_clear(&result);
}

int main(void)
{
    AegisToolRegistry registry;
    char workspace[] = "/tmp/aegis-tools-XXXXXX";

    assert(mkdtemp(workspace) != NULL);
    aegis_tool_registry_init(&registry);
    assert(aegis_tool_registry_register_defaults(&registry) == AEGIS_OK);

    test_catalog(&registry);
    test_configs(&registry);
    test_policy(&registry, workspace);
    test_file_tools(&registry, workspace);

    puts("aegis tool tests: ok");
    return 0;
}

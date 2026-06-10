#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#include "aegis/config.h"
#include "aegis/tool_registry.h"

#define ASSERT_OK(expr) do { \
    AegisStatus _s = (expr); \
    if (_s != AEGIS_OK) { \
        fprintf(stderr, "FAIL:%d: %s == %d\n", __LINE__, #expr, _s); \
        abort(); \
    } \
} while (0)

#define ASSERT_ERR(expr, expected) do { \
    AegisStatus _s = (expr); \
    if (_s != (expected)) { \
        fprintf(stderr, "FAIL:%d: %s == %d (expected %d)\n", \
                __LINE__, #expr, _s, (int)(expected)); \
        abort(); \
    } \
} while (0)

#define ASSERT_NOT_NULL(expr) do { \
    void *_p = (void *)(expr); \
    if (!_p) { \
        fprintf(stderr, "FAIL:%d: %s is NULL\n", __LINE__, #expr); \
        abort(); \
    } \
} while (0)

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL:%d: %s\n", __LINE__, #expr); \
        abort(); \
    } \
} while (0)

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

    ASSERT_NOT_NULL(file);
    ASSERT_TRUE(fwrite(content, 1, length, file) == length);
    ASSERT_TRUE(fclose(file) == 0);
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

    ASSERT_TRUE(registry->count == AEGIS_TOOL_COUNT);
    for (index = 0; index < registry->count; ++index) {
        schema = cJSON_ParseWithOpts(
            registry->tools[index].schema_json,
            NULL,
            1
        );
        ASSERT_NOT_NULL(schema);
        ASSERT_TRUE(cJSON_IsObject(schema));
        cJSON_Delete(schema);

        if (registry->tools[index].availability == AEGIS_TOOL_READY) {
            ++ready_count;
        }
    }
    ASSERT_TRUE(ready_count == AEGIS_TOOL_COUNT);
    ASSERT_ERR(aegis_tool_registry_register(
        registry,
        aegis_tool_read_file()
    ), AEGIS_ERR_INVALID_ARGUMENT);
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
    ASSERT_OK(aegis_tool_registry_validate_config(
        registry,
        &config
    ));

    for (index = 0; index < sizeof(presets) / sizeof(presets[0]); ++index) {
        ASSERT_OK(aegis_config_load_preset(presets[index], &config));
        ASSERT_OK(aegis_tool_registry_validate_config(
            registry,
            &config
        ));
    }

    for (index = 0; index < sizeof(profiles) / sizeof(profiles[0]); ++index) {
        ASSERT_TRUE(snprintf(
            path,
            sizeof(path),
            "profiles/%s.json",
            profiles[index]
        ) > 0);
        ASSERT_OK(aegis_agent_profile_load_json(path, &profile));
        ASSERT_OK(aegis_tool_registry_validate_profile(
            registry,
            &profile
        ));
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
    ASSERT_OK(aegis_config_load_preset("aegis", &config));
    arguments = args_two(items, "path", "approval.txt", "content", "ok");

    aegis_tool_context_from_config(&context, &config, workspace, 0);
    ASSERT_ERR(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_WRITE_FILE,
        &arguments,
        &context,
        &result
    ), AEGIS_ERR_POLICY_DENIED);

    reset_result(&result);
    aegis_tool_context_from_config(&context, &config, workspace, 1);
    ASSERT_OK(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_WRITE_FILE,
        &arguments,
        &context,
        &result
    ));

    reset_result(&result);
    arguments = args_one(items, "command", "ls");
    config.sandbox_enabled = 0;
    aegis_tool_context_from_config(&context, &config, workspace, 1);
    ASSERT_OK(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_SHELL,
        &arguments,
        &context,
        &result
    ));

    reset_result(&result);
    arguments = args_one(items, "command", "ls; pwd");
    ASSERT_ERR(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_SHELL,
        &arguments,
        &context,
        &result
    ), AEGIS_ERR_POLICY_DENIED);

    reset_result(&result);
    ASSERT_OK(aegis_config_load_preset("safe", &config));
    aegis_tool_context_from_config(&context, &config, workspace, 1);
    ASSERT_ERR(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_WRITE_FILE,
        &arguments,
        &context,
        &result
    ), AEGIS_ERR_POLICY_DENIED);
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

    ASSERT_OK(aegis_config_load_preset("dev", &config));
    aegis_tool_context_from_config(&context, &config, workspace, 1);
    aegis_tool_result_init(&result);

    arguments = args_two(items, "path", "note.txt", "content", "hello");
    ASSERT_OK(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_WRITE_FILE,
        &arguments,
        &context,
        &result
    ));

    reset_result(&result);
    arguments = args_two(items, "path", "note.txt", "content", " world");
    ASSERT_OK(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_APPEND_FILE,
        &arguments,
        &context,
        &result
    ));

    reset_result(&result);
    arguments = args_one(items, "path", "note.txt");
    ASSERT_OK(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_READ_FILE,
        &arguments,
        &context,
        &result
    ));
    ASSERT_TRUE(strcmp(result.stdout_data, "hello world") == 0);

    reset_result(&result);
    arguments = args_two(items, "query", "world", "path", ".");
    ASSERT_OK(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_SEARCH_FILE,
        &arguments,
        &context,
        &result
    ));
    ASSERT_TRUE(strstr(result.stdout_data, "note.txt:1:hello world") != NULL);

    reset_result(&result);
    snprintf(path, sizeof(path), "%s/.env", workspace);
    write_fixture(path, "secret");
    arguments = args_one(items, "path", ".");
    ASSERT_OK(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_LIST_DIR,
        &arguments,
        &context,
        &result
    ));
    ASSERT_TRUE(strstr(result.stdout_data, "note.txt\n") != NULL);
    ASSERT_TRUE(strstr(result.stdout_data, ".env") == NULL);
    ASSERT_TRUE(strstr(
        result.stdout_data, "approval.txt\nnote.txt\n"
    ) != NULL);

    reset_result(&result);
    arguments = args_one(items, "path", ".env");
    ASSERT_ERR(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_READ_FILE,
        &arguments,
        &context,
        &result
    ), AEGIS_ERR_PATH_ESCAPE);

    reset_result(&result);
    arguments = args_one(items, "path", "../outside");
    ASSERT_ERR(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_READ_FILE,
        &arguments,
        &context,
        &result
    ), AEGIS_ERR_PATH_ESCAPE);

    reset_result(&result);
    arguments = args_one(items, "path", "/etc/passwd");
    ASSERT_ERR(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_READ_FILE,
        &arguments,
        &context,
        &result
    ), AEGIS_ERR_PATH_ESCAPE);

    snprintf(path, sizeof(path), "%s/.hidden", workspace);
    write_fixture(path, "hidden");
    config.allow_hidden_files = 0;
    reset_result(&result);
    arguments = args_one(items, "path", ".hidden");
    ASSERT_ERR(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_READ_FILE,
        &arguments,
        &context,
        &result
    ), AEGIS_ERR_PATH_ESCAPE);

    snprintf(path, sizeof(path), "%s/link", workspace);
    ASSERT_TRUE(symlink("/etc/passwd", path) == 0);
    reset_result(&result);
    arguments = args_one(items, "path", "link");
    ASSERT_ERR(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_READ_FILE,
        &arguments,
        &context,
        &result
    ), AEGIS_ERR_PATH_ESCAPE);

    reset_result(&result);
    arguments = args_two(items, "query", "world", "path", ".");
    ASSERT_OK(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_SEARCH_FILE,
        &arguments,
        &context,
        &result
    ));
    ASSERT_TRUE(strstr(result.stdout_data, "note.txt:1:hello world") != NULL);

    config.allow_hidden_files = 1;
    context.max_output_bytes = 4;
    reset_result(&result);
    arguments = args_one(items, "path", "note.txt");
    ASSERT_ERR(aegis_tool_registry_execute(
        registry,
        AEGIS_TOOL_READ_FILE,
        &arguments,
        &context,
        &result
    ), AEGIS_ERR_RUNTIME);

    aegis_tool_result_clear(&result);
}

int main(void)
{
    AegisToolRegistry registry;
    char workspace[] = "/tmp/aegis-tools-XXXXXX";

    ASSERT_NOT_NULL(mkdtemp(workspace));
    aegis_tool_registry_init(&registry);
    ASSERT_OK(aegis_tool_registry_register_defaults(&registry));

    test_catalog(&registry);
    test_configs(&registry);
    test_policy(&registry, workspace);
    test_file_tools(&registry, workspace);

    puts("aegis tool tests: ok");
    return 0;
}

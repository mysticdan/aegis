#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <cjson/cJSON.h>

#include "aegis/context.h"

static AegisMessage message_with_text(const char *text)
{
    AegisMessage message = {
        .channel = "cli",
        .user_id = "user",
        .session_id = "session",
        .text = text,
        .workspace = ".",
        .profile = "coding_agent"
    };

    return message;
}

static void disable_optional_context(AegisConfig *config)
{
    config->include_system_prompt = 0;
    config->include_tool_schemas = 0;
    config->include_workspace_summary = 0;
    config->active_profile.include_workspace_summary = 0;
}

static const AegisContextMessage *find_message(
    const AegisContext *context,
    AegisContextEventKind kind
)
{
    size_t index;

    for (index = 0U; index < context->message_count; ++index) {
        if (context->messages[index].kind == kind) {
            return &context->messages[index];
        }
    }
    return NULL;
}

static void test_full_context(const AegisToolRegistry *registry)
{
    AegisConfig config;
    AegisContext context;
    char current_text[] = "implement context";
    char workspace[] = "workspace: clean";
    char old_user[] = "old user";
    char observation[] = "tests passed";
    AegisMessage current = message_with_text(current_text);
    AegisContextEvent history[] = {
        {
            .role = AEGIS_CONTEXT_ROLE_USER,
            .kind = AEGIS_CONTEXT_EVENT_MESSAGE,
            .content = old_user
        },
        {
            .role = AEGIS_CONTEXT_ROLE_ASSISTANT,
            .kind = AEGIS_CONTEXT_EVENT_MESSAGE,
            .content = "old assistant"
        },
        {
            .role = AEGIS_CONTEXT_ROLE_TOOL,
            .kind = AEGIS_CONTEXT_EVENT_OBSERVATION,
            .name = "run_tests",
            .content = observation
        },
        {
            .role = AEGIS_CONTEXT_ROLE_TOOL,
            .kind = AEGIS_CONTEXT_EVENT_FILE_READ,
            .path = "src/core/context.c",
            .content = "file content"
        }
    };
    AegisContextBuildInput input = {
        .current_message = &current,
        .history = history,
        .history_count = sizeof(history) / sizeof(history[0]),
        .workspace_summary = workspace,
        .history_summary = "older work was summarized"
    };
    static const char *expected_tools[] = {
        AEGIS_TOOL_LIST_DIR,
        AEGIS_TOOL_READ_FILE,
        AEGIS_TOOL_WRITE_FILE,
        AEGIS_TOOL_APPEND_FILE,
        AEGIS_TOOL_SEARCH_FILE,
        AEGIS_TOOL_SHELL,
        AEGIS_TOOL_RUN_TESTS,
        AEGIS_TOOL_GIT_STATUS,
        AEGIS_TOOL_GIT_DIFF,
        AEGIS_TOOL_GIT_LOG,
        AEGIS_TOOL_GIT_APPLY_PATCH
    };
    size_t index;

    assert(aegis_config_load_preset("dev", &config) == AEGIS_OK);
    aegis_context_init(&context);
    assert(aegis_context_build(
        &context,
        &config,
        registry,
        &input
    ) == AEGIS_OK);

    assert(context.message_count == 8U);
    assert(context.messages[0].role == AEGIS_CONTEXT_ROLE_SYSTEM);
    assert(strcmp(context.messages[0].name, "system_prompt") == 0);
    assert(strstr(context.messages[0].content, "Aegis Coding Agent") != NULL);
    assert(strcmp(context.messages[1].name, "workspace_summary") == 0);
    assert(strcmp(context.messages[2].name, "history_summary") == 0);
    assert(strcmp(context.messages[3].content, "old user") == 0);
    assert(strcmp(context.messages[4].content, "old assistant") == 0);
    assert(context.messages[5].kind == AEGIS_CONTEXT_EVENT_OBSERVATION);
    assert(context.messages[6].kind == AEGIS_CONTEXT_EVENT_FILE_READ);
    assert(strcmp(context.messages[6].path, "src/core/context.c") == 0);
    assert(context.messages[7].role == AEGIS_CONTEXT_ROLE_USER);
    assert(strcmp(context.messages[7].content, "implement context") == 0);
    assert(context.total_chars > 0U);
    assert(!context.truncated);
    assert(context.dropped_history_count == 0U);

    assert(context.tool_count ==
        sizeof(expected_tools) / sizeof(expected_tools[0]));
    for (index = 0U; index < context.tool_count; ++index) {
        cJSON *schema;

        assert(strcmp(context.tools[index].name, expected_tools[index]) == 0);
        assert(strcmp(context.tools[index].policy_decision, "deny") != 0);
        schema = cJSON_ParseWithOpts(
            context.tools[index].schema_json,
            NULL,
            1
        );
        assert(schema != NULL);
        cJSON_Delete(schema);
    }

    current_text[0] = 'X';
    workspace[0] = 'X';
    old_user[0] = 'X';
    observation[0] = 'X';
    strcpy(config.policy_decisions[0].decision, "deny");
    assert(strcmp(context.messages[1].content, "workspace: clean") == 0);
    assert(strcmp(context.messages[3].content, "old user") == 0);
    assert(strcmp(context.messages[5].content, "tests passed") == 0);
    assert(strcmp(context.messages[7].content, "implement context") == 0);
    assert(strcmp(context.tools[0].policy_decision, "allow") == 0);

    aegis_context_clear(&context);
    aegis_context_clear(&context);
}

static void test_config_and_profile_filtering(
    const AegisToolRegistry *registry
)
{
    AegisConfig config;
    AegisContext context;
    AegisMessage current = message_with_text("now");
    AegisContextEvent history[] = {
        {
            .role = AEGIS_CONTEXT_ROLE_TOOL,
            .kind = AEGIS_CONTEXT_EVENT_OBSERVATION,
            .name = "tool",
            .content = "observation"
        },
        {
            .role = AEGIS_CONTEXT_ROLE_TOOL,
            .kind = AEGIS_CONTEXT_EVENT_FILE_READ,
            .path = "secret.txt",
            .content = "file"
        }
    };
    AegisContextBuildInput input = {
        .current_message = &current,
        .history = history,
        .history_count = 2U,
        .workspace_summary = "safe workspace"
    };

    assert(aegis_config_load_preset("safe", &config) == AEGIS_OK);
    aegis_context_init(&context);
    assert(aegis_context_build(
        &context,
        &config,
        registry,
        &input
    ) == AEGIS_OK);
    assert(context.tool_count == 2U);
    assert(strcmp(context.tools[0].name, AEGIS_TOOL_LIST_DIR) == 0);
    assert(strcmp(context.tools[1].name, AEGIS_TOOL_READ_FILE) == 0);
    assert(find_message(&context, AEGIS_CONTEXT_EVENT_OBSERVATION) != NULL);
    assert(find_message(&context, AEGIS_CONTEXT_EVENT_FILE_READ) == NULL);
    aegis_context_clear(&context);

    config.include_recent_observations = 0;
    config.include_system_prompt = 0;
    config.include_tool_schemas = 0;
    config.include_workspace_summary = 0;
    assert(aegis_context_build(
        &context,
        &config,
        registry,
        &input
    ) == AEGIS_OK);
    assert(context.message_count == 1U);
    assert(strcmp(context.messages[0].content, "now") == 0);
    aegis_context_clear(&context);

    config.include_recent_observations = 1;
    config.include_system_prompt = 1;
    config.include_tool_schemas = 1;
    config.active_profile.include_system_prompt = 0;
    config.active_profile.include_tool_schemas = 0;
    config.active_profile.include_recent_observations = 0;
    assert(aegis_context_build(
        &context,
        &config,
        registry,
        &input
    ) == AEGIS_OK);
    assert(context.tool_count == 0U);
    assert(find_message(&context, AEGIS_CONTEXT_EVENT_OBSERVATION) == NULL);
    aegis_context_clear(&context);
}

static void test_history_limits(const AegisToolRegistry *registry)
{
    AegisConfig config;
    AegisContext context;
    AegisMessage current = message_with_text("now");
    AegisContextEvent history[] = {
        {
            .role = AEGIS_CONTEXT_ROLE_USER,
            .kind = AEGIS_CONTEXT_EVENT_MESSAGE,
            .content = "aaa"
        },
        {
            .role = AEGIS_CONTEXT_ROLE_ASSISTANT,
            .kind = AEGIS_CONTEXT_EVENT_MESSAGE,
            .content = "bbb"
        },
        {
            .role = AEGIS_CONTEXT_ROLE_USER,
            .kind = AEGIS_CONTEXT_EVENT_MESSAGE,
            .content = "ccc"
        }
    };
    AegisContextBuildInput input = {
        .current_message = &current,
        .history = history,
        .history_count = 3U
    };

    aegis_config_defaults(&config);
    disable_optional_context(&config);
    config.max_history_events = 2;
    config.active_profile.max_context_chars = 100;
    aegis_context_init(&context);
    assert(aegis_context_build(
        &context,
        &config,
        registry,
        &input
    ) == AEGIS_OK);
    assert(context.message_count == 3U);
    assert(strcmp(context.messages[0].content, "bbb") == 0);
    assert(strcmp(context.messages[1].content, "ccc") == 0);
    assert(context.dropped_history_count == 1U);
    assert(context.truncated);
    aegis_context_clear(&context);

    config.max_history_events = 3;
    config.active_profile.max_context_chars = 8;
    assert(aegis_context_build(
        &context,
        &config,
        registry,
        &input
    ) == AEGIS_OK);
    assert(context.message_count == 2U);
    assert(strcmp(context.messages[0].content, "ccc") == 0);
    assert(strcmp(context.messages[1].content, "now") == 0);
    assert(context.dropped_history_count == 2U);
    assert(context.total_chars == 6U);
    aegis_context_clear(&context);
}

static void test_content_caps(const AegisToolRegistry *registry)
{
    static const char utf8_observation[] =
        "ab" "\xF0\x9F\x98\x80" "cd";
    AegisConfig config;
    AegisContext context;
    AegisMessage current = message_with_text("now");
    AegisContextEvent history[] = {
        {
            .role = AEGIS_CONTEXT_ROLE_TOOL,
            .kind = AEGIS_CONTEXT_EVENT_OBSERVATION,
            .name = "tool",
            .content = utf8_observation
        },
        {
            .role = AEGIS_CONTEXT_ROLE_TOOL,
            .kind = AEGIS_CONTEXT_EVENT_FILE_READ,
            .path = "file.txt",
            .content = "abcdef"
        }
    };
    AegisContextBuildInput input = {
        .current_message = &current,
        .history = history,
        .history_count = 2U
    };

    aegis_config_defaults(&config);
    disable_optional_context(&config);
    config.active_profile.max_context_chars = 100;
    config.active_profile.max_observation_chars = 10;
    config.max_tool_output_bytes = 5;
    config.max_file_read_bytes = 3;
    aegis_context_init(&context);
    assert(aegis_context_build(
        &context,
        &config,
        registry,
        &input
    ) == AEGIS_OK);
    assert(context.message_count == 3U);
    assert(strcmp(context.messages[0].content, "ab") == 0);
    assert(strcmp(context.messages[1].content, "abc") == 0);
    assert(context.truncated);
    assert(context.dropped_history_count == 0U);
    aegis_context_clear(&context);
}

static void test_failures(const AegisToolRegistry *registry)
{
    AegisConfig config;
    AegisContext context;
    AegisMessage valid = message_with_text("ok");
    AegisMessage too_large = message_with_text("four");
    AegisMessage invalid = message_with_text("bad");
    AegisContextEvent invalid_history = {
        .role = AEGIS_CONTEXT_ROLE_USER,
        .kind = (AegisContextEventKind)99,
        .content = "bad"
    };
    AegisContextBuildInput input = {
        .current_message = &valid
    };

    aegis_config_defaults(&config);
    disable_optional_context(&config);
    config.active_profile.max_context_chars = 10;
    aegis_context_init(&context);
    assert(aegis_context_build(
        &context,
        &config,
        registry,
        &input
    ) == AEGIS_OK);
    assert(strcmp(context.messages[0].content, "ok") == 0);

    config.active_profile.max_context_chars = 3;
    input.current_message = &too_large;
    assert(aegis_context_build(
        &context,
        &config,
        registry,
        &input
    ) == AEGIS_ERR_RUNTIME);
    assert(context.message_count == 1U);
    assert(strcmp(context.messages[0].content, "ok") == 0);

    invalid.session_id = NULL;
    input.current_message = &invalid;
    assert(aegis_context_build(
        &context,
        &config,
        registry,
        &input
    ) == AEGIS_ERR_INVALID_ARGUMENT);
    assert(aegis_context_build(
        &context,
        &config,
        registry,
        NULL
    ) == AEGIS_ERR_INVALID_ARGUMENT);

    input.current_message = &valid;
    input.history = &invalid_history;
    input.history_count = 1U;
    config.active_profile.max_context_chars = 100;
    assert(aegis_context_build(
        &context,
        &config,
        registry,
        &input
    ) == AEGIS_ERR_INVALID_ARGUMENT);
    aegis_context_clear(&context);
}

static void test_prompt_failures(const AegisToolRegistry *registry)
{
    AegisConfig config;
    AegisContext context;
    AegisMessage current = message_with_text("now");
    AegisContextBuildInput input = {
        .current_message = &current
    };

    assert(aegis_config_load_preset("dev", &config) == AEGIS_OK);
    config.include_tool_schemas = 0;
    config.include_workspace_summary = 0;
    aegis_context_init(&context);

    strcpy(config.active_profile.prompt_path, "../outside.md");
    assert(aegis_context_build(
        &context,
        &config,
        registry,
        &input
    ) == AEGIS_ERR_PATH_ESCAPE);

    strcpy(config.active_profile.prompt_path, "/tmp/outside.md");
    assert(aegis_context_build(
        &context,
        &config,
        registry,
        &input
    ) == AEGIS_ERR_PATH_ESCAPE);

    strcpy(config.active_profile.prompt_path, "prompts/missing.md");
    assert(aegis_context_build(
        &context,
        &config,
        registry,
        &input
    ) == AEGIS_ERR_NOT_FOUND);

    strcpy(
        config.active_profile.prompt_path,
        "prompts/system_coding_agent.md"
    );
    config.active_profile.max_context_chars = 16;
    assert(aegis_context_build(
        &context,
        &config,
        registry,
        &input
    ) == AEGIS_ERR_RUNTIME);
    aegis_context_clear(&context);
}

int main(void)
{
    AegisToolRegistry registry;

    assert(strcmp(
        aegis_context_role_name(AEGIS_CONTEXT_ROLE_ASSISTANT),
        "assistant"
    ) == 0);
    assert(strcmp(
        aegis_context_event_kind_name(AEGIS_CONTEXT_EVENT_FILE_READ),
        "file_read"
    ) == 0);
    assert(aegis_context_role_name((AegisContextRole)99) == NULL);
    assert(aegis_context_event_kind_name(
        (AegisContextEventKind)99
    ) == NULL);

    aegis_tool_registry_init(&registry);
    assert(aegis_tool_registry_register_defaults(&registry) == AEGIS_OK);
    test_full_context(&registry);
    test_config_and_profile_filtering(&registry);
    test_history_limits(&registry);
    test_content_caps(&registry);
    test_failures(&registry);
    test_prompt_failures(&registry);

    puts("aegis context tests: ok");
    return 0;
}

cmake_minimum_required(VERSION 3.20)

project(aegis C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(AEGIS_BUILD_APP "Build the Aegis command-line executable" ON)
option(AEGIS_ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wmissing-prototypes -Wstrict-prototypes)
    if(AEGIS_ENABLE_SANITIZERS)
        add_compile_options(
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        add_link_options(
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
    endif()
endif()

find_package(cJSON REQUIRED)
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)

set(AEGIS_RESOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/aegis-resources")
file(GLOB AEGIS_CONFIG_RESOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/config/*"
)
file(GLOB AEGIS_PROFILE_RESOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/profiles/*"
)
file(GLOB AEGIS_PROMPT_RESOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/prompts/*"
)
add_custom_command(
    OUTPUT "${AEGIS_RESOURCE_DIR}/.stamp"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${AEGIS_RESOURCE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${AEGIS_RESOURCE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/config"
            "${AEGIS_RESOURCE_DIR}/config"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/profiles"
            "${AEGIS_RESOURCE_DIR}/profiles"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/prompts"
            "${AEGIS_RESOURCE_DIR}/prompts"
    COMMAND "${CMAKE_COMMAND}" -E touch "${AEGIS_RESOURCE_DIR}/.stamp"
    DEPENDS
        ${AEGIS_CONFIG_RESOURCES}
        ${AEGIS_PROFILE_RESOURCES}
        ${AEGIS_PROMPT_RESOURCES}
    VERBATIM
)
add_custom_target(aegis_resources ALL
    DEPENDS "${AEGIS_RESOURCE_DIR}/.stamp"
)

add_library(aegis_config STATIC
    src/core/config.c
)
target_include_directories(aegis_config PUBLIC include)
target_link_libraries(aegis_config PUBLIC cjson)

add_library(aegis_tools STATIC
    src/utils/str.c
    src/tools/tool_registry.c
    src/tools/tool_path.c
    src/tools/tool_process.c
    src/tools/file/list_dir.c
    src/tools/file/read_file.c
    src/tools/file/write_file.c
    src/tools/file/append_file.c
    src/tools/file/search_file.c
    src/tools/shell/shell.c
    src/tools/shell/run_tests.c
    src/tools/shell/command_policy.c
    src/tools/git/git_status.c
    src/tools/git/git_common.c
    src/tools/git/git_diff.c
    src/tools/git/git_log.c
    src/tools/git/git_apply_patch.c
    src/tools/http/http_get.c
    src/tools/http/http_common.c
    src/tools/http/http_post.c
    src/tools/assistant/ask_user.c
    src/tools/assistant/send_message.c
    src/tools/assistant/reminder.c
    src/tools/ops/read_log.c
    src/tools/ops/grep_log.c
    src/tools/ops/health_check.c
    src/tools/mcp/mcp_tool.c
    src/tools/mcp/mcp_client.c
)
target_include_directories(aegis_tools PUBLIC include)
target_link_libraries(aegis_tools PUBLIC aegis_config CURL::libcurl)

add_library(aegis_context STATIC
    src/core/context.c
    src/core/message.c
    src/core/prompt.c
    src/core/workspace.c
)
target_include_directories(aegis_context PUBLIC include)
target_link_libraries(aegis_context PUBLIC aegis_tools)

add_library(aegis_runtime STATIC
    src/core/action.c
    src/core/agent.c
    src/core/error.c
    src/core/response.c
    src/core/runtime.c
    src/core/session.c
    src/core/state_sqlite.c
    src/core/trace_jsonl.c
    src/core/trace_reader.c
    src/providers/provider.c
    src/providers/http_json.c
    src/providers/openrouter.c
    src/providers/openai_compat.c
    src/providers/ollama.c
    src/providers/anthropic.c
    src/providers/gemini.c
    src/providers/mock_provider.c
)
target_include_directories(aegis_runtime PUBLIC include)
target_link_libraries(aegis_runtime
    PUBLIC aegis_context CURL::libcurl SQLite::SQLite3 cjson
)

if(AEGIS_BUILD_APP)
    add_executable(aegis
        src/main.c
        src/cli/cli.c
        src/cli/cli_init.c
        src/cli/cli_sessions.c
        src/cli/cli_replay.c
        src/cli/cli_inspect.c
        src/cli/cli_chat.c
        src/cli/cli_resume.c
        src/cli/cli_profiles.c
        src/cli/cli_doctor.c
        src/cli/cli_completion.c
        src/cli/cli_tools.c
        src/cli/cli_config.c
        src/cli/cli_eval.c
        src/cli/cli_mcp.c
        src/cli/cli_run.c
        src/cli/cli_version.c
        src/cli/cli_help.c
        src/core/command.c
    )
    add_dependencies(aegis aegis_resources)
    target_include_directories(aegis PRIVATE include)
    target_compile_definitions(aegis PRIVATE
        AEGIS_DEFAULT_RESOURCE_DIR="${AEGIS_RESOURCE_DIR}"
    )
    target_link_libraries(aegis PRIVATE aegis_runtime)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(aegis_config PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(aegis_tools PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(aegis_context PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(aegis_runtime PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    if(AEGIS_BUILD_APP)
        target_compile_options(aegis PRIVATE
            -Wall -Wextra -Wpedantic -Werror
        )
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    add_executable(aegis_tool_tests tests/test_tools.c)
    target_link_libraries(aegis_tool_tests PRIVATE aegis_tools cjson)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(aegis_tool_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror
        )
    endif()

    add_test(NAME aegis_tool_tests COMMAND aegis_tool_tests)
    set_tests_properties(aegis_tool_tests PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )

    add_executable(aegis_context_tests tests/test_context.c)
    target_link_libraries(aegis_context_tests PRIVATE aegis_context cjson)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(aegis_context_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror
        )
    endif()

    add_test(NAME aegis_context_tests COMMAND aegis_context_tests)
    set_tests_properties(aegis_context_tests PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )

    add_executable(aegis_runtime_tests tests/test_runtime.c)
    target_link_libraries(aegis_runtime_tests PRIVATE aegis_runtime)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(aegis_runtime_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror
        )
    endif()

    add_test(NAME aegis_runtime_tests COMMAND aegis_runtime_tests)
    set_tests_properties(aegis_runtime_tests PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )

    if(AEGIS_BUILD_APP)
        add_test(
            NAME aegis_cli_tests
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tests/test_cli.py"
                    "$<TARGET_FILE:aegis>"
        )
        set_tests_properties(aegis_cli_tests PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        )
    endif()

    add_test(
        NAME aegis_tool_catalog_sync
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/test_tool_catalog.py"
    )
    set_tests_properties(aegis_tool_catalog_sync PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()

cmake_minimum_required(VERSION 3.20)

project(aegis C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(AEGIS_BUILD_APP "Build the incomplete Aegis application executable" OFF)

find_package(cJSON REQUIRED)

add_library(aegis_config STATIC
    src/core/config.c
)
target_include_directories(aegis_config PUBLIC include)
target_link_libraries(aegis_config PUBLIC cjson)

add_library(aegis_tools STATIC
    src/utils/str.c
    src/tools/tool_registry.c
    src/tools/tool_path.c
    src/tools/file/list_dir.c
    src/tools/file/read_file.c
    src/tools/file/write_file.c
    src/tools/file/append_file.c
    src/tools/file/search_file.c
    src/tools/shell/shell.c
    src/tools/shell/run_tests.c
    src/tools/shell/command_policy.c
    src/tools/git/git_status.c
    src/tools/git/git_diff.c
    src/tools/git/git_log.c
    src/tools/git/git_apply_patch.c
    src/tools/http/http_get.c
    src/tools/http/http_post.c
    src/tools/assistant/ask_user.c
    src/tools/assistant/send_message.c
    src/tools/assistant/reminder.c
    src/tools/ops/read_log.c
    src/tools/ops/grep_log.c
    src/tools/ops/health_check.c
    src/tools/mcp/mcp_tool.c
)
target_include_directories(aegis_tools PUBLIC include)
target_link_libraries(aegis_tools PUBLIC aegis_config)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(aegis_config PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(aegis_tools PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
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

    add_test(
        NAME aegis_tool_catalog_sync
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/test_tool_catalog.py"
    )
    set_tests_properties(aegis_tool_catalog_sync PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()

if(AEGIS_BUILD_APP)
    message(FATAL_ERROR
        "AEGIS_BUILD_APP is not available yet; core runtime/CLI is incomplete."
    )
endif()

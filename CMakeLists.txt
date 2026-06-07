cmake_minimum_required(VERSION 3.20)

project(aegis C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(cJSON REQUIRED)
find_package(SQLite3 REQUIRED)

include_directories(include)

add_executable(aegis
    src/main.c
    src/agent.c
    src/config.c
    src/http.c
    src/llm_openrouter.c
    src/log.c
    src/memory.c
    src/tool.c
    src/workspace.c

    src/tools/tool_read_file.c
    src/tools/tool_write_file.c
    src/tools/tool_list_dir.c
    src/tools/tool_run_shell.c
    src/tools/tool_http_request.c
)

target_link_libraries(aegis
    CURL::libcurl
    cjson
    SQLite::SQLite3
)
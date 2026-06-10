# cmake/dependencies.cmake
#
# External dependency discovery and linking for Aegis.
# Keeps find_package and target_link_libraries centralized.

# Find all required and optional dependencies.
# Results are set in parent scope via PARENT_SCOPE.
function(aegis_find_dependencies)
    # Required: cJSON
    find_package(cJSON REQUIRED)
    set(AEGIS_HAVE_CJSON TRUE PARENT_SCOPE)

    # Required: libcurl (used by all HTTP providers and HTTP tools)
    find_package(CURL REQUIRED)
    set(AEGIS_HAVE_CURL TRUE PARENT_SCOPE)

    # Required: SQLite3 (used by state backend)
    find_package(SQLite3 REQUIRED)
    set(AEGIS_HAVE_SQLITE TRUE PARENT_SCOPE)

    # Optional: libseccomp (Linux sandbox hardening)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_library(SECCOMP_LIBRARY seccomp)
        if(SECCOMP_LIBRARY)
            set(AEGIS_HAVE_SECCOMP TRUE PARENT_SCOPE)
            set(SECCOMP_LIBRARY ${SECCOMP_LIBRARY} PARENT_SCOPE)
        else()
            message(STATUS "libseccomp not found; seccomp support disabled")
            set(AEGIS_HAVE_SECCOMP FALSE PARENT_SCOPE)
        endif()
    else()
        set(AEGIS_HAVE_SECCOMP FALSE PARENT_SCOPE)
    endif()
endfunction()

# Link all dependencies to a target.
# Usage: aegis_link_dependencies(<target>)
function(aegis_link_dependencies target)
    target_link_libraries(${target} PRIVATE
        cjson
        CURL::libcurl
        SQLite::SQLite3
    )

    if(AEGIS_HAVE_SECCOMP)
        target_link_libraries(${target} PRIVATE ${SECCOMP_LIBRARY})
        target_compile_definitions(${target} PRIVATE AEGIS_HAVE_SECCOMP=1)
    else()
        target_compile_definitions(${target} PRIVATE AEGIS_HAVE_SECCOMP=0)
    endif()
endfunction()

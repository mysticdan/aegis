# cmake/compiler_flags.cmake
#
# Compiler flag management for Aegis.
# Separates warning, standard, debug, and release configuration
# from the root CMakeLists.txt.

# Apply standard compiler flags to a target.
# Usage: aegis_apply_compiler_flags(<target>)
function(aegis_apply_compiler_flags target)
    target_compile_features(${target} PRIVATE c_std_23)

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
            -Wstrict-prototypes
            -Wmissing-prototypes
            -Werror=implicit-function-declaration
            -Wformat=2
            -Werror=format-security
            -Wundef
            -Wcast-align
            -Wwrite-strings
            -fno-common
            -ffunction-sections
            -fdata-sections
        )

        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            target_compile_options(${target} PRIVATE
                -g3
                -O0
                -fno-omit-frame-pointer
            )
        endif()

        if(CMAKE_BUILD_TYPE STREQUAL "Release")
            target_compile_options(${target} PRIVATE
                -O2
                -DNDEBUG
            )
        endif()
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
        )
    endif()
endfunction()

# Apply sanitizer instrumentation to a target.
# Usage: aegis_apply_sanitizers(<target>)
function(aegis_apply_sanitizers target)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
            -g
        )

        target_link_options(${target} PRIVATE
            -fsanitize=address,undefined
        )
    endif()
endfunction()

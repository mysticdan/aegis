# cmake/hardening.cmake
#
# Binary security hardening flags for Aegis.
# Reduces impact of memory bugs and exploitation attempts.
#
# Applied to the final executable only. Static libraries do not need
# hardening flags because they are linked into the final binary.

# Apply hardening flags to a target.
# Usage: aegis_apply_hardening(<target>)
function(aegis_apply_hardening target)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        # Compile-time hardening
        target_compile_options(${target} PRIVATE
            -D_FORTIFY_SOURCE=3
            -fstack-protector-strong
            -fstack-clash-protection
            -fPIE
            -fno-plt
            -fno-strict-overflow
            -fno-delete-null-pointer-checks
        )

        # -ftrivial-auto-var-init=zero: GCC 12+ / Clang 16+
        # Zero-initializes uninitialized stack variables to prevent
        # use-of-uninitialized bugs.
        if(CMAKE_C_COMPILER_ID MATCHES "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL "12")
            target_compile_options(${target} PRIVATE
                -ftrivial-auto-var-init=zero
            )
        elseif(CMAKE_C_COMPILER_ID MATCHES "Clang" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL "16")
            target_compile_options(${target} PRIVATE
                -ftrivial-auto-var-init=zero
            )
        endif()

        # Link-time hardening
        target_link_options(${target} PRIVATE
            -pie
            -Wl,-z,relro
            -Wl,-z,now
            -Wl,-z,noexecstack
            -Wl,-z,separate-code
            -Wl,-z,text
            -Wl,--gc-sections
            -Wl,--as-needed
        )
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE
            /GS
            /DYNAMICBASE
            /NXCOMPAT
            /guard:cf
        )
        target_link_options(${target} PRIVATE
            /GUARD:CF
        )
    endif()
endfunction()

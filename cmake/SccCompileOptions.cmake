# SccCompileOptions.cmake
#
# Defines two INTERFACE-only targets that carry compile / link options:
#   - scc_warnings   : project warning surface (W4 / -Wall -Wextra -Wpedantic).
#                      Honours SCC_ENABLE_WARNINGS_AS_ERRORS.
#   - scc_sanitizers : ASan + UBSan flags when SCC_ENABLE_SANITIZERS is ON.
#
# These are attached to test and bench targets only — never PUBLIC-linked into
# scc_codec, so consumers don't inherit our warning surface.

if(TARGET scc_warnings)
    return()
endif()

add_library(scc_warnings   INTERFACE)
add_library(scc_sanitizers INTERFACE)

if(MSVC)
    target_compile_options(scc_warnings INTERFACE
        /W4
        /utf-8
        /permissive-
        /Zc:__cplusplus
    )
    if(SCC_ENABLE_WARNINGS_AS_ERRORS)
        target_compile_options(scc_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(scc_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
    )
    if(SCC_ENABLE_WARNINGS_AS_ERRORS)
        target_compile_options(scc_warnings INTERFACE -Werror)
    endif()
endif()

if(SCC_ENABLE_SANITIZERS)
    if(MSVC)
        # MSVC supports ASan only (no UBSan). /RTC1 conflicts with /fsanitize=address,
        # so the canonical config is RelWithDebInfo (see CMakePresets.json: msvc-asan).
        target_compile_options(scc_sanitizers INTERFACE /fsanitize=address)
        target_link_options   (scc_sanitizers INTERFACE /INCREMENTAL:NO)
    else()
        target_compile_options(scc_sanitizers INTERFACE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
            -fno-sanitize-recover=undefined
        )
        target_link_options(scc_sanitizers INTERFACE
            -fsanitize=address,undefined
        )
    endif()
endif()

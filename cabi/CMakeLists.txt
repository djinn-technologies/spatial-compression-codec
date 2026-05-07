# cabi/CMakeLists.txt
#
# Builds libscc -- the SHARED-library C ABI shim. SOVERSION = 1; bumped on
# any breaking change to the symbol set or function semantics. See
# docs/abi.md and ADR-006.

add_library(scc_libscc SHARED
    src/libscc.cpp
)
add_library(scc::libscc ALIAS scc_libscc)

target_include_directories(scc_libscc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(scc_libscc PUBLIC cxx_std_17)

# scc_codec is statically linked into the shared library so libscc.so/dll/dylib
# is a single-file deliverable for downstream language bindings.
target_link_libraries(scc_libscc PRIVATE scc::codec scc_warnings)

# DLL export gating: the .h checks LIBSCC_BUILD to decide between
# dllexport and dllimport. We define it only when *building* the library.
target_compile_definitions(scc_libscc PRIVATE LIBSCC_BUILD=1)

# Hidden visibility on non-Windows platforms; the header re-marks the
# public functions with __attribute__((visibility("default"))). Combined
# with -fvisibility-inlines-hidden on the codec lib, only the 10 public
# functions are exported.
set_target_properties(scc_libscc PROPERTIES
    OUTPUT_NAME scc
    VERSION   1.0.0
    SOVERSION 1
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

if(MSVC)
    # Patent-comment en-dashes; same flag as scc_codec.
    target_compile_options(scc_libscc PRIVATE /utf-8)
    target_compile_definitions(scc_libscc PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ---------------------------------------------------------------------------
# ABI baseline diff -- registered as CTest entry `abi_baseline_diff`.
#
# Dumps the exported symbol table from the built library and diffs against
# cabi/abi-baseline.txt (a committed list of expected exports). CI fails on
# mismatch. [REQ-029] [Ultrathink #1]
# ---------------------------------------------------------------------------

if(SCC_ENABLE_TESTS)
    add_test(NAME abi_baseline_diff
        COMMAND ${CMAKE_COMMAND}
            -DLIBSCC_FILE=$<TARGET_FILE:scc_libscc>
            -DBASELINE=${CMAKE_CURRENT_SOURCE_DIR}/abi-baseline.txt
            -DPLATFORM=${CMAKE_SYSTEM_NAME}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check-abi.cmake
    )
    set_tests_properties(abi_baseline_diff PROPERTIES
        LABELS "abi"
        TIMEOUT 30
    )
endif()

if(SCC_ENABLE_TESTS)
    add_subdirectory(tests)
endif()

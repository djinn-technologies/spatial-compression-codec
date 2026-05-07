add_library(scc_codec STATIC
    src/common/rans.cpp
    src/common/disparity.cpp
    src/common/frequency.cpp
    src/common/quadtree.cpp
    src/common/sei/sei.cpp
)
add_library(scc::codec ALIAS scc_codec)

target_include_directories(scc_codec
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(scc_codec PUBLIC cxx_std_17)
target_link_libraries(scc_codec PRIVATE scc_warnings)

if(MSVC)
    # /utf-8 keeps en-dashes in patent-citation comments from being mis-decoded
    # as CP1252 (which trips Catch2's C4828 in dependent test TUs).
    target_compile_options(scc_codec PRIVATE /utf-8)
    target_compile_definitions(scc_codec PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ---------------------------------------------------------------------------
# SIMD-specific translation units for the disparity transform.
#
# The dispatcher in src/common/disparity.cpp itself is compiled with default
# flags (no AVX2 dependency), so the binary still runs on pre-Haswell CPUs.
# Only the disparity_avx2.cpp TU is compiled with AVX2 enabled, and the
# dispatcher routes calls there only when cpuid reports support.
# ---------------------------------------------------------------------------

set(_scc_arch_x86 OFF)
set(_scc_arch_arm OFF)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|x64)$")
    set(_scc_arch_x86 ON)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
    set(_scc_arch_arm ON)
endif()

if(_scc_arch_x86)
    target_sources(scc_codec PRIVATE src/common/disparity_avx2.cpp)
    target_compile_definitions(scc_codec PRIVATE SCC_HAS_AVX2_BUILD=1)
    if(MSVC)
        set_source_files_properties(
            src/common/disparity_avx2.cpp PROPERTIES
            COMPILE_OPTIONS "/arch:AVX2"
        )
    else()
        set_source_files_properties(
            src/common/disparity_avx2.cpp PROPERTIES
            COMPILE_OPTIONS "-mavx2"
        )
    endif()
elseif(_scc_arch_arm)
    target_sources(scc_codec PRIVATE src/common/disparity_neon.cpp)
    target_compile_definitions(scc_codec PRIVATE SCC_HAS_NEON_BUILD=1)
    # NEON is mandatory on aarch64; no extra flags needed.
endif()

# Propagate the SIMD-build defines to dependents (test/bench TUs that include
# scc/detail/disparity_impls.hpp need to see the same gating).
if(_scc_arch_x86)
    target_compile_definitions(scc_codec INTERFACE SCC_HAS_AVX2_BUILD=1)
elseif(_scc_arch_arm)
    target_compile_definitions(scc_codec INTERFACE SCC_HAS_NEON_BUILD=1)
endif()

set_target_properties(scc_codec PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

if(SCC_ENABLE_TESTS)
    add_subdirectory(tests)
endif()

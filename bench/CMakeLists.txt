function(scc_apply_bench_compile_options tgt)
    if(MSVC)
        target_compile_options(${tgt} PRIVATE /utf-8 /permissive- /Zc:__cplusplus)
        target_compile_definitions(${tgt} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endfunction()

add_executable(bench_rans bench_rans.cpp)
target_link_libraries(bench_rans PRIVATE scc::codec scc_warnings)
scc_apply_bench_compile_options(bench_rans)

add_executable(bench_disparity bench_disparity.cpp)
target_link_libraries(bench_disparity PRIVATE scc::codec scc_warnings)
scc_apply_bench_compile_options(bench_disparity)

add_executable(bench_quadtree bench_quadtree.cpp)
target_link_libraries(bench_quadtree PRIVATE scc::codec scc_warnings)
scc_apply_bench_compile_options(bench_quadtree)

# Wire bench_disparity / bench_quadtree into ctest as perf-regression gates
# (Ultrathink #5 in their respective prompts). Each bench decides at runtime
# whether to enforce its budget: NDEBUG + no-sanitizer builds enforce; Debug
# / ASan / UBSan builds run for correctness only.
if(SCC_ENABLE_TESTS)
    add_test(NAME perf_disparity_1280x720 COMMAND bench_disparity)
    set_tests_properties(perf_disparity_1280x720 PROPERTIES
        LABELS "perf;disparity"
        TIMEOUT 60
    )
    add_test(NAME perf_quadtree_1280x720 COMMAND bench_quadtree)
    set_tests_properties(perf_quadtree_1280x720 PROPERTIES
        LABELS "perf;quadtree"
        TIMEOUT 60
    )
endif()

# ---------------------------------------------------------------------
# scc-bench: comparator harness (AI Build Prompt #14, [REQ-024]).
#
# Builds an executable that benchmarks SCC against jpeg2000 / png / tiff /
# zlib over a deterministic synthetic corpus, with optional download of
# real-world fixtures declared in fixtures/manifest.json.
#
# Per the prompt's "fail loud" requirement, every codec dependency is
# REQUIRED at configure time when SCC_ENABLE_BENCH_HARNESS is ON. There
# is no silent codec-skip path: a missing OpenJPEG fails the configure
# step with a clear message, not a runtime "codec unavailable" warning.
#
# Toggle:   -DSCC_ENABLE_BENCH_HARNESS=ON
# Default:  OFF (so a vanilla `cmake -B build` does not require libpng)
# ---------------------------------------------------------------------
option(SCC_ENABLE_BENCH_HARNESS
    "Build the scc-bench codec comparator (requires OpenJPEG, libpng, libtiff, zlib)"
    OFF)

if(SCC_ENABLE_BENCH_HARNESS)
    find_package(ZLIB     REQUIRED)
    find_package(PNG      REQUIRED)
    find_package(TIFF     REQUIRED)

    # OpenJPEG: prefer pkg-config because Debian/Ubuntu's
    # `libopenjp2-7-dev` ships an `OpenJPEGConfig.cmake` that
    #   (a) reports `version: unknown` (so any version-pinned find
    #       rejects every candidate), and
    #   (b) exports the `openjp2` target without populating
    #       INTERFACE_INCLUDE_DIRECTORIES, so consumers can't find
    #       `<openjpeg.h>` (the headers live at `/usr/include/openjpeg-2.4/`).
    # The matching `libopenjp2.pc` pkg-config file is correct in both
    # respects, so we use it as the source of truth.
    #
    # Fallback: vcpkg / brew ship a complete CMake config; if pkg-config
    # is absent we drop back to find_package(CONFIG) on the assumption
    # that the local install is well-formed.
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(OpenJPEG REQUIRED IMPORTED_TARGET libopenjp2)
        set(SCC_OPENJPEG_TARGET PkgConfig::OpenJPEG)
    else()
        find_package(OpenJPEG REQUIRED CONFIG)
        set(SCC_OPENJPEG_TARGET openjp2)
    endif()

    add_executable(scc_bench
        src/main.cpp
        src/codecs/scc.cpp
        src/codecs/jpeg2000.cpp
        src/codecs/png.cpp
        src/codecs/tiff.cpp
        src/codecs/zlib.cpp
        src/metrics.cpp
        src/report.cpp
        src/corpus.cpp
    )
    set_target_properties(scc_bench PROPERTIES OUTPUT_NAME "scc-bench")
    target_include_directories(scc_bench PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    # Link against scc::libscc (the C ABI shim) rather than the internal
    # scc::codec target. The bench is meant to measure the surface every
    # downstream binding actually uses; including any C-ABI marshalling
    # cost is part of the honest answer to "how fast is SCC in production?".
    target_link_libraries(scc_bench PRIVATE
        scc::libscc
        scc_warnings
        ZLIB::ZLIB
        PNG::PNG
        TIFF::TIFF
        ${SCC_OPENJPEG_TARGET}
    )
    target_compile_features(scc_bench PRIVATE cxx_std_17)
    scc_apply_bench_compile_options(scc_bench)

    # On Linux we use sched_setaffinity for single-thread phase pinning;
    # nothing else is platform-specific.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(scc_bench PRIVATE SCC_BENCH_HAS_AFFINITY=1)
    endif()

    # CI tiny-corpus regression gate (Ultrathink #5). The default fixture
    # is the 320x240 / 30-frame synthetic sequence; the gate compares
    # against the committed bench/reports/baseline.json. Marked as a
    # "perf" labelled test so it can be selected with `ctest -L perf`.
    if(SCC_ENABLE_TESTS)
        add_test(NAME bench_regression_gate
            COMMAND scc_bench
                --corpus ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
                --codecs scc,zlib
                --profiles lossless
                --resolutions 320x240
                --bit-depths 12
                --runs 5
                --seed 0xC0FFEE
                --gate ${CMAKE_CURRENT_SOURCE_DIR}/reports/baseline.json
                --out  ${CMAKE_CURRENT_BINARY_DIR}/bench-out
                --format json
        )
        set_tests_properties(bench_regression_gate PROPERTIES
            LABELS "perf;bench"
            TIMEOUT 300   # 5-minute budget per the run-quick.sh acceptance
        )
    endif()
endif()

# codec/fuzz/CMakeLists.txt
#
# libFuzzer harnesses, gated on SCC_ENABLE_LIBFUZZER. Requires Clang or
# clang-cl with -fsanitize=fuzzer support.
#
# A nightly CI job builds this directory with Clang+ASan+libFuzzer and
# runs each harness for some configured wall-clock budget. Crashes are
# uploaded as test artefacts.

if(NOT (CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
    message(WARNING
        "SCC_ENABLE_LIBFUZZER=ON requires Clang or clang-cl; "
        "current compiler is ${CMAKE_CXX_COMPILER_ID}. Skipping fuzz targets.")
    return()
endif()

# fuzz_sei -- coverage-guided fuzz of scc::sei::demux.
add_executable(fuzz_sei fuzz_sei.cpp)
target_link_libraries(fuzz_sei PRIVATE scc::codec scc_warnings)
target_compile_options(fuzz_sei PRIVATE -fsanitize=fuzzer,address,undefined)
target_link_options   (fuzz_sei PRIVATE -fsanitize=fuzzer,address,undefined)

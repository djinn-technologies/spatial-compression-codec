cmake_minimum_required(VERSION 3.27)
project(scc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

set(SCC_IS_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(SCC_IS_TOP_LEVEL ON)
endif()

option(SCC_ENABLE_TESTS              "Build SCC tests"               ${SCC_IS_TOP_LEVEL})
option(SCC_ENABLE_BENCH              "Build SCC benchmarks"          ${SCC_IS_TOP_LEVEL})
option(SCC_ENABLE_SANITIZERS         "Build with ASan + UBSan"       OFF)
option(SCC_ENABLE_WARNINGS_AS_ERRORS "Treat warnings as errors"      OFF)
option(SCC_ENABLE_LIBFUZZER          "Build libFuzzer harnesses (Clang)" OFF)

include(SccCompileOptions)

if(SCC_ENABLE_TESTS)
    enable_testing()
    include(CTest)
endif()

add_subdirectory(codec)
add_subdirectory(cabi)

if(SCC_ENABLE_BENCH AND SCC_IS_TOP_LEVEL)
    add_subdirectory(bench)
endif()

if(SCC_ENABLE_LIBFUZZER AND SCC_IS_TOP_LEVEL)
    add_subdirectory(codec/fuzz)
endif()

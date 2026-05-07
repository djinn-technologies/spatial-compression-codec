# sdk/python/CMakeLists.txt
#
# pybind11 extension for scc-py.  Builds a self-contained _core.so/.pyd
# that bundles the C++ codec stages and the C ABI shim.  No runtime DLL
# dependencies; the wheel is fully relocatable.

cmake_minimum_required(VERSION 3.27)
project(scc_py LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# scikit-build-core sets PYTHON_EXECUTABLE; pybind11 picks it up via the
# config-file imports below.
find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Codec sources.
#
# The repo root holds `codec/src/common/*.cpp` and `cabi/src/libscc.cpp`.
# We compile them directly into the pybind11 extension so the produced
# .so/.pyd is a single self-contained file.  The same source files are
# built by the parent CMake project for native tests; here we re-build
# them with the Python extension's flags (PIC, hidden visibility).
# ---------------------------------------------------------------------------

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(CODEC_SOURCES
    "${REPO_ROOT}/codec/src/common/rans.cpp"
    "${REPO_ROOT}/codec/src/common/disparity.cpp"
    "${REPO_ROOT}/codec/src/common/frequency.cpp"
    "${REPO_ROOT}/codec/src/common/quadtree.cpp"
    "${REPO_ROOT}/codec/src/common/sei/sei.cpp"
    "${REPO_ROOT}/cabi/src/libscc.cpp"
)
foreach(_src IN LISTS CODEC_SOURCES)
    if(NOT EXISTS "${_src}")
        message(FATAL_ERROR "scc-py: missing codec source: ${_src}")
    endif()
endforeach()

pybind11_add_module(_core MODULE
    src/scc_py/_core.cpp
    ${CODEC_SOURCES}
)

target_include_directories(_core PRIVATE
    "${REPO_ROOT}/codec/include"
    "${REPO_ROOT}/cabi/include"
)

target_compile_features(_core PRIVATE cxx_std_17)

# LIBSCC_BUILD selects the dllexport branch of libscc.h's macro; even
# though we link the C ABI as object code (not a separate DLL), the
# definition makes the function decls extern "C" with the correct
# linkage.  Building as a Python extension on Windows gives _core.pyd
# with no scc-prefixed exports leaking; pybind11's visibility-hidden
# preset hides them.
target_compile_definitions(_core PRIVATE LIBSCC_BUILD=1)

if(MSVC)
    # /utf-8 keeps en-dashes in the codec's patent-citation comments
    # from being mis-decoded as CP1252.
    target_compile_options(_core PRIVATE /utf-8 /permissive-)
    target_compile_definitions(_core PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Hidden visibility on Linux/macOS -- the pybind11 module's PyInit_*
# entry point is the only symbol that needs to escape.  Hiding the rest
# avoids ABI surprises when the loaded extension co-exists with another
# library that also embeds rANS / disparity etc.
set_target_properties(_core PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# scikit-build-core looks at the install rule to decide what goes in
# the wheel.  Drop _core into the scc_py/ package alongside the .py /
# .pyi / py.typed files.
install(TARGETS _core DESTINATION scc_py)

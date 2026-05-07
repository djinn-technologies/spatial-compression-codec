# SccFetchTestDeps.cmake
#
# Pulls Catch2 v3 + rapidcheck via FetchContent (or find_package when a
# system / vcpkg / apt package is available). Idempotent: safe to include
# from multiple subdirectories. Always (re-)augments CMAKE_MODULE_PATH for
# the calling scope so `include(Catch)` works after the first call too.

include(FetchContent)

# ---- Catch2 v3 -----------------------------------------------------------
if(NOT TARGET Catch2::Catch2WithMain)
    find_package(Catch2 3 QUIET)
    if(NOT Catch2_FOUND)
        FetchContent_Declare(Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG        v3.5.4
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(Catch2)
    endif()
endif()

# Locate the Catch.cmake module (the directory holding it) and cache it
# globally. FetchContent_MakeAvailable sets catch2_SOURCE_DIR in the
# CALLING scope only, so the second include from a different subdir
# would lose the variable -- we cache the resolved path so all subsequent
# subdirs can see it.
if(NOT DEFINED SCC_CATCH2_EXTRAS_DIR)
    if(DEFINED catch2_SOURCE_DIR
       AND EXISTS "${catch2_SOURCE_DIR}/extras/Catch.cmake")
        set(SCC_CATCH2_EXTRAS_DIR "${catch2_SOURCE_DIR}/extras"
            CACHE INTERNAL "Path to Catch2 CMake helpers")
    else()
        find_path(_scc_catch2_extras Catch.cmake PATH_SUFFIXES Catch2)
        if(_scc_catch2_extras)
            set(SCC_CATCH2_EXTRAS_DIR "${_scc_catch2_extras}"
                CACHE INTERNAL "Path to Catch2 CMake helpers")
        endif()
    endif()
endif()
if(DEFINED SCC_CATCH2_EXTRAS_DIR)
    list(APPEND CMAKE_MODULE_PATH "${SCC_CATCH2_EXTRAS_DIR}")
endif()

# Re-export the augmented module path to the parent scope so subsequent
# include(Catch) works without callers having to remember this dance.
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" PARENT_SCOPE)

# ---- rapidcheck (+ Catch2 integration) -----------------------------------
#
# DO NOT enable RC_ENABLE_CATCH: that flag wires rapidcheck's *ext/catch*
# directory (a vendored Catch2 v2.4.2) into the build, which collides with
# Catch2 v3's `Catch2` target name (CMake error CMP0002).
#
# The actually-useful piece is rapidcheck/extras/catch/include/rapidcheck/catch.h,
# a thin shim that auto-detects Catch2 v3 (via CATCH_TEST_MACROS_HPP_INCLUDED)
# and falls back to v2 only if v3 isn't present. We expose it manually below.
if(NOT TARGET rapidcheck)
    set(RC_ENABLE_CATCH         OFF CACHE BOOL "" FORCE)
    set(RC_ENABLE_TESTS         OFF CACHE BOOL "" FORCE)
    set(RC_ENABLE_EXAMPLES      OFF CACHE BOOL "" FORCE)
    set(RC_ENABLE_GTEST         OFF CACHE BOOL "" FORCE)
    set(RC_ENABLE_GMOCK         OFF CACHE BOOL "" FORCE)
    set(RC_ENABLE_BOOST         OFF CACHE BOOL "" FORCE)
    set(RC_ENABLE_BOOST_TEST    OFF CACHE BOOL "" FORCE)
    set(RC_ENABLE_DOCTEST       OFF CACHE BOOL "" FORCE)
    set(RC_INSTALL_ALL_EXTRAS   OFF CACHE BOOL "" FORCE)
    set(RC_ENABLE_RTTI          ON  CACHE BOOL "" FORCE)

    FetchContent_Declare(rapidcheck
        GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
        GIT_TAG        ff6af6fc683159deb51c543b065eba14dfcf329b
    )
    FetchContent_MakeAvailable(rapidcheck)
endif()

# Manually expose rapidcheck_catch as an INTERFACE-only target. Tests link
# this in addition to Catch2::Catch2WithMain + rapidcheck.
if(NOT TARGET rapidcheck_catch AND DEFINED rapidcheck_SOURCE_DIR)
    add_library(rapidcheck_catch INTERFACE)
    target_include_directories(rapidcheck_catch INTERFACE
        $<BUILD_INTERFACE:${rapidcheck_SOURCE_DIR}/extras/catch/include>
    )
    target_link_libraries(rapidcheck_catch INTERFACE rapidcheck)
endif()

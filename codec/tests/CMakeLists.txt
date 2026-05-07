include(SccFetchTestDeps)

# Common compile-options helper so the per-test executables stay terse.
function(scc_apply_test_compile_options tgt)
    if(MSVC)
        target_compile_options(${tgt} PRIVATE /utf-8 /permissive- /Zc:__cplusplus)
        # Catch2 / rapidcheck hit narrowing-conversion + shadowing warnings
        # under /W4; suppress locally so SCC_ENABLE_WARNINGS_AS_ERRORS stays
        # on for our own code.
        target_compile_options(${tgt} PRIVATE /wd4244 /wd4267 /wd4456)
    else()
        target_compile_options(${tgt} PRIVATE -Wno-sign-compare)
    endif()
endfunction()

# --- test_rans -------------------------------------------------------------
add_executable(test_rans
    test_rans.cpp
    vectors/ryg_compat.cpp
)
target_include_directories(test_rans PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_rans
    PRIVATE
        scc::codec
        Catch2::Catch2WithMain
        rapidcheck
        rapidcheck_catch
        scc_warnings
        scc_sanitizers
)
scc_apply_test_compile_options(test_rans)

# --- test_disparity --------------------------------------------------------
add_executable(test_disparity
    test_disparity.cpp
)
target_include_directories(test_disparity PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_disparity
    PRIVATE
        scc::codec
        Catch2::Catch2WithMain
        rapidcheck
        rapidcheck_catch
        scc_warnings
        scc_sanitizers
)
scc_apply_test_compile_options(test_disparity)

# --- test_frequency --------------------------------------------------------
add_executable(test_frequency
    test_frequency.cpp
)
target_include_directories(test_frequency PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_frequency
    PRIVATE
        scc::codec
        Catch2::Catch2WithMain
        rapidcheck
        rapidcheck_catch
        scc_warnings
        scc_sanitizers
)
scc_apply_test_compile_options(test_frequency)

# --- test_quadtree ---------------------------------------------------------
add_executable(test_quadtree
    test_quadtree.cpp
)
target_include_directories(test_quadtree PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_quadtree
    PRIVATE
        scc::codec
        Catch2::Catch2WithMain
        rapidcheck
        rapidcheck_catch
        scc_warnings
        scc_sanitizers
)
scc_apply_test_compile_options(test_quadtree)

# --- test_sei --------------------------------------------------------------
add_executable(test_sei
    test_sei.cpp
)
target_include_directories(test_sei PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_sei
    PRIVATE
        scc::codec
        Catch2::Catch2WithMain
        rapidcheck
        rapidcheck_catch
        scc_warnings
        scc_sanitizers
)
scc_apply_test_compile_options(test_sei)

include(Catch)
catch_discover_tests(test_rans
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "unit;rans"
)
catch_discover_tests(test_disparity
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "unit;disparity"
)
catch_discover_tests(test_frequency
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "unit;frequency"
)
catch_discover_tests(test_quadtree
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "unit;quadtree"
)
catch_discover_tests(test_sei
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "unit;sei"
)

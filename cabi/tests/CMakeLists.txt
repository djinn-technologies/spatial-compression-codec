include(SccFetchTestDeps)

add_executable(test_libscc
    test_libscc.cpp
)

target_include_directories(test_libscc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(test_libscc
    PRIVATE
        scc_libscc
        Catch2::Catch2WithMain
        scc_warnings
        scc_sanitizers
)

if(MSVC)
    target_compile_options(test_libscc PRIVATE /utf-8 /permissive- /Zc:__cplusplus)
    target_compile_options(test_libscc PRIVATE /wd4244 /wd4267 /wd4456)
    target_compile_definitions(test_libscc PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(test_libscc PRIVATE -Wno-sign-compare)
endif()

# Windows: scc.dll is built in cabi/ but the test exe lives in cabi/tests/.
# Copy the DLL (and any other transitive runtime DLLs) next to the test
# binary so Windows' loader finds it. CMake 3.21+ exposes
# $<TARGET_RUNTIME_DLLS> which is the canonical mechanism. On non-Windows
# platforms TARGET_RUNTIME_DLLS is empty and the command is a no-op.
add_custom_command(TARGET test_libscc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:test_libscc>
        $<TARGET_FILE_DIR:test_libscc>
    COMMAND_EXPAND_LISTS
)

include(Catch)
catch_discover_tests(test_libscc
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "unit;libscc"
)

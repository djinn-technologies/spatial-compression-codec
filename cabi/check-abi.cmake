# cabi/check-abi.cmake
#
# Runs as a CTest command. Dumps the exported symbol set from the built
# libscc and diffs against cabi/abi-baseline.txt. Fails (non-zero exit) on
# any addition / removal / rename. [REQ-029] [Ultrathink #1]
#
# Inputs (passed via -D):
#   LIBSCC_FILE   path to the built shared library (DLL / SO / dylib)
#   BASELINE      path to abi-baseline.txt (one symbol per line, sorted)
#   PLATFORM      ${CMAKE_SYSTEM_NAME} -- "Windows" / "Linux" / "Darwin"

if(NOT DEFINED LIBSCC_FILE OR NOT DEFINED BASELINE OR NOT DEFINED PLATFORM)
    message(FATAL_ERROR "check-abi.cmake requires -DLIBSCC_FILE, -DBASELINE, -DPLATFORM")
endif()
if(NOT EXISTS "${LIBSCC_FILE}")
    message(FATAL_ERROR "libscc not found at ${LIBSCC_FILE}")
endif()
if(NOT EXISTS "${BASELINE}")
    message(FATAL_ERROR "baseline not found at ${BASELINE}")
endif()

# ---------------------------------------------------------------------------
# Platform-specific symbol-table dumper.
# ---------------------------------------------------------------------------

set(_dump_output "")
if(PLATFORM STREQUAL "Windows")
    # MSVC ships dumpbin.exe alongside cl.exe; either is on PATH inside the
    # VS dev shell. We grep its /EXPORTS output for the symbol column.
    find_program(_dumpbin dumpbin)
    if(NOT _dumpbin)
        message(FATAL_ERROR "dumpbin.exe not found on PATH; run inside the VS Developer shell")
    endif()
    execute_process(
        COMMAND "${_dumpbin}" /EXPORTS "${LIBSCC_FILE}"
        OUTPUT_VARIABLE _dump_output
        ERROR_VARIABLE  _dump_err
        RESULT_VARIABLE _dump_rc
    )
    if(NOT _dump_rc EQUAL 0)
        message(FATAL_ERROR "dumpbin failed (rc=${_dump_rc}): ${_dump_err}")
    endif()
elseif(PLATFORM STREQUAL "Darwin")
    find_program(_nm nm)
    if(NOT _nm)
        message(FATAL_ERROR "nm not found on PATH")
    endif()
    execute_process(
        COMMAND "${_nm}" -gU "${LIBSCC_FILE}"
        OUTPUT_VARIABLE _dump_output
        RESULT_VARIABLE _dump_rc
    )
    if(NOT _dump_rc EQUAL 0)
        message(FATAL_ERROR "nm failed (rc=${_dump_rc})")
    endif()
else()  # Linux / *BSD
    find_program(_nm nm)
    if(NOT _nm)
        message(FATAL_ERROR "nm not found on PATH")
    endif()
    execute_process(
        COMMAND "${_nm}" -D --defined-only "${LIBSCC_FILE}"
        OUTPUT_VARIABLE _dump_output
        RESULT_VARIABLE _dump_rc
    )
    if(NOT _dump_rc EQUAL 0)
        message(FATAL_ERROR "nm failed (rc=${_dump_rc})")
    endif()
endif()

# ---------------------------------------------------------------------------
# Extract `scc_*` symbol names from the dump (ignore everything else).
# ---------------------------------------------------------------------------

set(_actual_symbols "")
string(REPLACE "\n" ";" _dump_lines "${_dump_output}")
foreach(_line IN LISTS _dump_lines)
    # Skip empty / informational headers. Match anything containing "scc_X"
    # where X is an identifier character.
    if(_line MATCHES "(scc_[A-Za-z0-9_]+)")
        list(APPEND _actual_symbols "${CMAKE_MATCH_1}")
    endif()
endforeach()

list(REMOVE_DUPLICATES _actual_symbols)
list(SORT _actual_symbols)

# ---------------------------------------------------------------------------
# Read the committed baseline.
# ---------------------------------------------------------------------------

file(STRINGS "${BASELINE}" _expected_symbols)
list(SORT _expected_symbols)

# ---------------------------------------------------------------------------
# Diff.
# ---------------------------------------------------------------------------

set(_missing "")
set(_extra "")
foreach(_sym IN LISTS _expected_symbols)
    list(FIND _actual_symbols "${_sym}" _idx)
    if(_idx EQUAL -1)
        list(APPEND _missing "${_sym}")
    endif()
endforeach()
foreach(_sym IN LISTS _actual_symbols)
    list(FIND _expected_symbols "${_sym}" _idx)
    if(_idx EQUAL -1)
        list(APPEND _extra "${_sym}")
    endif()
endforeach()

if(_missing OR _extra)
    message("ABI mismatch on ${PLATFORM}:")
    if(_missing)
        message("  MISSING (in baseline, not in library):")
        foreach(_s IN LISTS _missing)
            message("    - ${_s}")
        endforeach()
    endif()
    if(_extra)
        message("  EXTRA (in library, not in baseline):")
        foreach(_s IN LISTS _extra)
            message("    + ${_s}")
        endforeach()
    endif()
    message("Update cabi/abi-baseline.txt deliberately for any intentional change,")
    message("then bump SOVERSION in cabi/CMakeLists.txt if the change is breaking.")
    message(FATAL_ERROR "ABI baseline diff failed.")
endif()

message(STATUS "ABI baseline diff: ${PLATFORM} OK -- ${_actual_symbols}")

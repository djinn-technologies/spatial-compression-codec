// codec/fuzz/fuzz_sei.cpp
//
// libFuzzer harness for scc::sei::demux.
//
// Build with:  -DSCC_ENABLE_LIBFUZZER=ON  (Clang or clang-cl required)
//
// Coverage-guided fuzzing must never crash the demuxer regardless of
// input. The portable rapidcheck companion test in test_sei.cpp asserts
// the same invariant on every CI run; this entry point extends that to
// LLVM's libFuzzer for nightly fuzz jobs.
//
// References:
//   ISO/IEC 14496-10 §D.1.6, §7.4.1.1.
//   [REQ-014]                    -- demux must reject malformed inputs gracefully.

#include "scc/sei.hpp"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // Call through the public API. Cap at 64 MiB so libFuzzer doesn't waste
    // time on huge attempts.
    auto result = scc::sei::demux(scc::sei::byte_span(data, size),
                                  /*max_payload_bytes=*/64ull * 1024 * 1024);
    (void)result;
    return 0;
}

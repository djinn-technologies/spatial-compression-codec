// codec/include/scc/frequency.hpp
//
// Frequency-analyser TOP/R/B decomposition for the Spatial Compression Codec.
//
// Reference: US Patent 10,827,161 B2, col. 7. The patent observes that
// "the top 4 values typically constitute 85-98% of disparity values" in a
// well-behaved depth frame, motivating a small-alphabet TOP stream coded
// separately from a long-tail R stream.
//
// Evidence tags: [REQ-007], [REQ-008], [US10827161B2 col. 7].
//
// Decomposition algorithm
// -----------------------
// Given a flattened S array (int16_t), the analyser:
//   1. Builds a frequency histogram over all 16-bit value bit-patterns.
//   2. Selects the `top_count` most frequent values, breaking ties by
//      ascending signed value (deterministic across runs).
//   3. Walks S in reading order; for each value v:
//        - if v in TOP: emits its index into `top_indices`, B[i] = 0;
//        - else:        emits v into `r_values`,            B[i] = 1.
//
// The TOP stream is small-alphabet (uint8_t indices) and well-suited to
// rANS-8 entropy coding. The R stream covers the full int16 range and
// goes to rANS-16. The B mask is a bit-stream that the codec layer
// either rANS-8-codes directly or muxes via a higher-level run-length form.
//
// Bit packing (Ultrathink #4)
// ---------------------------
// `b_mask` is byte-packed in LSB-first order within each byte:
//
//     bit i (i = element index in S) lives in:
//         byte_index = i / 8
//         bit_index  = i % 8        (LSB first within the byte)
//
// Each byte is a single addressable unit; uint8_t shifts are platform-
// independent. The byte sequence is therefore identical on every supported
// architecture (x86_64, AArch64, big-endian targets) for the same input.
// Trailing bits in the final byte (when `n % 8 != 0`) are guaranteed zero.
//
// Reversibility (Ultrathink #2)
// -----------------------------
// `recompose` is the bit-exact inverse of `decompose` for any well-formed
// `DecomposeResult`. n is inferred as `top_indices.size() + r_values.size()`.
// Empty input produces an empty result; recompose with n == 0 writes
// nothing.
//
// top_count vs unique_values (Ultrathink #3)
// ------------------------------------------
// The input `top_count` parameter must be in [2, 16]. If the input has
// fewer unique values than requested, the *effective* `top_count` in the
// returned struct is clamped to `num_unique_values` and may be < 2 (or 0
// for empty input). The unused slots of `top` stay zero-initialised.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace scc {

// [REQ-007] The patent observes that "the top 4 values typically constitute
// 85-98% of disparity values"; the codec uses 4 by default and exposes the
// knob in [2, 16] for callers with atypical depth distributions.
inline constexpr uint8_t kDefaultTopCount = 4;
inline constexpr uint8_t kMinTopCount     = 2;
inline constexpr uint8_t kMaxTopCount     = 16;

// Output of `decompose`. Each member is documented in terms of the input S.
struct DecomposeResult {
    // Up to 16 selected TOP values, ordered by descending frequency (ties
    // broken by ascending signed value). Slots [top_count .. 16) are 0.
    std::array<int16_t, 16> top{};

    // Effective TOP count, in [0, 16]. 0 only for empty input. May be
    // smaller than the requested top_count if the input has fewer unique
    // values; see "top_count vs unique_values" above.
    uint8_t top_count = 0;

    // For every S element with v in TOP, its index in `top` (in [0, top_count)).
    // Size == count of S elements that landed in TOP.
    std::vector<uint8_t> top_indices;

    // Residual values for S elements not in TOP, in reading order.
    // Size == n - top_indices.size().
    std::vector<int16_t> r_values;

    // 1-bit-per-S-element mask, LSB-first within each byte.
    //     b_mask[i / 8] bit (i % 8) == 0  -> S[i] is in TOP
    //     b_mask[i / 8] bit (i % 8) == 1  -> S[i] is in R
    // Size == ceil(n / 8). [Ultrathink #4]
    std::vector<uint8_t> b_mask;
};

// [REQ-007] Forward decomposition.
// Throws std::invalid_argument if top_count not in [kMinTopCount, kMaxTopCount].
// `s` may be nullptr only when n == 0.
DecomposeResult decompose(const int16_t* s, std::size_t n, uint8_t top_count);

// [REQ-008] Inverse: reconstruct s_out (n elements) from a DecomposeResult.
// n is inferred as top_indices.size() + r_values.size(); s_out must point
// to writable storage of at least n elements. recompose is bit-exact for
// any DecomposeResult produced by decompose.
void recompose(const DecomposeResult& dr, int16_t* s_out) noexcept;

// Helper: number of S elements implied by `dr` (== top_indices.size() +
// r_values.size()). Useful when the caller serialises `dr` and needs to
// record n separately.
inline std::size_t decomposed_element_count(const DecomposeResult& dr) noexcept {
    return dr.top_indices.size() + dr.r_values.size();
}

} // namespace scc

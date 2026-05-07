// codec/include/scc/disparity.hpp
//
// Disparity matrix transform for the Spatial Compression Codec.
//
// Reference: US Patent 10,827,161 B2, columns 4-5 (verbatim formula block):
//
//     S[1][1]  = d[1][1]                       // anchor
//     S[1][j]  = d[1][j]   - d[1][j-1]          // top row, horizontal
//     S[i][j]  = d[i][j]   - d[i-1][j]          // i >= 2, column-wise vertical
//
// Translated to 0-indexed C++:
//
//     S[0][0] = d[0][0]                        // anchor
//     S[0][j] = d[0][j] - d[0][j-1]            for j in [1, W-1]   (top row)
//     S[i][j] = d[i][j] - d[i-1][j]            for i in [1, H-1]   (vertical)
//
// The inverse is the prefix-sum of the same shape:
//
//     d[0][0] = S[0][0]
//     d[0][j] = d[0][j-1] + S[0][j]            (horizontal prefix-sum, row 0)
//     d[i][j] = d[i-1][j] + S[i][j]            (vertical prefix-sum, i >= 1)
//
// Evidence tags: [REQ-001], [REQ-002], [REQ-006], [US10827161B2 col. 4-5].
//
// Overflow semantics
// ------------------
// d is uint16_t (range [0, 65535]); S is int16_t. The pairwise difference can
// exceed int16_t range. The patent's reversibility requirement is satisfied
// by computing the difference in unsigned 16-bit arithmetic (which is modular
// by definition in C++17, well-defined wrap-around) and storing the bit
// pattern verbatim into the int16_t buffer. Inverse adds the same bit pattern
// back via unsigned addition, producing the original d exactly. No saturation,
// no clamping. See ADR-002. [Ultrathink #1]
//
// Aliasing of int16_t and uint16_t through the same storage is explicitly
// permitted by [basic.lval]/11 (signed/unsigned counterparts).
//
// SIMD dispatch
// -------------
// The dispatcher selects an implementation at first call:
//   - x86_64: AVX2 if cpuid reports it; otherwise scalar.
//   - aarch64: NEON (mandatory in the AArch64 spec).
//   - everything else: scalar.
// Decision is cached in a function-local static. See codec/src/common/disparity.cpp.

#pragma once

#include <cstddef>
#include <cstdint>

namespace scc {

// REQ-002: forward transform.
// Preconditions: W >= 0, H >= 0; d and s point to W*H elements (or are
// unread/unwritten when W*H == 0). d and s must not alias.
void disparity_forward(const uint16_t* d, int W, int H, int16_t* s) noexcept;

// REQ-006: inverse (prefix-sum) transform. Same preconditions.
void disparity_inverse(const int16_t* s, int W, int H, uint16_t* d) noexcept;

} // namespace scc

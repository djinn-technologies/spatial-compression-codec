// codec/src/common/disparity_neon.cpp
//
// NEON disparity transform. NEON is mandatory in the AArch64 spec, so this
// TU compiles with no extra flags on aarch64.
//
// Reference: US Patent 10,827,161 B2, cols. 4-5.
// [REQ-002], [REQ-006].

#if defined(SCC_HAS_NEON_BUILD)

#include "scc/detail/disparity_impls.hpp"

#include <arm_neon.h>
#include <cstddef>
#include <cstdint>

namespace scc::detail {

namespace {
constexpr int kVec = 8;   // 8 lanes of uint16_t per uint16x8_t.
} // namespace

// REQ-002: forward, NEON.
void disparity_forward_neon(const uint16_t* d, int W, int H, int16_t* s) noexcept {
    if (W <= 0 || H <= 0) return;
    uint16_t* s_u = reinterpret_cast<uint16_t*>(s);

    // [REQ-002] Top row: anchor + horizontal differences.
    s_u[0] = d[0];
    int j = 1;
    for (; j + kVec <= W; j += kVec) {
        const uint16x8_t curr = vld1q_u16(d + j);
        const uint16x8_t prev = vld1q_u16(d + j - 1);
        vst1q_u16(s_u + j, vsubq_u16(curr, prev));
    }
    // Tail (Ultrathink #2 / #3).
    for (; j < W; ++j) {
        s_u[j] = static_cast<uint16_t>(d[j] - d[j - 1]);
    }

    // [REQ-002] Rows i >= 1: vertical differences.
    for (int i = 1; i < H; ++i) {
        const uint16_t* d_curr = d + static_cast<std::ptrdiff_t>(i) * W;
        const uint16_t* d_prev = d_curr - W;
        uint16_t*       s_row  = s_u   + static_cast<std::ptrdiff_t>(i) * W;
        int jj = 0;
        for (; jj + kVec <= W; jj += kVec) {
            const uint16x8_t a = vld1q_u16(d_curr + jj);
            const uint16x8_t b = vld1q_u16(d_prev + jj);
            vst1q_u16(s_row + jj, vsubq_u16(a, b));
        }
        for (; jj < W; ++jj) {
            s_row[jj] = static_cast<uint16_t>(d_curr[jj] - d_prev[jj]);
        }
    }
}

// REQ-006: inverse, NEON.
void disparity_inverse_neon(const int16_t* s, int W, int H, uint16_t* d) noexcept {
    if (W <= 0 || H <= 0) return;
    const uint16_t* s_u = reinterpret_cast<const uint16_t*>(s);

    // [REQ-006] Top row: scalar horizontal prefix-sum.
    d[0] = s_u[0];
    for (int j = 1; j < W; ++j) {
        d[j] = static_cast<uint16_t>(d[j - 1] + s_u[j]);
    }

    // [REQ-006] Rows i >= 1: vertical prefix-sum -- SIMD.
    for (int i = 1; i < H; ++i) {
        const uint16_t* s_row  = s_u + static_cast<std::ptrdiff_t>(i) * W;
        const uint16_t* d_prev = d   + static_cast<std::ptrdiff_t>(i - 1) * W;
        uint16_t*       d_curr = d   + static_cast<std::ptrdiff_t>(i) * W;
        int j = 0;
        for (; j + kVec <= W; j += kVec) {
            const uint16x8_t a = vld1q_u16(d_prev + j);
            const uint16x8_t b = vld1q_u16(s_row  + j);
            vst1q_u16(d_curr + j, vaddq_u16(a, b));
        }
        for (; j < W; ++j) {
            d_curr[j] = static_cast<uint16_t>(d_prev[j] + s_row[j]);
        }
    }
}

} // namespace scc::detail

#endif // SCC_HAS_NEON_BUILD

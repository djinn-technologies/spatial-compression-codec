// codec/src/common/disparity_avx2.cpp
//
// AVX2 disparity transform. This TU is compiled with /arch:AVX2 (MSVC) or
// -mavx2 (gcc/clang) so the AVX2 intrinsics are legal here. The dispatcher
// (disparity.cpp) is compiled without AVX2 and routes calls only when
// `cpuid` reports support, so the rest of the binary remains AVX2-free.
//
// Reference: US Patent 10,827,161 B2, cols. 4-5.
// [REQ-002], [REQ-006].

#if defined(SCC_HAS_AVX2_BUILD)

#include "scc/detail/disparity_impls.hpp"

#include <cstddef>
#include <cstdint>
#include <immintrin.h>

namespace scc::detail {

namespace {

// 16 lanes of int16/uint16 = 256 bits = one __m256i.
constexpr int kVec = 16;

inline __m256i load(const uint16_t* p) noexcept {
    return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
}
inline void store(uint16_t* p, __m256i v) noexcept {
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(p), v);
}

} // namespace

// REQ-002: forward, AVX2.
void disparity_forward_avx2(const uint16_t* d, int W, int H, int16_t* s) noexcept {
    if (W <= 0 || H <= 0) return;
    uint16_t* s_u = reinterpret_cast<uint16_t*>(s);

    // [REQ-002] Top row: anchor + horizontal differences.
    s_u[0] = d[0];
    int j = 1;
    // SIMD body: load curr at [j..j+15], prev at [j-1..j+14] (unaligned), subtract.
    for (; j + kVec <= W; j += kVec) {
        const __m256i curr = load(d + j);
        const __m256i prev = load(d + j - 1);
        store(s_u + j, _mm256_sub_epi16(curr, prev));
    }
    // Tail handles W - 1 not being a multiple of 16 (Ultrathink #2 / #3).
    for (; j < W; ++j) {
        s_u[j] = static_cast<uint16_t>(d[j] - d[j - 1]);
    }

    // [REQ-002] Rows i >= 1: vertical column-wise differences.
    for (int i = 1; i < H; ++i) {
        const uint16_t* d_curr = d + static_cast<std::ptrdiff_t>(i) * W;
        const uint16_t* d_prev = d_curr - W;
        uint16_t*       s_row  = s_u   + static_cast<std::ptrdiff_t>(i) * W;
        int jj = 0;
        for (; jj + kVec <= W; jj += kVec) {
            const __m256i a = load(d_curr + jj);
            const __m256i b = load(d_prev + jj);
            store(s_row + jj, _mm256_sub_epi16(a, b));
        }
        for (; jj < W; ++jj) {
            s_row[jj] = static_cast<uint16_t>(d_curr[jj] - d_prev[jj]);
        }
    }
}

// REQ-006: inverse, AVX2.
void disparity_inverse_avx2(const int16_t* s, int W, int H, uint16_t* d) noexcept {
    if (W <= 0 || H <= 0) return;
    const uint16_t* s_u = reinterpret_cast<const uint16_t*>(s);

    // [REQ-006] Top row: horizontal prefix-sum is sequential -- scalar.
    d[0] = s_u[0];
    for (int j = 1; j < W; ++j) {
        d[j] = static_cast<uint16_t>(d[j - 1] + s_u[j]);
    }

    // [REQ-006] Rows i >= 1: vertical prefix-sum is column-parallel -- SIMD.
    for (int i = 1; i < H; ++i) {
        const uint16_t* s_row  = s_u + static_cast<std::ptrdiff_t>(i) * W;
        const uint16_t* d_prev = d   + static_cast<std::ptrdiff_t>(i - 1) * W;
        uint16_t*       d_curr = d   + static_cast<std::ptrdiff_t>(i) * W;
        int j = 0;
        for (; j + kVec <= W; j += kVec) {
            const __m256i a = load(d_prev + j);
            const __m256i b = load(s_row  + j);
            store(d_curr + j, _mm256_add_epi16(a, b));
        }
        for (; j < W; ++j) {
            d_curr[j] = static_cast<uint16_t>(d_prev[j] + s_row[j]);
        }
    }
}

} // namespace scc::detail

#endif // SCC_HAS_AVX2_BUILD

// codec/src/common/disparity.cpp
//
// Disparity matrix transform: scalar implementation + runtime dispatcher.
//
// Reference: US Patent 10,827,161 B2, cols. 4-5.
// Evidence tags: [REQ-001], [REQ-002], [REQ-006].

#include "scc/disparity.hpp"
#include "scc/detail/disparity_impls.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
  #include <intrin.h>
#endif

namespace scc::detail {

// ---------------------------------------------------------------------------
// Scalar implementations
// ---------------------------------------------------------------------------
//
// uint16_t arithmetic is modular by [conv.integral]/2 in C++17, so the
// difference / sum is well-defined and reversible. The signed/unsigned 16-bit
// types alias each other under [basic.lval]/11.

// REQ-002: forward — anchor + horizontal diffs on row 0; vertical diffs below.
void disparity_forward_scalar(const uint16_t* d, int W, int H, int16_t* s) noexcept {
    if (W <= 0 || H <= 0) return;
    uint16_t* s_u = reinterpret_cast<uint16_t*>(s);

    // [REQ-002] Top row: anchor at (0,0), horizontal differences elsewhere.
    s_u[0] = d[0];
    for (int j = 1; j < W; ++j) {
        s_u[j] = static_cast<uint16_t>(d[j] - d[j - 1]);
    }

    // [REQ-002] Rows i >= 1: vertical column-wise differences.
    for (int i = 1; i < H; ++i) {
        const uint16_t* d_curr = d + static_cast<std::ptrdiff_t>(i) * W;
        const uint16_t* d_prev = d_curr - W;
        uint16_t*       s_row  = s_u   + static_cast<std::ptrdiff_t>(i) * W;
        for (int j = 0; j < W; ++j) {
            s_row[j] = static_cast<uint16_t>(d_curr[j] - d_prev[j]);
        }
    }
}

// REQ-006: inverse — prefix-sum over the same row/column structure.
void disparity_inverse_scalar(const int16_t* s, int W, int H, uint16_t* d) noexcept {
    if (W <= 0 || H <= 0) return;
    const uint16_t* s_u = reinterpret_cast<const uint16_t*>(s);

    // [REQ-006] Top row: horizontal prefix-sum (sequential).
    d[0] = s_u[0];
    for (int j = 1; j < W; ++j) {
        d[j] = static_cast<uint16_t>(d[j - 1] + s_u[j]);
    }

    // [REQ-006] Rows i >= 1: vertical prefix-sum (column-parallel).
    for (int i = 1; i < H; ++i) {
        const uint16_t* s_row  = s_u + static_cast<std::ptrdiff_t>(i) * W;
        const uint16_t* d_prev = d   + static_cast<std::ptrdiff_t>(i - 1) * W;
        uint16_t*       d_curr = d   + static_cast<std::ptrdiff_t>(i) * W;
        for (int j = 0; j < W; ++j) {
            d_curr[j] = static_cast<uint16_t>(d_prev[j] + s_row[j]);
        }
    }
}

// ---------------------------------------------------------------------------
// CPU feature detection
// ---------------------------------------------------------------------------

bool cpu_supports_avx2() noexcept {
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
  #if defined(_MSC_VER) && !defined(__clang__)
    int info[4] = {0, 0, 0, 0};
    __cpuid(info, 0);
    if (info[0] < 7) return false;
    __cpuidex(info, 7, 0);
    // EBX bit 5 = AVX2.
    return (info[1] & (1 << 5)) != 0;
  #elif defined(__GNUC__) || defined(__clang__)
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") != 0;
  #else
    return false;
  #endif
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Dispatcher
// ---------------------------------------------------------------------------

namespace {

using ForwardFn = void(*)(const uint16_t*, int, int, int16_t*);
using InverseFn = void(*)(const int16_t*, int, int, uint16_t*);

DispatchKind pick_kind() noexcept {
#if defined(SCC_HAS_NEON_BUILD)
    return DispatchKind::Neon;
#elif defined(SCC_HAS_AVX2_BUILD)
    return cpu_supports_avx2() ? DispatchKind::Avx2 : DispatchKind::Scalar;
#else
    return DispatchKind::Scalar;
#endif
}

ForwardFn pick_forward() noexcept {
    switch (pick_kind()) {
#if defined(SCC_HAS_NEON_BUILD)
    case DispatchKind::Neon: return &disparity_forward_neon;
#endif
#if defined(SCC_HAS_AVX2_BUILD)
    case DispatchKind::Avx2: return &disparity_forward_avx2;
#endif
    default: return &disparity_forward_scalar;
    }
}

InverseFn pick_inverse() noexcept {
    switch (pick_kind()) {
#if defined(SCC_HAS_NEON_BUILD)
    case DispatchKind::Neon: return &disparity_inverse_neon;
#endif
#if defined(SCC_HAS_AVX2_BUILD)
    case DispatchKind::Avx2: return &disparity_inverse_avx2;
#endif
    default: return &disparity_inverse_scalar;
    }
}

ForwardFn forward_dispatcher() noexcept {
    static ForwardFn fn = pick_forward();
    return fn;
}
InverseFn inverse_dispatcher() noexcept {
    static InverseFn fn = pick_inverse();
    return fn;
}

} // namespace

DispatchKind active_dispatch_kind() noexcept {
    static DispatchKind k = pick_kind();
    return k;
}

} // namespace scc::detail

namespace scc {

void disparity_forward(const uint16_t* d, int W, int H, int16_t* s) noexcept {
    scc::detail::forward_dispatcher()(d, W, H, s);
}

void disparity_inverse(const int16_t* s, int W, int H, uint16_t* d) noexcept {
    scc::detail::inverse_dispatcher()(s, W, H, d);
}

} // namespace scc

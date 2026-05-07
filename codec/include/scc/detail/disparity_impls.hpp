// codec/include/scc/detail/disparity_impls.hpp
//
// Internal — declares the concrete implementation entry points so tests can
// exercise each path (scalar, AVX2, NEON) directly without going through the
// runtime dispatcher. NOT a stable public API. Not installed.

#pragma once

#include <cstdint>

namespace scc::detail {

// Always available.
void disparity_forward_scalar(const uint16_t* d, int W, int H, int16_t* s) noexcept;
void disparity_inverse_scalar(const int16_t* s, int W, int H, uint16_t* d) noexcept;

#if defined(SCC_HAS_AVX2_BUILD)
void disparity_forward_avx2(const uint16_t* d, int W, int H, int16_t* s) noexcept;
void disparity_inverse_avx2(const int16_t* s, int W, int H, uint16_t* d) noexcept;
#endif

#if defined(SCC_HAS_NEON_BUILD)
void disparity_forward_neon(const uint16_t* d, int W, int H, int16_t* s) noexcept;
void disparity_inverse_neon(const int16_t* s, int W, int H, uint16_t* d) noexcept;
#endif

// Exposed for diagnostic / test use; tells which dispatch path the public
// API will take on this CPU.
enum class DispatchKind : uint8_t { Scalar, Avx2, Neon };
DispatchKind active_dispatch_kind() noexcept;

bool cpu_supports_avx2() noexcept;

} // namespace scc::detail

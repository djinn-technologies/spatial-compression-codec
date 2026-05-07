// bench/src/metrics.hpp
//
// Quality metrics for comparator runs.
//
//   - PSNR (peak signal-to-noise ratio) on the raster.
//   - SSIM (structural similarity) using an 11x11 Gaussian window with
//     sigma=1.5, K1=0.01, K2=0.03 -- the canonical Wang et al. 2004
//     parameters. Computed only on overlapping windows (no edge padding
//     bias) and averaged.
//   - Point-cloud diff: pointwise distance between reprojected ground
//     truth and reprojected decoded depth. Returns the fraction of
//     pixels where the difference exceeds `tolerance_m` metres.
//     This is deterministic (no sampling, no nearest-neighbour) and
//     therefore stable across runs (Ultrathink #3).

#pragma once

#include <cstddef>
#include <cstdint>

#include "frame.hpp"

namespace scc::bench::metrics {

// PSNR in dB. Returns +infinity if the frames are bit-equal; returns
// 0.0 if the frames have mismatched dimensions (signalling failure).
double psnr(const Frame& gt, const Frame& dec) noexcept;

// SSIM averaged over all valid 11x11 windows. Range [-1, 1]; 1 means
// identical. Returns -1 on dimension mismatch.
double ssim(const Frame& gt, const Frame& dec) noexcept;

// Fraction in [0, 1] of pixels whose reprojected 3D distance exceeds
// tolerance_m. Both frames must share dimensions and intrinsics.
// Pixels where either depth is 0 (sensor "no measurement") are
// excluded from both numerator and denominator.
double point_cloud_diff(const Frame& gt,
                        const Frame& dec,
                        const Intrinsics& intr,
                        double tolerance_m) noexcept;

} // namespace scc::bench::metrics

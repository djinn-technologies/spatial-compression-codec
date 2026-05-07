// bench/src/metrics.cpp

#include "metrics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace scc::bench::metrics {

namespace {

// 11x11 Gaussian kernel with sigma = 1.5, normalised to sum 1.
// Pre-computed so it's exactly reproducible across runs / platforms.
constexpr int    SSIM_W   = 11;
constexpr int    SSIM_R   = SSIM_W / 2;        // 5
constexpr double SSIM_SIG = 1.5;

std::array<double, SSIM_W * SSIM_W> make_gauss_kernel() {
    std::array<double, SSIM_W * SSIM_W> k{};
    double sum = 0.0;
    for (int j = -SSIM_R; j <= SSIM_R; ++j) {
        for (int i = -SSIM_R; i <= SSIM_R; ++i) {
            const double v = std::exp(-(i*i + j*j) / (2.0 * SSIM_SIG * SSIM_SIG));
            k[(j + SSIM_R) * SSIM_W + (i + SSIM_R)] = v;
            sum += v;
        }
    }
    for (auto& v : k) v /= sum;
    return k;
}

const std::array<double, SSIM_W * SSIM_W>& gauss_kernel() {
    static const auto K = make_gauss_kernel();
    return K;
}

} // namespace

double psnr(const Frame& gt, const Frame& dec) noexcept {
    if (gt.width != dec.width || gt.height != dec.height
        || gt.bit_depth != dec.bit_depth || gt.data.size() != dec.data.size()) {
        return 0.0;
    }
    if (gt.data.empty()) return std::numeric_limits<double>::infinity();
    long double sse = 0.0L;
    for (std::size_t i = 0; i < gt.data.size(); ++i) {
        const long double d = static_cast<long double>(gt.data[i])
                            - static_cast<long double>(dec.data[i]);
        sse += d * d;
    }
    if (sse == 0.0L) return std::numeric_limits<double>::infinity();
    const double mse = static_cast<double>(sse / static_cast<long double>(gt.data.size()));
    const double max = static_cast<double>(gt.max_value());
    return 10.0 * std::log10((max * max) / mse);
}

double ssim(const Frame& gt, const Frame& dec) noexcept {
    if (gt.width != dec.width || gt.height != dec.height
        || gt.bit_depth != dec.bit_depth || gt.data.size() != dec.data.size()) {
        return -1.0;
    }
    const int W = static_cast<int>(gt.width);
    const int H = static_cast<int>(gt.height);
    if (W < SSIM_W || H < SSIM_W) {
        // Not enough room for one full window; treat as identical iff
        // the data is bit-equal, otherwise a degenerate 0.
        bool eq = true;
        for (std::size_t i = 0; i < gt.data.size() && eq; ++i) eq = (gt.data[i] == dec.data[i]);
        return eq ? 1.0 : 0.0;
    }
    const double L  = static_cast<double>(gt.max_value());
    const double K1 = 0.01, K2 = 0.03;
    const double C1 = (K1 * L) * (K1 * L);
    const double C2 = (K2 * L) * (K2 * L);

    const auto& K = gauss_kernel();

    long double acc = 0.0L;
    std::size_t n = 0;

    for (int y = SSIM_R; y < H - SSIM_R; ++y) {
        for (int x = SSIM_R; x < W - SSIM_R; ++x) {
            // Compute weighted means, variances, and covariance over the
            // 11x11 window centred at (x, y).
            double mx = 0, my = 0;
            for (int j = -SSIM_R; j <= SSIM_R; ++j) {
                for (int i = -SSIM_R; i <= SSIM_R; ++i) {
                    const double w = K[(j + SSIM_R) * SSIM_W + (i + SSIM_R)];
                    const double a = static_cast<double>(gt.data [(y + j) * W + (x + i)]);
                    const double b = static_cast<double>(dec.data[(y + j) * W + (x + i)]);
                    mx += w * a;
                    my += w * b;
                }
            }
            double sx2 = 0, sy2 = 0, sxy = 0;
            for (int j = -SSIM_R; j <= SSIM_R; ++j) {
                for (int i = -SSIM_R; i <= SSIM_R; ++i) {
                    const double w = K[(j + SSIM_R) * SSIM_W + (i + SSIM_R)];
                    const double a = static_cast<double>(gt.data [(y + j) * W + (x + i)]) - mx;
                    const double b = static_cast<double>(dec.data[(y + j) * W + (x + i)]) - my;
                    sx2 += w * a * a;
                    sy2 += w * b * b;
                    sxy += w * a * b;
                }
            }
            const double num = (2.0 * mx * my + C1) * (2.0 * sxy + C2);
            const double den = (mx*mx + my*my + C1) * (sx2 + sy2 + C2);
            acc += (den != 0.0) ? (num / den) : 1.0;
            ++n;
        }
    }
    return n ? static_cast<double>(acc / static_cast<long double>(n)) : -1.0;
}

double point_cloud_diff(const Frame& gt,
                        const Frame& dec,
                        const Intrinsics& intr,
                        double tolerance_m) noexcept {
    if (gt.width != dec.width || gt.height != dec.height) return 1.0;
    const int W = static_cast<int>(gt.width);
    const int H = static_cast<int>(gt.height);
    const double s   = static_cast<double>(intr.depth_scale_m_per_unit);
    const double tol2 = tolerance_m * tolerance_m;

    std::size_t total = 0, moved = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const std::uint16_t za = gt.data [y * W + x];
            const std::uint16_t zb = dec.data[y * W + x];
            if (za == 0 || zb == 0) continue;     // sensor "no measurement"
            ++total;
            const double zad = za * s, zbd = zb * s;
            // Pinhole back-projection difference. Since both rasters
            // share intrinsics and (x, y), the X and Y components only
            // differ by a factor of (z_a - z_b), not by independent
            // values; the squared distance simplifies to
            //     d^2 = (1 + (x-cx)^2/fx^2 + (y-cy)^2/fy^2) * (za - zb)^2
            // which is the closed form we use below.
            const double dz   = zad - zbd;
            const double ux   = (static_cast<double>(x) - intr.cx) / intr.fx;
            const double uy   = (static_cast<double>(y) - intr.cy) / intr.fy;
            const double d2   = (1.0 + ux*ux + uy*uy) * dz * dz;
            if (d2 > tol2) ++moved;
        }
    }
    if (total == 0) return 0.0;
    return static_cast<double>(moved) / static_cast<double>(total);
}

} // namespace scc::bench::metrics

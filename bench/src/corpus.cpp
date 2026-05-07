// bench/src/corpus.cpp

#include "corpus.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>

namespace scc::bench {

namespace {

// Seeded LCG -- exactly reproducible across platforms. We don't use
// std::mt19937 because its sequence depends on libstdc++/libc++ details
// that have varied historically.
struct Lcg {
    std::uint64_t s;
    explicit Lcg(std::uint64_t seed) : s(seed ? seed : 0xC0FFEEull) {}
    std::uint32_t next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::uint32_t>(s >> 33);
    }
    double next_unit() {
        return static_cast<double>(next() & 0xFFFFFFu) / static_cast<double>(0x1000000u);
    }
};

void apply_mask(std::vector<std::uint16_t>& d, std::uint16_t mask) {
    for (auto& v : d) v = static_cast<std::uint16_t>(v & mask);
}

// Sequence 1: moving plane. A tilted plane translates across the field
// of view; depth at every pixel changes by a constant offset per frame.
// Tests temporal predictability.
Sequence make_moving_plane(const CorpusSpec& spec, Intrinsics intr) {
    Sequence seq;
    seq.name = "moving_plane";
    seq.frames.reserve(spec.frames);
    const std::uint16_t mask = static_cast<std::uint16_t>((1u << spec.bit_depth) - 1u);
    const double base_z = 1500.0;        // mm
    const double tilt   = 0.15;          // dz / dx normalised
    for (std::uint32_t f = 0; f < spec.frames; ++f) {
        Frame fr;
        fr.width = spec.width; fr.height = spec.height; fr.bit_depth = spec.bit_depth;
        fr.data.resize(static_cast<std::size_t>(spec.width) * spec.height);
        const double offset = static_cast<double>(f) * 8.0;     // mm/frame
        for (std::uint32_t y = 0; y < spec.height; ++y) {
            for (std::uint32_t x = 0; x < spec.width; ++x) {
                const double nx = (x - intr.cx) / intr.fx;
                const double ny = (y - intr.cy) / intr.fy;
                const double z  = base_z + offset + tilt * 1000.0 * nx + 0.05 * 1000.0 * ny;
                int v = static_cast<int>(std::round(z));
                if (v < 0) v = 0;
                if (v > mask) v = mask;
                fr.data[y * spec.width + x] = static_cast<std::uint16_t>(v);
            }
        }
        seq.frames.push_back(std::move(fr));
    }
    return seq;
}

// Sequence 2: static room. A room with floor / two walls / ceiling.
// Per-frame: the *same* geometry plus per-pixel sensor noise. Tests
// the codec's response to noise + a static spatial layout.
Sequence make_static_room(const CorpusSpec& spec, Intrinsics intr, std::uint64_t seed) {
    Sequence seq;
    seq.name = "static_room";
    seq.frames.reserve(spec.frames);
    const std::uint16_t mask = static_cast<std::uint16_t>((1u << spec.bit_depth) - 1u);

    // Pre-compute the noise-free geometry once.
    std::vector<std::uint16_t> base_z(static_cast<std::size_t>(spec.width) * spec.height);
    for (std::uint32_t y = 0; y < spec.height; ++y) {
        for (std::uint32_t x = 0; x < spec.width; ++x) {
            const double nx = (x - intr.cx) / intr.fx;
            const double ny = (y - intr.cy) / intr.fy;
            // Floor: z = 1.5 / max(0.01, ny)  (intersect ray with y = 1.5m plane)
            // Ceiling: z = 1.5 / max(0.01, -ny)
            // Walls at x = +-2m
            double z = 5000.0;          // ceiling default
            if (ny > 0.05) z = std::min(z, 1500.0 / ny);
            if (ny < -0.05) z = std::min(z, 1500.0 / (-ny));
            if (nx > 0.05) z = std::min(z, 2000.0 / nx);
            if (nx < -0.05) z = std::min(z, 2000.0 / (-nx));
            int v = static_cast<int>(std::round(z));
            if (v < 0) v = 0;
            if (v > mask) v = mask;
            base_z[y * spec.width + x] = static_cast<std::uint16_t>(v);
        }
    }

    Lcg rng(seed ^ 0xA5A5A5A5ull);
    for (std::uint32_t f = 0; f < spec.frames; ++f) {
        Frame fr;
        fr.width = spec.width; fr.height = spec.height; fr.bit_depth = spec.bit_depth;
        fr.data = base_z;
        // Add gaussian-like noise (Box-Muller from two uniform samples).
        for (auto& v : fr.data) {
            const double u1 = std::max(1e-7, rng.next_unit());
            const double u2 = rng.next_unit();
            const double n  = std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * 3.14159265358979 * u2);
            const double sigma = 4.0;       // mm
            int u = static_cast<int>(v) + static_cast<int>(std::round(n * sigma));
            if (u < 0) u = 0;
            if (u > mask) u = mask;
            v = static_cast<std::uint16_t>(u);
        }
        seq.frames.push_back(std::move(fr));
    }
    return seq;
}

// Sequence 3: textured sphere. A sphere occupies the centre of the
// frame; the rest is background. Tests spatial prediction on
// non-planar smooth geometry.
Sequence make_textured_sphere(const CorpusSpec& spec, Intrinsics intr, std::uint64_t seed) {
    Sequence seq;
    seq.name = "textured_sphere";
    seq.frames.reserve(spec.frames);
    const std::uint16_t mask = static_cast<std::uint16_t>((1u << spec.bit_depth) - 1u);

    Lcg rng(seed ^ 0x5A5A5A5Aull);

    for (std::uint32_t f = 0; f < spec.frames; ++f) {
        Frame fr;
        fr.width = spec.width; fr.height = spec.height; fr.bit_depth = spec.bit_depth;
        fr.data.resize(static_cast<std::size_t>(spec.width) * spec.height);

        // Sphere oscillates radius slightly per frame.
        const double radius_m = 0.35 + 0.02 * std::sin(f * 0.2);
        const double center_z = 1.2;        // metres
        for (std::uint32_t y = 0; y < spec.height; ++y) {
            for (std::uint32_t x = 0; x < spec.width; ++x) {
                const double nx = (x - intr.cx) / intr.fx;
                const double ny = (y - intr.cy) / intr.fy;
                // Ray: P = t * (nx, ny, 1). Intersect sphere (0, 0, center_z)
                //   |t (nx,ny,1) - (0,0,center_z)|^2 = r^2
                const double a = nx*nx + ny*ny + 1.0;
                const double b = -2.0 * center_z;
                const double c = center_z*center_z - radius_m*radius_m;
                const double disc = b*b - 4.0*a*c;
                double z_m;
                if (disc >= 0) {
                    const double t = (-b - std::sqrt(disc)) / (2.0 * a);
                    z_m = t * 1.0;          // along z-axis component
                } else {
                    z_m = 2.5;               // background plane
                }
                // mm + small high-frequency texture noise (deterministic).
                int z_mm = static_cast<int>(std::round(z_m * 1000.0));
                z_mm += static_cast<int>(rng.next() % 7) - 3;
                if (z_mm < 0) z_mm = 0;
                if (z_mm > mask) z_mm = mask;
                fr.data[y * spec.width + x] = static_cast<std::uint16_t>(z_mm);
            }
        }
        seq.frames.push_back(std::move(fr));
    }
    return seq;
}

} // namespace

Intrinsics default_intrinsics(std::uint32_t width, std::uint32_t height) {
    Intrinsics i;
    // 60-degree horizontal FOV.
    i.fx = static_cast<float>(width  / (2.0 * std::tan(0.5 * 60.0 * 3.14159265358979 / 180.0)));
    i.fy = i.fx;       // square pixels
    i.cx = static_cast<float>(width  * 0.5);
    i.cy = static_cast<float>(height * 0.5);
    i.depth_scale_m_per_unit = 1.0f / 1000.0f;     // mm
    return i;
}

std::vector<Sequence> synthesize_corpus(const CorpusSpec& spec) {
    const auto intr = default_intrinsics(spec.width, spec.height);
    std::vector<Sequence> out;
    out.reserve(3);
    out.push_back(make_moving_plane    (spec, intr));
    out.push_back(make_static_room     (spec, intr, spec.seed));
    out.push_back(make_textured_sphere (spec, intr, spec.seed));
    // Final defensive masking, in case any generator produced a stray
    // value out of the bit-depth range due to FP rounding.
    const std::uint16_t mask = static_cast<std::uint16_t>((1u << spec.bit_depth) - 1u);
    for (auto& s : out) for (auto& f : s.frames) apply_mask(f.data, mask);
    return out;
}

} // namespace scc::bench

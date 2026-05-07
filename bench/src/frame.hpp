// bench/src/frame.hpp
//
// Common frame type for the comparator harness. Every codec sees the
// SAME Frame -- this is how we keep the comparison fair (Ultrathink #1).
// The bench owns the frame's storage; codec wrappers never resize.
//
// Depth values are uint16 regardless of bit_depth: a 12-bit sensor's
// values occupy [0, 4095] inside a uint16 channel. The high bits are
// guaranteed to be zero by the corpus loader (the synthetic generator
// masks; the real-world loader rejects out-of-range values).

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>

namespace scc::bench {

struct Frame {
    std::uint32_t width;
    std::uint32_t height;
    std::uint8_t  bit_depth;        // 8, 10, 12, or 16
    std::vector<std::uint16_t> data; // size == width * height

    [[nodiscard]] std::size_t pixel_count() const noexcept {
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }
    [[nodiscard]] std::size_t raw_size_bytes() const noexcept {
        return pixel_count() * sizeof(std::uint16_t);
    }
    [[nodiscard]] std::uint16_t max_value() const noexcept {
        return static_cast<std::uint16_t>((1u << bit_depth) - 1u);
    }
};

struct Sequence {
    std::string         name;        // human-readable, used in reports
    std::vector<Frame>  frames;      // one or more
};

// Pinhole intrinsics for the point-cloud diff. Default to "centred,
// fov=60deg" if no per-sequence intrinsics are present in the corpus
// manifest -- the metric is then comparable across runs even if not
// physically calibrated.
struct Intrinsics {
    float fx, fy;
    float cx, cy;
    float depth_scale_m_per_unit;    // 1/1000 for RealSense (mm); 1.0 for raw
};

} // namespace scc::bench

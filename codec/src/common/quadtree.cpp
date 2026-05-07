// codec/src/common/quadtree.cpp
//
// Movement detection + quad-tree region map.
//
// Reference: US Patent 10,827,161 B2, cols. 9-10.
// Evidence tags: [REQ-021], [REQ-022].

#include "scc/quadtree.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <stdexcept>

namespace scc {

namespace {

// Smallest power of two >= v (with `1` returned for v == 0 or v == 1).
std::uint32_t round_up_pow2(std::uint32_t v) noexcept {
    if (v <= 1) return 1;
    --v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

// Single-pass region histogram: track max(Delta) and the count of pixels
// with Delta > tau_low. Both accumulators feed the leaf-decision logic.
// [REQ-021] [REQ-022]
struct RegionStats {
    std::uint32_t max_delta;
    std::uint32_t count_above_tau_low;
    std::uint32_t area;
};

RegionStats scan_region(const std::uint16_t* curr,
                        const std::uint16_t* prev,
                        int                  W,
                        int                  x,
                        int                  y,
                        int                  aw,
                        int                  ah,
                        std::uint16_t        tau_low) noexcept {
    // Branch-free: max-reduction via cmov and count-increment via boolean
    // implicit conversion. Both forms are amenable to autovectorisation
    // (the compiler emits SIMD max + masked-add on AVX2 / NEON without
    // intrinsics in the source).
    std::uint32_t max_delta = 0;
    std::uint32_t count = 0;
    const std::uint32_t tl = tau_low;
    for (int j = 0; j < ah; ++j) {
        const std::ptrdiff_t base = static_cast<std::ptrdiff_t>(y + j) * W + x;
        const std::uint16_t* c = curr + base;
        const std::uint16_t* p = prev + base;
        for (int i = 0; i < aw; ++i) {
            const std::uint32_t a = c[i];
            const std::uint32_t b = p[i];
            const std::uint32_t d = (a > b) ? (a - b) : (b - a);
            max_delta = (d > max_delta) ? d : max_delta;
            count += (d > tl) ? 1u : 0u;
        }
    }
    return RegionStats{max_delta, count,
                       static_cast<std::uint32_t>(aw) * static_cast<std::uint32_t>(ah)};
}

void emit_leaf(RegionMap&    out,
               int           x,
               int           y,
               int           nominal_size,
               std::uint8_t  motion_class) {
    QuadRegion leaf;
    leaf.x = static_cast<std::uint16_t>(x);
    leaf.y = static_cast<std::uint16_t>(y);
    leaf.size = static_cast<std::uint16_t>(nominal_size);
    leaf.motion_class = motion_class;
    leaf.pattern_id = motion_class;       // 1:1 mapping in v1; future-proofed.
    out.leaves.push_back(leaf);
}

// Depth-first recursive partition. Order at each node is fixed:
// [TL, TR, BL, BR] -> deterministic leaf list across runs (Ultrathink #2).
void partition_recursive(const std::uint16_t* curr,
                         const std::uint16_t* prev,
                         int                  W,
                         int                  H,
                         int                  x,
                         int                  y,
                         int                  size,
                         std::uint16_t        tau_static,
                         std::uint16_t        tau_low,
                         int                  leaf_size_px,
                         std::uint8_t         depth,
                         RegionMap&           out) {
    // Clip to frame. Regions entirely off-frame produce no leaf.
    const int aw = std::min(size, W - x);
    const int ah = std::min(size, H - y);
    if (aw <= 0 || ah <= 0) return;

    if (depth > out.depth) out.depth = depth;

    const RegionStats st = scan_region(curr, prev, W, x, y, aw, ah, tau_low);

    // [REQ-021] Static class: max(Delta) <= tau_static.
    if (st.max_delta <= tau_static) {
        emit_leaf(out, x, y, size, /*class*/ 0);
        return;
    }
    // [REQ-021] Low class: percentile_95(Delta) <= tau_low. Equivalent to
    // count_above_tau_low * 20 <= area (95% of pixels at or below tau_low).
    if (st.count_above_tau_low * 20u <= st.area) {
        emit_leaf(out, x, y, size, /*class*/ 1);
        return;
    }
    // [REQ-022] Reached leaf granularity without a static / low classification.
    if (size <= leaf_size_px) {
        emit_leaf(out, x, y, size, /*class*/ 2);
        return;
    }
    // Split. [TL, TR, BL, BR] depth-first.
    const int half = size / 2;
    partition_recursive(curr, prev, W, H, x,         y,         half,
                        tau_static, tau_low, leaf_size_px,
                        static_cast<std::uint8_t>(depth + 1), out);
    partition_recursive(curr, prev, W, H, x + half,  y,         half,
                        tau_static, tau_low, leaf_size_px,
                        static_cast<std::uint8_t>(depth + 1), out);
    partition_recursive(curr, prev, W, H, x,         y + half,  half,
                        tau_static, tau_low, leaf_size_px,
                        static_cast<std::uint8_t>(depth + 1), out);
    partition_recursive(curr, prev, W, H, x + half,  y + half,  half,
                        tau_static, tau_low, leaf_size_px,
                        static_cast<std::uint8_t>(depth + 1), out);
}

} // namespace

// [REQ-021] [REQ-022]
RegionMap build_region_map(const std::uint16_t* curr,
                           const std::uint16_t* prev,
                           int                  W,
                           int                  H,
                           std::uint16_t        tau_static,
                           std::uint16_t        tau_low,
                           std::uint16_t        tau_high,
                           std::uint16_t        leaf_size_px) {
    (void)tau_high;   // reserved; see file header.

    if (leaf_size_px == 0 ||
        (leaf_size_px & static_cast<std::uint16_t>(leaf_size_px - 1)) != 0) {
        throw std::invalid_argument(
            "scc::build_region_map: leaf_size_px must be a power of two");
    }

    RegionMap rm;
    if (W <= 0 || H <= 0) return rm;
    if (curr == nullptr) {
        throw std::invalid_argument("scc::build_region_map: curr == nullptr");
    }

    // Root size: smallest power of two covering max(W, H). See file header
    // for rationale (clean halving + frame clipping).
    const std::uint32_t root_size = round_up_pow2(
        static_cast<std::uint32_t>(std::max(W, H)));
    assert(root_size <= 65535u && "frame dimension exceeds quadtree's uint16 size");

    // [REQ-022] I-frame: single root leaf, full update.
    if (prev == nullptr) {
        emit_leaf(rm, 0, 0, static_cast<int>(root_size), /*class*/ 2);
        return rm;
    }

    partition_recursive(curr, prev, W, H, 0, 0,
                        static_cast<int>(root_size),
                        tau_static, tau_low,
                        static_cast<int>(leaf_size_px),
                        /*depth*/ 0, rm);
    return rm;
}

} // namespace scc

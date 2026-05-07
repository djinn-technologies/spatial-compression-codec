// codec/include/scc/quadtree.hpp
//
// Movement detection + quad-tree region map for the Spatial Compression Codec.
//
// Reference: US Patent 10,827,161 B2, cols. 9-10. The patent partitions
// inter-frame depth deltas into a quad-tree of motion-classified regions:
// static blocks need no update, low-motion blocks update sparsely, high-
// motion blocks update fully. The InterlacePatternSelector consumes this
// map to choose which depth pixels to retransmit each frame.
//
// Evidence tags: [REQ-021], [REQ-022], [US10827161B2 col. 9-10].
//
// Algorithm (per col. 9-10)
// -------------------------
// 1. If `prev == nullptr` (an I-frame), the result is a single root leaf
//    with motion_class = 2 ("full update").
// 2. Otherwise: compute |curr[i,j] - prev[i,j]| at every pixel and
//    recursively partition starting from a square covering the frame:
//       a. if max(Delta) <= tau_static                       -> leaf class 0
//       b. else if percentile_95(Delta) <= tau_low           -> leaf class 1
//       c. else if region size <= leaf_size_px               -> leaf class 2
//       d. else split into 4 children [TL, TR, BL, BR] and recurse.
// 3. Output is the depth-first leaf list (stable order across runs).
//
// Boundary handling
// -----------------
// The root size is `next_pow2(max(W, H))` so that integer halving stays
// clean throughout the tree (Ultrathink #3). The prompt's "(0,0,max(W,H))"
// is honoured in spirit -- the root covers the entire frame -- but the
// literal nominal size is rounded up so no sub-region has an odd side
// that would create coverage gaps under split.
//
// Each region is then *clipped to the frame* during histogram analysis:
// children entirely outside the frame are not emitted as leaves; partial
// children store the nominal `size`, and the consumer derives the visible
// rect as
//     [x, min(x + size, W)) x [y, min(y + size, H)).
// The sum of `min(size, W-x) * min(size, H-y)` over all leaves equals W*H
// (exactly-once pixel coverage). [Ultrathink #3 fuzz invariant.]
//
// Histogram = two counters
// ------------------------
// Per the prompt we "compute the histogram of Delta in the region", but
// we only ever need (a) max(Delta) and (b) the cardinality of
// {Delta > tau_low}. We track both with two scalar accumulators in one
// pass over the region's pixels. The percentile-95 test
//     percentile_95(Delta) <= tau_low
// reduces to the integer comparison
//     count_above_tau_low * 20 <= area
// (no floating point, no per-region 65k-bucket allocation). See ADR-004.
//
// tau_high
// --------
// Exposed in the API but currently unused by the partitioner. The patent
// hints at finer class refinement (e.g. a fourth "very-high motion" class
// distinguishing outlier-pixel regions from uniform-high regions); the
// parameter is reserved for that work without breaking the wire format
// later. Today: pass any value (e.g. tau_low * 4); it is stored only in
// debug logs, not in the result.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace scc {

// Default leaf side. Patent col. 9-10 specifies 16 (matches H.264 macroblock
// alignment for SEI muxing). See ADR-004.
inline constexpr std::uint16_t kDefaultLeafSizePx = 16;

// One leaf in the depth-first traversal of the region tree.
struct QuadRegion {
    std::uint16_t x = 0;             // top-left column of the *nominal* square
    std::uint16_t y = 0;             // top-left row    of the *nominal* square
    std::uint16_t size = 0;          // nominal square side; visible rect is
                                     // [x, min(x+size, W)) x [y, min(y+size, H))
    std::uint8_t  motion_class = 0;  // 0 = static, 1 = low, 2 = high
    std::uint8_t  pattern_id = 0;    // currently identical to motion_class
};

struct RegionMap {
    std::vector<QuadRegion> leaves;  // depth-first [TL, TR, BL, BR] traversal
    std::uint8_t            depth = 0; // max recursion depth reached (0 = no split)
};

// [REQ-021] [REQ-022]
// Build a movement-classified region map for one (curr, prev) frame pair.
//
// Parameters:
//   curr           : current frame, W*H uint16_t depth pixels (must not be null when n > 0).
//   prev           : previous frame, same shape, OR nullptr for an I-frame
//                    (returns a single root leaf with motion_class = 2).
//   W, H           : frame dimensions in pixels. Both must be >= 0; any
//                    dimension == 0 returns an empty RegionMap.
//   tau_static     : max(Delta) <= tau_static  -> region is "static" (class 0).
//   tau_low        : percentile_95(Delta) <= tau_low -> region is "low" (class 1).
//   tau_high       : reserved, currently unused (see file header).
//   leaf_size_px   : minimum partition side (default 16). Must be a power of
//                    two in [1, 32768]; lower values produce more leaves.
//
// All thresholds are profile-tunable per call. The codec layer is expected
// to pick them based on the frame profile (e.g. "still scene", "fast motion").
RegionMap build_region_map(
    const std::uint16_t* curr,
    const std::uint16_t* prev,
    int                  W,
    int                  H,
    std::uint16_t        tau_static,
    std::uint16_t        tau_low,
    std::uint16_t        tau_high,
    std::uint16_t        leaf_size_px = kDefaultLeafSizePx);

} // namespace scc

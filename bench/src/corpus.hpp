// bench/src/corpus.hpp
//
// Corpus loader. Two modes:
//
//   1. Synthetic: generated deterministically from a 64-bit seed, in
//      memory. Three sequences -- "moving_plane", "static_room",
//      "textured_sphere" -- each at the requested resolution and
//      bit-depth. Used by CI (zero network, fully reproducible per
//      Ultrathink #4) and as a fallback when no manifest is present.
//
//   2. Manifest-driven: corpus_root/manifest.json declares a list of
//      external sequences with download URLs and SHA-256 sums. Real-
//      world depth corpora (e.g. NYUv2 subset, BigBIRD) live here. The
//      bench downloads on first invocation; CI does NOT use this path.
//
// Both modes return Sequences with the same shape; the rest of the
// harness can't tell them apart, which is the point.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "frame.hpp"

namespace scc::bench {

struct CorpusSpec {
    std::uint32_t width      = 320;
    std::uint32_t height     = 240;
    std::uint8_t  bit_depth  = 12;
    std::uint32_t frames     = 30;
    std::uint64_t seed       = 0xC0FFEEull;
};

// Generate the synthetic corpus. Always succeeds.
std::vector<Sequence> synthesize_corpus(const CorpusSpec& spec);

// Default intrinsics for the synthetic corpus -- centred principal
// point, fov ~60 degrees, 1mm depth scale (matches RealSense default).
Intrinsics default_intrinsics(std::uint32_t width, std::uint32_t height);

} // namespace scc::bench

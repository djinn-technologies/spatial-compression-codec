// codec/src/common/frequency.cpp
//
// FrequencyAnalyser TOP/R/B decomposition.
//
// Reference: US Patent 10,827,161 B2, col. 7.
// Evidence tags: [REQ-007], [REQ-008].

#include "scc/frequency.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace scc {

namespace {

// 65536 buckets cover the full int16 range; index = static_cast<uint16_t>(s[i]),
// which is the unsigned bit pattern (unique mapping).
constexpr std::size_t kHistSize = 1u << 16;

inline std::uint16_t to_key(std::int16_t v) noexcept {
    return static_cast<std::uint16_t>(v);
}
inline std::int16_t from_key(std::uint32_t k) noexcept {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(k));
}

} // namespace

// [REQ-007] decompose: histogram -> partial-sort top-K -> emit TOP/R/B streams.
DecomposeResult decompose(const std::int16_t* s, std::size_t n, std::uint8_t top_count) {
    if (top_count < kMinTopCount || top_count > kMaxTopCount) {
        throw std::invalid_argument("scc::decompose: top_count must be in [2, 16]");
    }

    DecomposeResult dr;
    dr.top.fill(0);
    dr.top_count = 0;
    if (n == 0) return dr;
    if (s == nullptr) {
        throw std::invalid_argument("scc::decompose: s == nullptr with n > 0");
    }

    // [REQ-007] Build dense 65536-bucket histogram. uint16_t bit-pattern
    // indexing gives a unique mapping for every int16 value, including
    // INT16_MIN.
    std::vector<std::uint32_t> hist(kHistSize, 0);
    for (std::size_t i = 0; i < n; ++i) {
        ++hist[to_key(s[i])];
    }

    // Collect non-zero buckets so partial_sort touches only present values.
    std::vector<std::pair<std::uint32_t, std::int16_t>> entries;
    entries.reserve(64);
    for (std::uint32_t k = 0; k < kHistSize; ++k) {
        if (hist[k] != 0) {
            entries.emplace_back(hist[k], from_key(k));
        }
    }

    // [REQ-007] Partial sort, NOT full sort. Order: descending count, then
    // ascending signed value (deterministic tie-break).
    const std::size_t k_eff = std::min<std::size_t>(top_count, entries.size());
    std::partial_sort(
        entries.begin(),
        entries.begin() + static_cast<std::ptrdiff_t>(k_eff),
        entries.end(),
        [](const auto& a, const auto& b) noexcept {
            if (a.first != b.first) return a.first > b.first;
            return a.second < b.second;
        });

    dr.top_count = static_cast<std::uint8_t>(k_eff);
    for (std::size_t i = 0; i < k_eff; ++i) {
        dr.top[i] = entries[i].second;
    }

    // Build value -> top-index lookup. Sentinel = -1 (not in TOP).
    // 65536 entries of int16_t = 128 KiB, populated only for the top_count
    // selected slots; the rest stays at -1.
    std::vector<std::int16_t> v_to_idx(kHistSize, std::int16_t{-1});
    for (std::uint8_t i = 0; i < dr.top_count; ++i) {
        v_to_idx[to_key(dr.top[i])] = static_cast<std::int16_t>(i);
    }

    // Pre-size output streams using the histogram counts (avoids realloc
    // in the hot walk below).
    std::size_t top_total = 0;
    for (std::uint8_t i = 0; i < dr.top_count; ++i) {
        top_total += hist[to_key(dr.top[i])];
    }
    dr.top_indices.reserve(top_total);
    dr.r_values.reserve(n - top_total);
    dr.b_mask.assign((n + 7) / 8, std::uint8_t{0});

    // [REQ-007] Walk S in reading order, emitting TOP/R streams and the
    // 1-bit-per-element B mask. LSB-first packing within each byte
    // (Ultrathink #4: byte-wise, endian-deterministic).
    for (std::size_t i = 0; i < n; ++i) {
        const std::int16_t idx = v_to_idx[to_key(s[i])];
        if (idx >= 0) {
            dr.top_indices.push_back(static_cast<std::uint8_t>(idx));
            // bit (i % 8) of byte (i / 8) stays 0 -- already zero-initialised.
        } else {
            dr.r_values.push_back(s[i]);
            dr.b_mask[i / 8] |=
                static_cast<std::uint8_t>(1u << (i % 8));
        }
    }

    return dr;
}

// [REQ-008] recompose: walk b_mask bit-by-bit, draw from TOP or R.
// Bit-exact inverse for any DecomposeResult produced by decompose.
void recompose(const DecomposeResult& dr, std::int16_t* s_out) noexcept {
    const std::size_t n = decomposed_element_count(dr);
    if (n == 0) return;
    assert(s_out != nullptr);

    std::size_t ti = 0, ri = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t bit =
            static_cast<std::uint8_t>((dr.b_mask[i / 8] >> (i % 8)) & 1u);
        if (bit == 0) {
            assert(ti < dr.top_indices.size());
            const std::uint8_t k = dr.top_indices[ti++];
            assert(k < dr.top_count);
            s_out[i] = dr.top[k];
        } else {
            assert(ri < dr.r_values.size());
            s_out[i] = dr.r_values[ri++];
        }
    }
}

} // namespace scc

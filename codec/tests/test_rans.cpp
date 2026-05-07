// codec/tests/test_rans.cpp
//
// Catch2 v3 + rapidcheck unit tests for the SCC rANS coder.
//
// Test plan (mapped to AI prompt #1, "Tests" block, and the Ultrathink Block):
//
//   T1  rans8_roundtrip          random alphabet, random data       prompt #1
//   T2  rans8_empty_input        zero symbols                       prompt #2
//   T3  rans8_single_symbol      alphabet=1, freq=4096              prompt #3
//   T4  rans8_skewed             one symbol = 4095/4096             prompt #4 / UT4
//   T5  rans16_roundtrip         full int16 alphabet                prompt #1
//   T6  rans8_against_ryg        byte-equal vs reference             UT2
//   T7  rans16_against_ryg       byte-equal vs reference             UT2
//   T8  prop_rans8_roundtrip     rapidcheck                          prompt #5 / UT4
//   T9  prop_rans16_roundtrip    rapidcheck                          prompt #5 / UT4

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <rapidcheck.h>
#include <rapidcheck/catch.h>

#include "scc/rans.hpp"
#include "vectors/ryg_compat.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <numeric>
#include <random>
#include <vector>

using scc::rans::Decoder16;
using scc::rans::Decoder8;
using scc::rans::Encoder16;
using scc::rans::Encoder8;
using scc::rans::kProbScale;
using scc::rans::ProbTable16;
using scc::rans::ProbTable8;

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

namespace {

// Build a ProbTable8 from raw counts; thin wrapper for readability.
ProbTable8 make_table8(const std::vector<uint32_t>& counts) {
    return ProbTable8::from_counts(counts.data(), counts.size());
}

ProbTable16 make_table16(const std::vector<uint32_t>& counts) {
    return ProbTable16::from_counts(counts.data(), counts.size());
}

// Generate a symbol sequence whose empirical distribution is `freqs`.
// Deterministic given the seed.
std::vector<uint8_t> sample_from_table8(const ProbTable8& tab, std::size_t n, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<uint32_t> dist(0, kProbScale - 1);
    std::vector<uint8_t> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        const uint32_t slot = dist(rng);
        out[i] = tab.slot_to_symbol[slot];
    }
    return out;
}

std::vector<uint16_t> sample_from_table16(const ProbTable16& tab, std::size_t n, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<uint32_t> dist(0, kProbScale - 1);
    std::vector<uint16_t> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        const uint32_t slot = dist(rng);
        out[i] = tab.slot_to_symbol[slot];
    }
    return out;
}

// Encode + decode round-trip via the streaming classes (not the free fns).
std::vector<uint8_t> stream_encode8(const std::vector<uint8_t>& data, const ProbTable8& tab) {
    Encoder8 enc(tab);
    for (auto it = data.rbegin(); it != data.rend(); ++it) enc.put(*it);
    std::vector<uint8_t> bytes;
    enc.finish(std::back_inserter(bytes));
    return bytes;
}

std::vector<uint8_t> stream_encode16(const std::vector<uint16_t>& data, const ProbTable16& tab) {
    Encoder16 enc(tab);
    for (auto it = data.rbegin(); it != data.rend(); ++it) enc.put(*it);
    std::vector<uint8_t> bytes;
    enc.finish(std::back_inserter(bytes));
    return bytes;
}

std::vector<uint8_t> stream_decode8(const std::vector<uint8_t>& bytes, std::size_t n,
                                    const ProbTable8& tab) {
    Decoder8 dec(bytes.data(), bytes.size(), tab);
    std::vector<uint8_t> out(n);
    for (std::size_t i = 0; i < n; ++i) out[i] = dec.get();
    return out;
}

std::vector<uint16_t> stream_decode16(const std::vector<uint8_t>& bytes, std::size_t n,
                                      const ProbTable16& tab) {
    Decoder16 dec(bytes.data(), bytes.size(), tab);
    std::vector<uint16_t> out(n);
    for (std::size_t i = 0; i < n; ++i) out[i] = dec.get();
    return out;
}

} // namespace

// --------------------------------------------------------------------------
// T1: rans8 random round-trip across a few seeded sizes & alphabets
// --------------------------------------------------------------------------

TEST_CASE("rans8_roundtrip", "[rans][rans8][roundtrip]") {
    const auto seed = GENERATE(uint64_t{0x5EED'1234}, uint64_t{0xC0FFEE99}, uint64_t{42});
    const auto n    = GENERATE(std::size_t{1}, std::size_t{17}, std::size_t{1024}, std::size_t{50000});

    std::mt19937_64 rng(seed ^ (n * 0x9E3779B97F4A7C15ULL));

    // Random alphabet size in [2, 256] and random nonzero counts.
    std::uniform_int_distribution<int> alpha_dist(2, 256);
    const std::size_t alphabet = static_cast<std::size_t>(alpha_dist(rng));
    std::vector<uint32_t> counts(alphabet);
    std::uniform_int_distribution<uint32_t> count_dist(1, 1000);
    for (auto& c : counts) c = count_dist(rng);

    const auto tab = make_table8(counts);
    REQUIRE(tab.alphabet_size == alphabet);

    const auto data    = sample_from_table8(tab, n, seed + 1);
    const auto bytes   = stream_encode8(data, tab);
    const auto decoded = stream_decode8(bytes, n, tab);
    REQUIRE(decoded == data);

    // Free-function path agrees with streaming path.
    const auto bytes_ff = scc::rans::encode(data.data(), data.size(), tab);
    REQUIRE(bytes_ff == bytes);
}

// --------------------------------------------------------------------------
// T2: empty input
// --------------------------------------------------------------------------

TEST_CASE("rans8_empty_input", "[rans][rans8][edge]") {
    const std::vector<uint32_t> counts{1, 1, 1, 1};   // 4-symbol alphabet
    const auto tab = make_table8(counts);

    const std::vector<uint8_t> data;                  // empty
    const auto bytes = stream_encode8(data, tab);
    // The flush still emits the 4-byte initial state.
    REQUIRE(bytes.size() == 4);

    const auto decoded = stream_decode8(bytes, 0, tab);
    REQUIRE(decoded.empty());
}

TEST_CASE("rans16_empty_input", "[rans][rans16][edge]") {
    const std::vector<uint32_t> counts{1, 1};
    const auto tab = make_table16(counts);

    const std::vector<uint16_t> data;
    const auto bytes = stream_encode16(data, tab);
    REQUIRE(bytes.size() == 4);

    const auto decoded = stream_decode16(bytes, 0, tab);
    REQUIRE(decoded.empty());
}

// --------------------------------------------------------------------------
// T3: single-symbol alphabet
// --------------------------------------------------------------------------

TEST_CASE("rans8_single_symbol", "[rans][rans8][edge]") {
    // freq=4096 for symbol 0 -> encoder is a (state-stable) no-op; decoder
    // always returns symbol 0.
    const std::vector<uint16_t> freqs{static_cast<uint16_t>(kProbScale)};
    const auto tab = ProbTable8::from_freqs(freqs.data(), 1);

    const std::vector<uint8_t> data(1000, 0);
    const auto bytes   = stream_encode8(data, tab);
    REQUIRE(bytes.size() == 4);                       // only the flushed state
    const auto decoded = stream_decode8(bytes, data.size(), tab);
    REQUIRE(decoded == data);
}

TEST_CASE("rans16_single_symbol", "[rans][rans16][edge]") {
    std::vector<uint16_t> freqs(7, 0);
    freqs[3] = static_cast<uint16_t>(kProbScale);
    const auto tab = ProbTable16::from_freqs(freqs.data(), freqs.size());

    const std::vector<uint16_t> data(500, 3);
    const auto bytes   = stream_encode16(data, tab);
    REQUIRE(bytes.size() == 4);
    const auto decoded = stream_decode16(bytes, data.size(), tab);
    REQUIRE(decoded == data);
}

// --------------------------------------------------------------------------
// T4: worst-case skewed (4095/4096)
// --------------------------------------------------------------------------

TEST_CASE("rans8_skewed_4095_over_4096", "[rans][rans8][skewed]") {
    // Two symbols: freq[0] = 4095, freq[1] = 1.
    std::array<uint16_t, 2> freqs{4095, 1};
    const auto tab = ProbTable8::from_freqs(freqs.data(), 2);

    // Long stream of mostly-symbol-0 with occasional symbol-1.
    std::vector<uint8_t> data(20000, 0);
    for (std::size_t i = 17; i < data.size(); i += 257) data[i] = 1;

    const auto bytes   = stream_encode8(data, tab);
    const auto decoded = stream_decode8(bytes, data.size(), tab);
    REQUIRE(decoded == data);
}

TEST_CASE("rans16_skewed_4095_over_4096", "[rans][rans16][skewed]") {
    std::vector<uint16_t> freqs{4095, 1};
    const auto tab = ProbTable16::from_freqs(freqs.data(), 2);

    std::vector<uint16_t> data(20000, 0);
    for (std::size_t i = 23; i < data.size(); i += 311) data[i] = 1;

    const auto bytes   = stream_encode16(data, tab);
    const auto decoded = stream_decode16(bytes, data.size(), tab);
    REQUIRE(decoded == data);
}

// --------------------------------------------------------------------------
// T5: rans16 round-trip with sparse + dense alphabets
// --------------------------------------------------------------------------

TEST_CASE("rans16_roundtrip", "[rans][rans16][roundtrip]") {
    const auto seed = GENERATE(uint64_t{0xBADC0FFE}, uint64_t{0xDEADBEEF});

    // Two regimes: small dense alphabet, and a moderately wide one.
    SECTION("dense small") {
        std::vector<uint32_t> counts(64);
        std::mt19937_64 rng(seed);
        std::uniform_int_distribution<uint32_t> d(1, 500);
        for (auto& c : counts) c = d(rng);
        const auto tab     = make_table16(counts);
        const auto data    = sample_from_table16(tab, 5000, seed + 3);
        const auto bytes   = stream_encode16(data, tab);
        const auto decoded = stream_decode16(bytes, data.size(), tab);
        REQUIRE(decoded == data);
    }

    SECTION("wide sparse") {
        std::vector<uint32_t> counts(2048, 0);
        std::mt19937_64 rng(seed ^ 0x55);
        std::uniform_int_distribution<uint32_t> idx(0, 2047);
        std::uniform_int_distribution<uint32_t> mass(1, 50);
        // ~200 distinct symbols populated; rest stay at zero.
        for (int k = 0; k < 200; ++k) counts[idx(rng)] += mass(rng);
        // Guarantee at least one nonzero count.
        counts[0] += 1;
        const auto tab     = make_table16(counts);
        const auto data    = sample_from_table16(tab, 8000, seed + 7);
        const auto bytes   = stream_encode16(data, tab);
        const auto decoded = stream_decode16(bytes, data.size(), tab);
        REQUIRE(decoded == data);
    }
}

// --------------------------------------------------------------------------
// T6 / T7: byte-for-byte vs the ryg-reference implementation (Ultrathink #2)
// --------------------------------------------------------------------------

TEST_CASE("rans8_byte_equality_against_ryg_reference", "[rans][rans8][ryg]") {
    const auto seed = GENERATE(uint64_t{0xAA55AA55}, uint64_t{0x12345678});
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> alpha_dist(2, 256);
    const std::size_t alphabet = static_cast<std::size_t>(alpha_dist(rng));
    std::vector<uint32_t> counts(alphabet);
    std::uniform_int_distribution<uint32_t> count_dist(1, 1000);
    for (auto& c : counts) c = count_dist(rng);
    const auto tab = make_table8(counts);

    const auto data       = sample_from_table8(tab, 4096, seed + 9);
    const auto ours_bytes = stream_encode8(data, tab);
    const auto ryg_bytes  = ryg_compat::ryg_encode8(data.data(), data.size(), tab);

    REQUIRE(ours_bytes == ryg_bytes);

    // Cross-decode: ryg encoder -> our decoder, our encoder -> ryg decoder.
    const auto via_ours = stream_decode8(ryg_bytes, data.size(), tab);
    REQUIRE(via_ours == data);
    const auto via_ryg  = ryg_compat::ryg_decode8(ours_bytes.data(), ours_bytes.size(),
                                                  data.size(), tab);
    REQUIRE(via_ryg == data);
}

TEST_CASE("rans16_byte_equality_against_ryg_reference", "[rans][rans16][ryg]") {
    const auto seed = GENERATE(uint64_t{0xFEEDBEEF}, uint64_t{0xCAFEF00D});
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> alpha_dist(2, 1024);
    const std::size_t alphabet = static_cast<std::size_t>(alpha_dist(rng));
    std::vector<uint32_t> counts(alphabet);
    std::uniform_int_distribution<uint32_t> count_dist(0, 200);
    for (auto& c : counts) c = count_dist(rng);
    counts[0] += 1;  // ensure at least one nonzero
    const auto tab = make_table16(counts);

    const auto data       = sample_from_table16(tab, 6000, seed + 11);
    const auto ours_bytes = stream_encode16(data, tab);
    const auto ryg_bytes  = ryg_compat::ryg_encode16(data.data(), data.size(), tab);

    REQUIRE(ours_bytes == ryg_bytes);

    const auto via_ours = stream_decode16(ryg_bytes, data.size(), tab);
    REQUIRE(via_ours == data);
    const auto via_ryg  = ryg_compat::ryg_decode16(ours_bytes.data(), ours_bytes.size(),
                                                   data.size(), tab);
    REQUIRE(via_ryg == data);
}

// --------------------------------------------------------------------------
// T8 / T9: rapidcheck property tests covering uniform + skewed regimes
// --------------------------------------------------------------------------
//
// Strategy: rapidcheck generates raw counts, we route them through
// from_counts so the resulting table is always valid (sum == 4096, no
// zero-freq for present symbols). Symbol streams are drawn from the table's
// own slot lookup, which exercises both the renormalisation boundary and
// every alphabet size.

namespace {

template <class Table, class Symbol>
struct GeneratedCase {
    Table              tab;
    std::vector<Symbol> data;
};

GeneratedCase<ProbTable8, uint8_t> gen_case8(const std::vector<uint32_t>& counts,
                                             std::size_t                  n,
                                             uint64_t                     stream_seed) {
    GeneratedCase<ProbTable8, uint8_t> g;
    g.tab  = make_table8(counts);
    g.data = sample_from_table8(g.tab, n, stream_seed);
    return g;
}

GeneratedCase<ProbTable16, uint16_t> gen_case16(const std::vector<uint32_t>& counts,
                                                std::size_t                  n,
                                                uint64_t                     stream_seed) {
    GeneratedCase<ProbTable16, uint16_t> g;
    g.tab  = make_table16(counts);
    g.data = sample_from_table16(g.tab, n, stream_seed);
    return g;
}

} // namespace

TEST_CASE("prop_rans8_roundtrip", "[rans][rans8][property]") {
    rc::prop("encode8 -> decode8 is identity over arbitrary alphabets and inputs",
             [](const std::vector<uint32_t>& raw_counts,
                std::size_t                  n_raw,
                uint64_t                     stream_seed) {
                 // Constrain ranges; rapidcheck generates anything otherwise.
                 if (raw_counts.empty()) return;
                 std::vector<uint32_t> counts = raw_counts;
                 if (counts.size() > 256) counts.resize(256);
                 // Ensure at least one nonzero count.
                 bool any = false;
                 for (auto c : counts) if (c != 0) { any = true; break; }
                 if (!any) counts[0] = 1;

                 const std::size_t n = (n_raw % 2048);
                 const auto g  = gen_case8(counts, n, stream_seed);
                 const auto bytes   = stream_encode8(g.data, g.tab);
                 const auto decoded = stream_decode8(bytes, g.data.size(), g.tab);
                 RC_ASSERT(decoded == g.data);
             });
}

TEST_CASE("prop_rans16_roundtrip", "[rans][rans16][property]") {
    rc::prop("encode16 -> decode16 is identity",
             [](const std::vector<uint32_t>& raw_counts,
                std::size_t                  n_raw,
                uint64_t                     stream_seed) {
                 if (raw_counts.empty()) return;
                 std::vector<uint32_t> counts = raw_counts;
                 if (counts.size() > 4096) counts.resize(4096);
                 bool any = false;
                 for (auto c : counts) if (c != 0) { any = true; break; }
                 if (!any) counts[0] = 1;

                 const std::size_t n = (n_raw % 2048);
                 const auto g  = gen_case16(counts, n, stream_seed);
                 const auto bytes   = stream_encode16(g.data, g.tab);
                 const auto decoded = stream_decode16(bytes, g.data.size(), g.tab);
                 RC_ASSERT(decoded == g.data);
             });
}

// --------------------------------------------------------------------------
// Targeted regressions: skewed and uniform property coverage (UT4)
// --------------------------------------------------------------------------

TEST_CASE("prop_rans8_skewed_distribution", "[rans][rans8][property][skewed]") {
    rc::prop("skewed distribution (one symbol >> all others) round-trips",
             [](std::size_t n_raw, uint8_t hot_sym, uint64_t stream_seed) {
                 const std::size_t alphabet = 4 + (hot_sym % 32);   // 4..35
                 std::vector<uint32_t> counts(alphabet, 1);
                 counts[hot_sym % alphabet] = 100000;                // dominant
                 const auto tab = make_table8(counts);
                 const std::size_t n = (n_raw % 4096);
                 const auto data    = sample_from_table8(tab, n, stream_seed);
                 const auto bytes   = stream_encode8(data, tab);
                 const auto decoded = stream_decode8(bytes, data.size(), tab);
                 RC_ASSERT(decoded == data);
             });
}

TEST_CASE("prop_rans8_uniform_distribution", "[rans][rans8][property][uniform]") {
    rc::prop("uniform distribution round-trips",
             [](std::size_t n_raw, uint8_t alpha_raw, uint64_t stream_seed) {
                 const std::size_t alphabet = 2 + (alpha_raw % 64);  // 2..65
                 std::vector<uint32_t> counts(alphabet, 1);
                 const auto tab = make_table8(counts);
                 const std::size_t n = (n_raw % 4096);
                 const auto data    = sample_from_table8(tab, n, stream_seed);
                 const auto bytes   = stream_encode8(data, tab);
                 const auto decoded = stream_decode8(bytes, data.size(), tab);
                 RC_ASSERT(decoded == data);
             });
}

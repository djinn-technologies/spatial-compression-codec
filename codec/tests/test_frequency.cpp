// codec/tests/test_frequency.cpp
//
// Catch2 v3 + rapidcheck tests for the SCC FrequencyAnalyser.
//
// Test plan, mapped to the prompt's test list and the Ultrathink Block:
//
//   F1  roundtrip_random           random inputs, several top_count        prompt #1
//   F2  all_in_top                 binary alphabet, top covers everything  prompt #2
//   F3  no_values_in_top           all unique, top picks 4 arbitrarily     prompt #3
//   F4  empty_input                n == 0                                  Ultrathink #2
//   F5  top_count_exceeds_unique   top_count > unique values; clamped      Ultrathink #3
//   F6  top_count_out_of_range     top_count not in [2,16] throws          contract
//   F7  bmask_bit_layout           hand-computed B mask for known input    Ultrathink #4
//   F8  bmask_endianness_invariant byte sequence determinism cross-call    Ultrathink #4
//   F9  prop_roundtrip             rapidcheck forall(s, k in [2,16])       prompt #4 / Ultrathink #5
//   F10 prop_roundtrip_all_k       each generated s tested with all k      Ultrathink #5
//   F11 top_coverage_synthetic     Gaussian sigma=1 ~88% top-4 coverage    Ultrathink #1

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <rapidcheck.h>
#include <rapidcheck/catch.h>

#include "scc/frequency.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <set>
#include <vector>

namespace {

std::vector<int16_t> random_int16(std::size_t n, uint64_t seed,
                                  int16_t lo = -1000, int16_t hi = 1000) {
    std::vector<int16_t> out(n);
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> d(lo, hi);
    for (auto& v : out) v = static_cast<int16_t>(d(rng));
    return out;
}

void roundtrip_check(const std::vector<int16_t>& s, uint8_t k) {
    auto dr = scc::decompose(s.data(), s.size(), k);
    REQUIRE(scc::decomposed_element_count(dr) == s.size());
    REQUIRE(dr.b_mask.size() == (s.size() + 7) / 8);

    std::vector<int16_t> s2(s.size(), 0);
    scc::recompose(dr, s2.data());
    REQUIRE(s2 == s);
}

} // namespace

// --------------------------------------------------------------------------
// F1: random inputs, several top_count
// --------------------------------------------------------------------------

TEST_CASE("frequency_roundtrip_random", "[frequency][roundtrip]") {
    const auto k    = GENERATE(uint8_t{2}, uint8_t{4}, uint8_t{8}, uint8_t{16});
    const auto seed = GENERATE(uint64_t{0xF12E9CABULL}, uint64_t{0xDEADBEEF});
    for (auto n : {std::size_t{1}, std::size_t{17}, std::size_t{1024},
                   std::size_t{50000}}) {
        const auto s = random_int16(n, seed ^ (n * 0x9E3779B97F4A7C15ULL));
        roundtrip_check(s, k);
    }
}

// --------------------------------------------------------------------------
// F2: binary alphabet, all values land in TOP
// --------------------------------------------------------------------------

TEST_CASE("frequency_all_in_top", "[frequency][edge]") {
    // Binary alphabet {0, 1}; top_count = 2 captures both.
    const std::vector<int16_t> s = {0, 1, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0};
    auto dr = scc::decompose(s.data(), s.size(), 2);
    REQUIRE(dr.top_count == 2);
    REQUIRE(dr.r_values.empty());
    REQUIRE(dr.top_indices.size() == s.size());
    // Every B bit must be 0 since nothing went to R.
    for (auto byte : dr.b_mask) {
        // We may have padding bits in the last byte; they're zero too.
        REQUIRE(byte == 0);
    }

    std::vector<int16_t> s2(s.size(), 0);
    scc::recompose(dr, s2.data());
    REQUIRE(s2 == s);
}

// --------------------------------------------------------------------------
// F3: every value unique - top picks first K, rest go to R
// --------------------------------------------------------------------------

TEST_CASE("frequency_no_values_in_top", "[frequency][edge]") {
    // 100 unique values, every count == 1. Top picks K (tie-break on value).
    std::vector<int16_t> s;
    s.reserve(100);
    for (int v = 0; v < 100; ++v) s.push_back(static_cast<int16_t>(v - 50));

    auto dr = scc::decompose(s.data(), s.size(), 4);
    REQUIRE(dr.top_count == 4);
    // Tie-break: ascending value, so top = {-50, -49, -48, -47}.
    REQUIRE(dr.top[0] == -50);
    REQUIRE(dr.top[1] == -49);
    REQUIRE(dr.top[2] == -48);
    REQUIRE(dr.top[3] == -47);
    REQUIRE(dr.top_indices.size() == 4);
    REQUIRE(dr.r_values.size() == 96);

    std::vector<int16_t> s2(s.size(), 0);
    scc::recompose(dr, s2.data());
    REQUIRE(s2 == s);
}

// --------------------------------------------------------------------------
// F4: empty input
// --------------------------------------------------------------------------

TEST_CASE("frequency_empty", "[frequency][edge]") {
    auto dr = scc::decompose(nullptr, 0, 4);
    REQUIRE(dr.top_count == 0);
    REQUIRE(dr.top_indices.empty());
    REQUIRE(dr.r_values.empty());
    REQUIRE(dr.b_mask.empty());
    REQUIRE(scc::decomposed_element_count(dr) == 0);

    // recompose with n == 0 must be a no-op.
    constexpr int16_t kSentinel = static_cast<int16_t>(0xDEADu);
    int16_t sentinel = kSentinel;
    scc::recompose(dr, &sentinel);
    REQUIRE(sentinel == kSentinel);
}

// --------------------------------------------------------------------------
// F5: top_count > unique_values is clamped (Ultrathink #3)
// --------------------------------------------------------------------------

TEST_CASE("frequency_top_count_exceeds_unique", "[frequency][edge]") {
    // 3 unique values, top_count = 8 -> effective 3.
    const std::vector<int16_t> s = {1, 2, 3, 1, 2, 3, 1, 2};
    auto dr = scc::decompose(s.data(), s.size(), 8);
    REQUIRE(dr.top_count == 3);
    // No element should land in R because all 3 unique values are in TOP.
    REQUIRE(dr.r_values.empty());

    std::vector<int16_t> s2(s.size(), 0);
    scc::recompose(dr, s2.data());
    REQUIRE(s2 == s);
}

TEST_CASE("frequency_single_value_input", "[frequency][edge]") {
    // Pathological: one unique value, top_count=4 -> effective 1.
    const std::vector<int16_t> s(20, int16_t{42});
    auto dr = scc::decompose(s.data(), s.size(), 4);
    REQUIRE(dr.top_count == 1);
    REQUIRE(dr.top[0] == 42);
    REQUIRE(dr.r_values.empty());
    REQUIRE(dr.top_indices.size() == s.size());
    std::vector<int16_t> s2(s.size(), 0);
    scc::recompose(dr, s2.data());
    REQUIRE(s2 == s);
}

// --------------------------------------------------------------------------
// F6: top_count not in [2, 16] throws (contract)
// --------------------------------------------------------------------------

TEST_CASE("frequency_top_count_out_of_range", "[frequency][contract]") {
    const std::vector<int16_t> s = {1, 2, 3};
    REQUIRE_THROWS_AS(scc::decompose(s.data(), s.size(), 0),  std::invalid_argument);
    REQUIRE_THROWS_AS(scc::decompose(s.data(), s.size(), 1),  std::invalid_argument);
    REQUIRE_THROWS_AS(scc::decompose(s.data(), s.size(), 17), std::invalid_argument);
    REQUIRE_THROWS_AS(scc::decompose(s.data(), s.size(), 255),std::invalid_argument);
    // Boundary cases that must NOT throw.
    REQUIRE_NOTHROW(scc::decompose(s.data(), s.size(), 2));
    REQUIRE_NOTHROW(scc::decompose(s.data(), s.size(), 16));
}

// --------------------------------------------------------------------------
// F7: hand-computed B-mask bit layout
// --------------------------------------------------------------------------

TEST_CASE("frequency_bmask_bit_layout", "[frequency][bitpack]") {
    // Engineer S so TOP membership is unambiguous (no count ties between
    // candidate-TOP and candidate-R values).
    //
    //   value 10 occurs 4x  -> TOP[0]    (highest count)
    //   value 20 occurs 2x  -> TOP[1]    (second-highest count)
    //   values 30, 31, 32 each occur 1x  -> all in R (each less than 20's count)
    //
    // S = [10, 30, 10, 10, 31, 20, 20, 32, 10]    (n = 9)
    //      T   R   T   T   R   T   T   R   T
    //
    // b_mask bits (LSB-first; bit index = element index):
    //   byte 0, bits [0..7]: T R T T R T T R = 0,1,0,0,1,0,0,1
    //                                        = 2 + 16 + 128 = 0x92
    //   byte 1, bit  [8]   : T = 0; bits [9..15] = padding (0). byte 1 = 0x00
    const std::vector<int16_t> s = {10, 30, 10, 10, 31, 20, 20, 32, 10};
    auto dr = scc::decompose(s.data(), s.size(), 2);

    REQUIRE(dr.top_count == 2);
    REQUIRE(dr.top[0] == 10);
    REQUIRE(dr.top[1] == 20);
    REQUIRE(dr.top_indices == std::vector<uint8_t>{0, 0, 0, 1, 1, 0});
    REQUIRE(dr.r_values == std::vector<int16_t>{30, 31, 32});
    REQUIRE(dr.b_mask.size() == 2);
    REQUIRE(dr.b_mask[0] == 0x92);
    REQUIRE(dr.b_mask[1] == 0x00);

    std::vector<int16_t> s2(s.size(), 0);
    scc::recompose(dr, s2.data());
    REQUIRE(s2 == s);
}

// --------------------------------------------------------------------------
// F8: same input -> same byte sequence on every call (Ultrathink #4)
// --------------------------------------------------------------------------

TEST_CASE("frequency_bmask_byte_sequence_deterministic", "[frequency][bitpack]") {
    const auto s = random_int16(10000, 0x12345);
    auto a = scc::decompose(s.data(), s.size(), 4);
    auto b = scc::decompose(s.data(), s.size(), 4);
    REQUIRE(a.top == b.top);
    REQUIRE(a.top_count == b.top_count);
    REQUIRE(a.top_indices == b.top_indices);
    REQUIRE(a.r_values == b.r_values);
    REQUIRE(a.b_mask == b.b_mask);
}

// --------------------------------------------------------------------------
// F9: rapidcheck forall (s, k in [2, 16]): recompose(decompose(s, k)) == s
// --------------------------------------------------------------------------

TEST_CASE("frequency_prop_roundtrip", "[frequency][property]") {
    rc::prop("recompose(decompose(s, k)) == s",
             [](const std::vector<int16_t>& s, uint8_t k_raw) {
                 const uint8_t k = static_cast<uint8_t>(2 + (k_raw % 15));
                 auto dr = scc::decompose(s.data(), s.size(), k);
                 std::vector<int16_t> s2(s.size(), 0);
                 scc::recompose(dr, s2.data());
                 RC_ASSERT(s2 == s);
             });
}

// --------------------------------------------------------------------------
// F10: each generated s tested with EVERY k in [2, 16] (Ultrathink #5)
// --------------------------------------------------------------------------

TEST_CASE("frequency_prop_roundtrip_all_top_counts", "[frequency][property]") {
    rc::prop("for all k in [2, 16]: recompose(decompose(s, k)) == s",
             [](const std::vector<int16_t>& s) {
                 for (uint8_t k = 2; k <= 16; ++k) {
                     auto dr = scc::decompose(s.data(), s.size(), k);
                     std::vector<int16_t> s2(s.size(), 0);
                     scc::recompose(dr, s2.data());
                     RC_ASSERT(s2 == s);
                 }
             });
}

// --------------------------------------------------------------------------
// F11: synthetic top-4 coverage check (Ultrathink #1)
// --------------------------------------------------------------------------
//
// Patent col. 7 claims top-4 captures 85-98% of values on a real depth
// frame. We don't have a real depth corpus here; we approximate with a
// Gaussian distribution rounded to int16. Standard math: with sigma = 1.0,
// P(round=0) = 0.383, P(round=+/-1) = 2 * 0.236 = 0.472, P(round=+/-2)
// = 2 * 0.061 = 0.122. Top-4 = {0, 1, -1, 2 or -2} captures >= 0.85 of
// mass. The test asserts >= 0.80 to allow for finite-sample noise.

TEST_CASE("frequency_top_coverage_synthetic", "[frequency][coverage]") {
    constexpr std::size_t kN = 100000;
    std::vector<int16_t> s(kN);
    std::mt19937_64 rng(0xC07AC0EEULL);
    std::normal_distribution<double> dist(0.0, 1.0);
    for (auto& v : s) {
        const double x = std::lround(dist(rng));
        v = static_cast<int16_t>(std::max(double{-32768}, std::min(double{32767}, x)));
    }

    auto dr = scc::decompose(s.data(), s.size(), 4);
    const double coverage =
        static_cast<double>(dr.top_indices.size()) / static_cast<double>(kN);
    INFO("Top-4 coverage on synthetic Gaussian (sigma=1): " << (coverage * 100.0) << "%");
    REQUIRE(coverage > 0.80);
    REQUIRE(coverage < 1.00);   // some R mass exists from the tails

    std::vector<int16_t> s2(kN, 0);
    scc::recompose(dr, s2.data());
    REQUIRE(s2 == s);
}

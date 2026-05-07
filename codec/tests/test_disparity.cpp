// codec/tests/test_disparity.cpp
//
// Catch2 v3 + rapidcheck tests for the SCC disparity matrix transform.
//
// Test plan, mapped to the prompt's test list and the Ultrathink Block:
//
//   D1  patent_formula        literal example matches the formula      cite-or-die
//   D2  roundtrip_uniform     uniform frame round-trips                prompt #1
//   D3  roundtrip_random      random frame, several W,H                prompt #1 / UT2 / UT3
//   D4  single_row            H = 1                                    prompt #2
//   D5  single_column         W = 1                                    prompt #3
//   D6  one_by_one            W = H = 1                                prompt #4
//   D7  overflow_extremes     d = {0, 65535} edge case                 UT1
//   D8  non_multiple_widths   W in {17, 31, 1281} stresses tail        UT2 / UT3
//   D9  scalar_avx_neon_agree all impls byte-equal on the same input   cross-impl
//   D10 prop_roundtrip        rapidcheck forall (W,H,d): inv(fwd) == d prompt #5

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <rapidcheck.h>
#include <rapidcheck/catch.h>

#include "scc/disparity.hpp"
#include "scc/detail/disparity_impls.hpp"

#include <cstdint>
#include <random>
#include <vector>

namespace {

std::vector<uint16_t> make_random(int W, int H, uint64_t seed) {
    std::vector<uint16_t> out(static_cast<std::size_t>(W) * H);
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<uint32_t> dist(0, 65535);
    for (auto& v : out) v = static_cast<uint16_t>(dist(rng));
    return out;
}

// Round-trip via the public dispatcher.
void roundtrip(int W, int H, const std::vector<uint16_t>& d,
               std::vector<uint16_t>& d_out) {
    std::vector<int16_t> s(d.size());
    scc::disparity_forward(d.data(), W, H, s.data());
    d_out.assign(d.size(), 0);
    scc::disparity_inverse(s.data(), W, H, d_out.data());
}

} // namespace

// --------------------------------------------------------------------------
// D1: patent formula -- hand-computed example
// --------------------------------------------------------------------------

TEST_CASE("disparity_patent_formula", "[disparity][cite]") {
    // 0-indexed:
    //   d[0] = {1, 5,  9}
    //   d[1] = {3, 5, 13}
    //
    //   S[0][0] = d[0][0]              = 1
    //   S[0][1] = d[0][1] - d[0][0]    = 5 - 1 = 4
    //   S[0][2] = d[0][2] - d[0][1]    = 9 - 5 = 4
    //   S[1][0] = d[1][0] - d[0][0]    = 3 - 1 = 2     (vertical, i >= 1)
    //   S[1][1] = d[1][1] - d[0][1]    = 5 - 5 = 0
    //   S[1][2] = d[1][2] - d[0][2]    = 13 - 9 = 4
    constexpr int W = 3, H = 2;
    const std::vector<uint16_t> d = {1, 5, 9, 3, 5, 13};
    std::vector<int16_t> s(d.size());
    scc::disparity_forward(d.data(), W, H, s.data());
    REQUIRE(s[0] == 1);
    REQUIRE(s[1] == 4);
    REQUIRE(s[2] == 4);
    REQUIRE(s[3] == 2);
    REQUIRE(s[4] == 0);
    REQUIRE(s[5] == 4);

    std::vector<uint16_t> d2(d.size(), 0);
    scc::disparity_inverse(s.data(), W, H, d2.data());
    REQUIRE(d2 == d);
}

// --------------------------------------------------------------------------
// D2: uniform frame -- all values equal -> all S except anchor are zero
// --------------------------------------------------------------------------

TEST_CASE("disparity_roundtrip_uniform", "[disparity][roundtrip]") {
    const auto WH = GENERATE(std::pair{8, 4}, std::pair{31, 17}, std::pair{1280, 720});
    const int W = WH.first, H = WH.second;
    const std::vector<uint16_t> d(static_cast<std::size_t>(W) * H, uint16_t{0xABCD});

    std::vector<int16_t> s(d.size());
    scc::disparity_forward(d.data(), W, H, s.data());
    REQUIRE(s[0] == static_cast<int16_t>(0xABCD));
    for (std::size_t k = 1; k < s.size(); ++k) {
        REQUIRE(s[k] == 0);
    }

    std::vector<uint16_t> d2(d.size(), 0);
    scc::disparity_inverse(s.data(), W, H, d2.data());
    REQUIRE(d2 == d);
}

// --------------------------------------------------------------------------
// D3: random frame, multiple sizes including non-vector-aligned widths
// --------------------------------------------------------------------------

TEST_CASE("disparity_roundtrip_random", "[disparity][roundtrip]") {
    const auto seed = GENERATE(uint64_t{0xD15A1234}, uint64_t{0xBEEF5EED});
    for (auto WH : {std::pair{1, 1}, std::pair{16, 1}, std::pair{17, 17},
                    std::pair{32, 32}, std::pair{255, 255}, std::pair{1280, 720}}) {
        const int W = WH.first, H = WH.second;
        const auto d = make_random(W, H, seed ^ (W * 0x9E3779B97F4A7C15ULL) ^ H);
        std::vector<uint16_t> d2;
        roundtrip(W, H, d, d2);
        REQUIRE(d2 == d);
    }
}

// --------------------------------------------------------------------------
// D4 / D5 / D6: degenerate shapes
// --------------------------------------------------------------------------

TEST_CASE("disparity_single_row", "[disparity][edge]") {
    constexpr int W = 31, H = 1;
    const auto d = make_random(W, H, 0xABBA);
    std::vector<uint16_t> d2;
    roundtrip(W, H, d, d2);
    REQUIRE(d2 == d);
}

TEST_CASE("disparity_single_column", "[disparity][edge]") {
    constexpr int W = 1, H = 47;
    const auto d = make_random(W, H, 0xCAFE);
    std::vector<uint16_t> d2;
    roundtrip(W, H, d, d2);
    REQUIRE(d2 == d);
}

TEST_CASE("disparity_one_by_one", "[disparity][edge]") {
    constexpr int W = 1, H = 1;
    const std::vector<uint16_t> d = {42};
    std::vector<uint16_t> d2;
    roundtrip(W, H, d, d2);
    REQUIRE(d2 == d);
}

// --------------------------------------------------------------------------
// D7: int16 overflow at the largest legal depth difference (Ultrathink #1)
// --------------------------------------------------------------------------

TEST_CASE("disparity_overflow_extremes", "[disparity][overflow]") {
    // Worst-case horizontal: 0 -> 65535. Diff = 65535 (fits in uint16),
    // stored as int16 = -1. Inverse: 0 + (uint16)(-1) = 65535. Reversible.
    {
        constexpr int W = 2, H = 1;
        const std::vector<uint16_t> d = {0, 65535};
        std::vector<int16_t> s(2);
        scc::disparity_forward(d.data(), W, H, s.data());
        REQUIRE(s[0] == 0);
        REQUIRE(s[1] == static_cast<int16_t>(-1));   // bit pattern 0xFFFF
        std::vector<uint16_t> d2(2, 0);
        scc::disparity_inverse(s.data(), W, H, d2.data());
        REQUIRE(d2 == d);
    }
    // Worst-case vertical: row 0 = 65535, row 1 = 0. Diff = -65535 mod 2^16 = 1.
    {
        constexpr int W = 1, H = 2;
        const std::vector<uint16_t> d = {65535, 0};
        std::vector<int16_t> s(2);
        scc::disparity_forward(d.data(), W, H, s.data());
        REQUIRE(static_cast<uint16_t>(s[0]) == 65535);
        REQUIRE(s[1] == 1);                          // (uint16)(0 - 65535) == 1
        std::vector<uint16_t> d2(2, 0);
        scc::disparity_inverse(s.data(), W, H, d2.data());
        REQUIRE(d2 == d);
    }
    // Saturated frame near the int16 boundary: every other depth alternates
    // between 0 and 65535 horizontally and vertically.
    {
        constexpr int W = 8, H = 8;
        std::vector<uint16_t> d(static_cast<std::size_t>(W) * H);
        for (int i = 0; i < H; ++i)
            for (int j = 0; j < W; ++j)
                d[static_cast<std::size_t>(i) * W + j]
                    = ((i + j) & 1) ? uint16_t{0xFFFF} : uint16_t{0};
        std::vector<uint16_t> d2;
        roundtrip(W, H, d, d2);
        REQUIRE(d2 == d);
    }
}

// --------------------------------------------------------------------------
// D8: widths that don't divide cleanly into vector chunks (Ultrathink #2/#3)
// --------------------------------------------------------------------------

TEST_CASE("disparity_non_multiple_widths", "[disparity][simd][tail]") {
    // 17 = 16 + 1 (one full AVX2 chunk + 1 tail).
    // 31 = 16 + 15 (one full chunk + 15 tail).
    // 33 = 32 + 1.
    // 1281 = 80*16 + 1.
    // For NEON (8-wide): 17 = 16 + 1; 31 = 24 + 7. Both stress the tail.
    for (int W : {1, 2, 7, 8, 9, 15, 16, 17, 23, 24, 31, 32, 33, 1281}) {
        for (int H : {1, 2, 3, 64}) {
            const uint64_t seed = 0xDEAD0000ULL
                                ^ (static_cast<uint64_t>(W) << 16)
                                ^ static_cast<uint64_t>(H);
            const auto d = make_random(W, H, seed);
            std::vector<uint16_t> d2;
            roundtrip(W, H, d, d2);
            INFO("W=" << W << " H=" << H);
            REQUIRE(d2 == d);
        }
    }
}

// --------------------------------------------------------------------------
// D9: scalar / AVX2 / NEON impls all agree byte-for-byte on the same input.
// (Bypasses the dispatcher and exercises each impl directly.)
// --------------------------------------------------------------------------

TEST_CASE("disparity_impls_agree_byte_for_byte", "[disparity][simd]") {
    constexpr int W = 1280, H = 47;
    const auto d = make_random(W, H, 0xA17A17);

    std::vector<int16_t>  s_scalar(d.size());
    std::vector<uint16_t> d_scalar(d.size());
    scc::detail::disparity_forward_scalar(d.data(), W, H, s_scalar.data());
    scc::detail::disparity_inverse_scalar(s_scalar.data(), W, H, d_scalar.data());
    REQUIRE(d_scalar == d);

#if defined(SCC_HAS_AVX2_BUILD)
    if (scc::detail::cpu_supports_avx2()) {
        std::vector<int16_t>  s_avx(d.size());
        std::vector<uint16_t> d_avx(d.size());
        scc::detail::disparity_forward_avx2(d.data(), W, H, s_avx.data());
        REQUIRE(s_avx == s_scalar);                  // bit-for-bit
        scc::detail::disparity_inverse_avx2(s_avx.data(), W, H, d_avx.data());
        REQUIRE(d_avx == d);
    }
#endif

#if defined(SCC_HAS_NEON_BUILD)
    {
        std::vector<int16_t>  s_neon(d.size());
        std::vector<uint16_t> d_neon(d.size());
        scc::detail::disparity_forward_neon(d.data(), W, H, s_neon.data());
        REQUIRE(s_neon == s_scalar);
        scc::detail::disparity_inverse_neon(s_neon.data(), W, H, d_neon.data());
        REQUIRE(d_neon == d);
    }
#endif
}

// --------------------------------------------------------------------------
// D10: rapidcheck property -- forall (W, H, d): inverse(forward(d)) == d
// --------------------------------------------------------------------------

TEST_CASE("disparity_prop_roundtrip", "[disparity][property]") {
    rc::prop("inverse(forward(d)) == d for arbitrary (W,H,d)",
             [](uint16_t w_raw, uint16_t h_raw,
                const std::vector<uint16_t>& body) {
                 // Constrain W,H to [1, 256] so the test runs fast; the patent
                 // claim is forall [1, 4096], but rapidcheck shrinking on
                 // 4096*4096 frames is not a useful trade.
                 const int W = 1 + (w_raw % 256);
                 const int H = 1 + (h_raw % 256);
                 const std::size_t n = static_cast<std::size_t>(W) * H;
                 std::vector<uint16_t> d(n);
                 for (std::size_t k = 0; k < n; ++k) {
                     d[k] = body.empty() ? uint16_t{0}
                                         : body[k % body.size()];
                 }
                 std::vector<int16_t>  s(n);
                 std::vector<uint16_t> d2(n);
                 scc::disparity_forward(d.data(), W, H, s.data());
                 scc::disparity_inverse(s.data(), W, H, d2.data());
                 RC_ASSERT(d2 == d);
             });
}

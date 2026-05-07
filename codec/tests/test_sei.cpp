// codec/tests/test_sei.cpp
//
// Catch2 v3 + rapidcheck tests for the SCC SEI muxer / demuxer.
//
// Test plan, mapped to the prompt's test list and the Ultrathink Block:
//
//   S1  roundtrip_minimal               mux -> demux -> equal           prompt #1
//   S2  roundtrip_random                random valid payloads            prompt #1
//   S3  nal_escape_roundtrip            payload contains 00 00 00 ...   prompt #2 / Ultrathink #1
//   S4  nal_escape_unit                 escape/unescape are inverses    Ultrathink #1
//   S5  truncated_input_rejected        every prefix of a good payload  prompt #3 / Ultrathink #2
//   S6  oversized_length_rejected       u32 length > buffer remainder   prompt #4 / Ultrathink #2
//   S7  invalid_uuid_rejected            non-SCC payload                 contract
//   S8  invalid_bit_depth_rejected      bit_depth not in {8,12,16}      contract
//   S9  invalid_top_count_rejected       top_count not in [2,16]         contract
//   S10 max_payload_cap_enforced         input > max_payload returns ok=false  Ultrathink #2
//   S11 prop_demux_random_bytes_no_crash rapidcheck on arbitrary bytes   prompt #5 (portable)
//   S12 prop_roundtrip                   rapidcheck on valid payloads    prompt #1

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <rapidcheck.h>
#include <rapidcheck/catch.h>

#include "scc/sei.hpp"
#include "scc/detail/sei_internal.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <vector>

using scc::sei::byte_span;
using scc::sei::DemuxResult;
using scc::sei::demux;
using scc::sei::FrameHeader;
using scc::sei::kSccSeiUuid;
using scc::sei::mux;

namespace {

FrameHeader sample_hdr() {
    return FrameHeader{1280, 720, 12, 0x01};
}

std::array<int16_t, 16> sample_top(uint8_t k) {
    std::array<int16_t, 16> t{};
    for (uint8_t i = 0; i < k && i < 16; ++i) {
        t[i] = static_cast<int16_t>((i & 1) ? -static_cast<int>(i) : static_cast<int>(i));
    }
    return t;
}

std::vector<uint8_t> random_bytes(std::size_t n, uint64_t seed) {
    std::vector<uint8_t> v(n);
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> d(0, 255);
    for (auto& b : v) b = static_cast<uint8_t>(d(rng));
    return v;
}

} // namespace

// --------------------------------------------------------------------------
// S1: minimal round-trip
// --------------------------------------------------------------------------

TEST_CASE("sei_roundtrip_minimal", "[sei][roundtrip]") {
    const auto hdr = sample_hdr();
    const auto top = sample_top(4);
    const std::vector<uint8_t> rt{0xDE, 0xAD};
    const std::vector<uint8_t> rb{0xBE, 0xEF};
    const std::vector<uint8_t> rr{0xFA, 0xCE};
    const std::vector<uint8_t> qd{0x10, 0x20, 0x30};

    auto bytes = mux(hdr, top, 4, rt, rb, rr, qd);
    REQUIRE(!bytes.empty());

    auto r = demux(bytes);
    REQUIRE(r.ok);
    REQUIRE(r.hdr.width      == hdr.width);
    REQUIRE(r.hdr.height     == hdr.height);
    REQUIRE(r.hdr.bit_depth  == hdr.bit_depth);
    REQUIRE(r.hdr.mode_flags == hdr.mode_flags);
    REQUIRE(r.top_count == 4);
    for (uint8_t i = 0; i < 4; ++i) REQUIRE(r.top[i] == top[i]);
    // unused top slots are zeroed by demuxer.
    for (uint8_t i = 4; i < 16; ++i) REQUIRE(r.top[i] == 0);
    REQUIRE(r.rans_top      == rt);
    REQUIRE(r.rans_b        == rb);
    REQUIRE(r.rans_r        == rr);
    REQUIRE(r.quadtree_desc == qd);
}

// --------------------------------------------------------------------------
// S2: random valid payloads
// --------------------------------------------------------------------------

TEST_CASE("sei_roundtrip_random_payloads", "[sei][roundtrip]") {
    const auto seed = GENERATE(uint64_t{0xC0FFEE01}, uint64_t{0x12345678});
    std::mt19937_64 rng(seed);
    for (int trial = 0; trial < 8; ++trial) {
        FrameHeader hdr;
        hdr.width      = static_cast<uint16_t>(1 + (rng() % 65535));
        hdr.height     = static_cast<uint16_t>(1 + (rng() % 65535));
        hdr.bit_depth  = static_cast<uint8_t>((rng() % 3 == 0) ? 8 :
                                              (rng() % 2 == 0) ? 12 : 16);
        hdr.mode_flags = static_cast<uint8_t>(rng() & 0xFF);
        const uint8_t k = static_cast<uint8_t>(2 + (rng() % 15));
        std::array<int16_t, 16> top{};
        for (uint8_t i = 0; i < k; ++i) {
            top[i] = static_cast<int16_t>(rng());
        }
        const auto rt = random_bytes(rng() % 200,  seed + 1);
        const auto rb = random_bytes(rng() % 200,  seed + 2);
        const auto rr = random_bytes(rng() % 200,  seed + 3);
        const auto qd = random_bytes(rng() % 200,  seed + 4);
        auto bytes = mux(hdr, top, k, rt, rb, rr, qd);
        REQUIRE(!bytes.empty());
        auto r = demux(bytes);
        REQUIRE(r.ok);
        REQUIRE(r.hdr.width      == hdr.width);
        REQUIRE(r.hdr.height     == hdr.height);
        REQUIRE(r.hdr.bit_depth  == hdr.bit_depth);
        REQUIRE(r.hdr.mode_flags == hdr.mode_flags);
        REQUIRE(r.top_count == k);
        for (uint8_t i = 0; i < k; ++i) REQUIRE(r.top[i] == top[i]);
        REQUIRE(r.rans_top      == rt);
        REQUIRE(r.rans_b        == rb);
        REQUIRE(r.rans_r        == rr);
        REQUIRE(r.quadtree_desc == qd);
    }
}

// --------------------------------------------------------------------------
// S3: payload that aggressively triggers NAL escape
// --------------------------------------------------------------------------

TEST_CASE("sei_nal_escape_roundtrip", "[sei][nal]") {
    const auto hdr = FrameHeader{1, 1, 8, 0};
    auto top = sample_top(2);
    // A stream peppered with the patterns that get escaped.
    std::vector<uint8_t> rt{0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03,
                            0x00, 0x00, 0x03, 0x00, 0x00, 0xFF};
    std::vector<uint8_t> rb{0x00, 0x00, 0x00};
    std::vector<uint8_t> rr;
    std::vector<uint8_t> qd{0x00, 0x00, 0x00, 0x00};

    auto bytes = mux(hdr, top, 2, rt, rb, rr, qd);
    REQUIRE(!bytes.empty());

    // §7.4.1.1: the EBSP must NOT contain the 3-byte sequences
    //   0x00 0x00 0x00, 0x00 0x00 0x01, or 0x00 0x00 0x02.
    // (0x00 0x00 0x03 is the inserted escape and IS allowed; bytes >= 0x04
    // after 0x00 0x00 don't need escaping.)
    bool any_escape_present = false;
    for (std::size_t i = 0; i + 2 < bytes.size(); ++i) {
        if (bytes[i] == 0x00 && bytes[i + 1] == 0x00) {
            const uint8_t b2 = bytes[i + 2];
            INFO("triple at " << i << ": 00 00 " << static_cast<int>(b2));
            REQUIRE(b2 != 0x00);
            REQUIRE(b2 != 0x01);
            REQUIRE(b2 != 0x02);
            if (b2 == 0x03) any_escape_present = true;
        }
    }
    // Confirm at least one escape was actually inserted (input was designed
    // to trigger several).
    REQUIRE(any_escape_present);

    auto r = demux(bytes);
    REQUIRE(r.ok);
    REQUIRE(r.rans_top      == rt);
    REQUIRE(r.rans_b        == rb);
    REQUIRE(r.rans_r        == rr);
    REQUIRE(r.quadtree_desc == qd);
}

// --------------------------------------------------------------------------
// S4: nal_escape / nal_unescape are exact inverses
// --------------------------------------------------------------------------

TEST_CASE("sei_nal_escape_is_invertible", "[sei][nal]") {
    using scc::sei::detail::nal_escape;
    using scc::sei::detail::nal_unescape;

    const std::vector<std::vector<uint8_t>> cases = {
        {},
        {0x00},
        {0x00, 0x00},
        {0x00, 0x00, 0x00},
        {0x00, 0x00, 0x01},
        {0x00, 0x00, 0x02},
        {0x00, 0x00, 0x03},
        {0x00, 0x00, 0x04},                       // no escape
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0xFF, 0xFE, 0x00, 0x00, 0x00, 0x01, 0x02, 0xFF},
    };
    for (const auto& rbsp : cases) {
        auto ebsp = nal_escape(byte_span(rbsp));
        auto back = nal_unescape(byte_span(ebsp));
        REQUIRE(back == rbsp);
    }
}

// --------------------------------------------------------------------------
// S5: every prefix of a good payload is rejected
// --------------------------------------------------------------------------

TEST_CASE("sei_truncated_input_rejected", "[sei][validation]") {
    // The wire format's `quadtree_descriptor` consumes the rest of the
    // buffer, so any non-empty quadtree tail accepts arbitrary truncations
    // (the demux just sees a shorter quadtree_desc). To assert the strict
    // "every prefix rejected" property we use an empty quadtree_desc; the
    // EBSP then ends EXACTLY at the rans_r_len u32 and any strict prefix
    // leaves a mandatory header byte or u32-length field incomplete.
    const auto hdr = FrameHeader{1, 1, 8, 0};
    std::array<int16_t, 16> top{};
    top[0] = 1;
    top[1] = 2;
    const std::vector<uint8_t> rt{0xDE, 0xAD};
    const std::vector<uint8_t> rb{0xBE, 0xEF};
    const std::vector<uint8_t> rr{0xFA, 0xCE};
    const std::vector<uint8_t> qd;          // empty
    auto bytes = mux(hdr, top, 2, rt, rb, rr, qd);
    REQUIRE(!bytes.empty());
    auto r_full = demux(bytes);
    REQUIRE(r_full.ok);

    for (std::size_t cut = 0; cut < bytes.size(); ++cut) {
        std::vector<uint8_t> truncated(bytes.begin(), bytes.begin() + cut);
        auto r = demux(truncated);
        INFO("cut " << cut << " of " << bytes.size());
        REQUIRE_FALSE(r.ok);
    }
}

// --------------------------------------------------------------------------
// S6: a u32 length field that claims more bytes than remain is rejected
// without ever allocating
// --------------------------------------------------------------------------

TEST_CASE("sei_oversized_length_rejected", "[sei][validation]") {
    using scc::sei::detail::nal_escape;
    using scc::sei::detail::write_u16_be;
    using scc::sei::detail::write_u32_be;

    // Build a minimal RBSP up to the point where rans_top_len is written,
    // then write a huge length and stop. The demuxer must reject *before*
    // attempting to allocate the rans_top vector.
    std::vector<uint8_t> rbsp;
    rbsp.insert(rbsp.end(), kSccSeiUuid.begin(), kSccSeiUuid.end());
    rbsp.push_back(scc::sei::kVersionMajor);
    rbsp.push_back(scc::sei::kVersionMinor);

    auto append_u16 = [&](uint16_t v) {
        uint8_t b[2]; write_u16_be(b, v);
        rbsp.insert(rbsp.end(), b, b + 2);
    };
    auto append_u32 = [&](uint32_t v) {
        uint8_t b[4]; write_u32_be(b, v);
        rbsp.insert(rbsp.end(), b, b + 4);
    };

    append_u16(1);                  // width
    append_u16(1);                  // height
    rbsp.push_back(8);              // bit_depth
    rbsp.push_back(0);              // mode_flags
    rbsp.push_back(2);              // top_count = 2
    append_u16(0x0001);             // top[0] = 1   (no zero runs)
    append_u16(0x0002);             // top[1] = 2
    append_u32(0xFFFFFFFFu);        // rans_top_len -- catastrophic claim
    // No rans_top bytes follow; demuxer must reject.

    auto ebsp = nal_escape(byte_span(rbsp));
    auto r = demux(byte_span(ebsp));
    REQUIRE_FALSE(r.ok);
}

// --------------------------------------------------------------------------
// S7..S10: small contract checks
// --------------------------------------------------------------------------

TEST_CASE("sei_invalid_uuid_rejected", "[sei][validation]") {
    auto bytes = mux(sample_hdr(), sample_top(4), 4, byte_span{}, byte_span{},
                     byte_span{}, byte_span{});
    REQUIRE(bytes.size() >= 16);
    bytes[0] ^= 0xFF;   // corrupt UUID
    auto r = demux(bytes);
    REQUIRE_FALSE(r.ok);
}

TEST_CASE("sei_invalid_bit_depth_rejected", "[sei][validation]") {
    FrameHeader bad = sample_hdr();
    bad.bit_depth = 7;
    auto bytes = mux(bad, sample_top(4), 4, byte_span{}, byte_span{},
                     byte_span{}, byte_span{});
    REQUIRE(bytes.empty());   // mux refuses
}

TEST_CASE("sei_invalid_top_count_rejected", "[sei][validation]") {
    REQUIRE(mux(sample_hdr(), sample_top(4), 0,  byte_span{}, byte_span{},
                byte_span{}, byte_span{}).empty());
    REQUIRE(mux(sample_hdr(), sample_top(4), 1,  byte_span{}, byte_span{},
                byte_span{}, byte_span{}).empty());
    REQUIRE(mux(sample_hdr(), sample_top(4), 17, byte_span{}, byte_span{},
                byte_span{}, byte_span{}).empty());
    REQUIRE(!mux(sample_hdr(), sample_top(4), 2,  byte_span{}, byte_span{},
                 byte_span{}, byte_span{}).empty());
    REQUIRE(!mux(sample_hdr(), sample_top(16), 16, byte_span{}, byte_span{},
                 byte_span{}, byte_span{}).empty());
}

TEST_CASE("sei_max_payload_cap_enforced", "[sei][validation]") {
    auto bytes = mux(sample_hdr(), sample_top(4), 4,
                     byte_span{}, byte_span{}, byte_span{}, byte_span{});
    REQUIRE(!bytes.empty());
    // A cap below the actual payload size forces rejection.
    auto r_capped = demux(bytes, /*max_payload_bytes=*/bytes.size() - 1);
    REQUIRE_FALSE(r_capped.ok);
    auto r_ok = demux(bytes, /*max_payload_bytes=*/bytes.size());
    REQUIRE(r_ok.ok);
}

// --------------------------------------------------------------------------
// S11: rapidcheck portable fuzz — demux on arbitrary bytes never crashes.
// (libFuzzer harness in codec/fuzz/fuzz_sei.cpp covers coverage-guided
//  fuzzing under Clang.)
// --------------------------------------------------------------------------

TEST_CASE("sei_prop_demux_random_bytes_no_crash", "[sei][property][fuzz]") {
    rc::prop("demux on arbitrary bytes does not throw, abort, or read past end",
             [](const std::vector<uint8_t>& bytes) {
                 // The cap protects us if rapidcheck generates a huge input.
                 auto r = demux(byte_span(bytes), /*max=*/1u << 20);
                 (void)r;   // any outcome is acceptable; only crashing is not.
             });
}

// --------------------------------------------------------------------------
// S12: rapidcheck round-trip property.
// --------------------------------------------------------------------------

TEST_CASE("sei_prop_roundtrip", "[sei][property]") {
    rc::prop("demux(mux(x)) == x for arbitrary valid payloads",
             [](uint16_t w, uint16_t h, uint8_t bd_raw, uint8_t mf,
                uint8_t k_raw,
                const std::vector<uint8_t>& rt_raw,
                const std::vector<uint8_t>& rb_raw,
                const std::vector<uint8_t>& rr_raw,
                const std::vector<uint8_t>& qd_raw,
                const std::vector<int16_t>& top_raw) {
                 if (w == 0) w = 1;
                 if (h == 0) h = 1;
                 const uint8_t bd = (bd_raw % 3 == 0) ? 8 :
                                    (bd_raw % 2 == 0) ? 12 : 16;
                 const uint8_t k  = static_cast<uint8_t>(2 + (k_raw % 15));
                 std::array<int16_t, 16> top{};
                 for (uint8_t i = 0; i < k; ++i) {
                     top[i] = top_raw.empty() ? int16_t{0}
                                              : top_raw[i % top_raw.size()];
                 }
                 // Cap stream lengths so the test stays fast.
                 auto cap = [](const std::vector<uint8_t>& v) {
                     std::vector<uint8_t> c(v.begin(),
                                            v.begin() + std::min<std::size_t>(v.size(), 4096));
                     return c;
                 };
                 const auto rt = cap(rt_raw);
                 const auto rb = cap(rb_raw);
                 const auto rr = cap(rr_raw);
                 const auto qd = cap(qd_raw);
                 FrameHeader hdr{w, h, bd, mf};
                 auto bytes = mux(hdr, top, k, rt, rb, rr, qd);
                 RC_ASSERT(!bytes.empty());
                 auto r = demux(bytes);
                 RC_ASSERT(r.ok);
                 RC_ASSERT(r.hdr.width      == hdr.width);
                 RC_ASSERT(r.hdr.height     == hdr.height);
                 RC_ASSERT(r.hdr.bit_depth  == hdr.bit_depth);
                 RC_ASSERT(r.hdr.mode_flags == hdr.mode_flags);
                 RC_ASSERT(r.top_count == k);
                 for (uint8_t i = 0; i < k; ++i) RC_ASSERT(r.top[i] == top[i]);
                 RC_ASSERT(r.rans_top      == rt);
                 RC_ASSERT(r.rans_b        == rb);
                 RC_ASSERT(r.rans_r        == rr);
                 RC_ASSERT(r.quadtree_desc == qd);
             });
}

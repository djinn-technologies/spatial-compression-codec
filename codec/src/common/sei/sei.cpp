// codec/src/common/sei/sei.cpp
//
// SCC SEI muxer / demuxer + NAL escape utilities.
//
// References: see file header in codec/include/scc/sei.hpp.
// Evidence tags: [REQ-013], [REQ-014], [ISO/IEC 14496-10 §D.1.6, §7.4.1.1].

#include "scc/sei.hpp"
#include "scc/detail/sei_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace scc::sei {

namespace detail {

// ---------------------------------------------------------------------------
// Big-endian serialisation helpers (platform-independent byte arithmetic;
// produces the same bytes regardless of host endianness).
// ---------------------------------------------------------------------------

void write_u16_be(std::uint8_t* dst, std::uint16_t v) noexcept {
    dst[0] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    dst[1] = static_cast<std::uint8_t>( v       & 0xFFu);
}

void write_u32_be(std::uint8_t* dst, std::uint32_t v) noexcept {
    dst[0] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
    dst[1] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    dst[2] = static_cast<std::uint8_t>((v >>  8) & 0xFFu);
    dst[3] = static_cast<std::uint8_t>( v        & 0xFFu);
}

std::uint16_t read_u16_be(const std::uint8_t* src) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(src[0]) << 8) |
         static_cast<std::uint16_t>(src[1]));
}

std::uint32_t read_u32_be(const std::uint8_t* src) noexcept {
    return (static_cast<std::uint32_t>(src[0]) << 24) |
           (static_cast<std::uint32_t>(src[1]) << 16) |
           (static_cast<std::uint32_t>(src[2]) <<  8) |
            static_cast<std::uint32_t>(src[3]);
}

// ---------------------------------------------------------------------------
// NAL emulation prevention (ISO/IEC 14496-10 §7.4.1.1)
//
// RBSP -> EBSP: insert 0x03 between every "0x00 0x00" and a following byte
// in {0x00, 0x01, 0x02, 0x03}. Single pass with a "zeros run" counter.
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> nal_escape(byte_span rbsp) {
    std::vector<std::uint8_t> out;
    // Pessimistic upper bound on growth: every 2 bytes could in principle
    // trigger one escape. Reserve 1.5x to keep one realloc on typical input.
    out.reserve(rbsp.size() + rbsp.size() / 2 + 4);
    int zeros = 0;
    for (std::size_t i = 0; i < rbsp.size(); ++i) {
        const std::uint8_t b = rbsp[i];
        if (zeros >= 2 && b <= 0x03) {
            out.push_back(0x03);
            zeros = 0;
        }
        out.push_back(b);
        if (b == 0x00) ++zeros;
        else            zeros = 0;
    }
    return out;
}

std::vector<std::uint8_t> nal_unescape(byte_span ebsp) {
    std::vector<std::uint8_t> out;
    out.reserve(ebsp.size());
    int zeros = 0;
    for (std::size_t i = 0; i < ebsp.size(); ++i) {
        const std::uint8_t b = ebsp[i];
        if (zeros >= 2 && b == 0x03) {
            // Drop emulation_prevention_three_byte; do NOT emit. Reset
            // zeros counter so the *next* byte starts a fresh run.
            zeros = 0;
            continue;
        }
        out.push_back(b);
        if (b == 0x00) ++zeros;
        else            zeros = 0;
    }
    return out;
}

} // namespace detail

namespace {

constexpr std::size_t kHeaderFixedBytes =
    16 +   // UUID
    1 + 1 +    // version_major, version_minor
    2 + 2 +    // width, height
    1 +        // bit_depth
    1 +        // mode_flags
    1;         // top_count

bool is_valid_bit_depth(std::uint8_t bd) noexcept {
    return bd == 8 || bd == 12 || bd == 16;
}

} // namespace

// ---------------------------------------------------------------------------
// mux  [REQ-013]
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> mux(const FrameHeader&                  hdr,
                              const std::array<std::int16_t, 16>& top,
                              std::uint8_t                        top_count,
                              byte_span                           rans_top,
                              byte_span                           rans_b,
                              byte_span                           rans_r,
                              byte_span                           quadtree_desc) {
    // Contract validation. Empty vector = "rejected".
    if (top_count < 2 || top_count > 16) return {};
    if (!is_valid_bit_depth(hdr.bit_depth)) return {};

    // Plausibility check on the per-stream sizes against the cap (using the
    // default cap; the cap is only enforced strictly on the demux side).
    const std::size_t streams_size = rans_top.size() + rans_b.size()
                                    + rans_r.size() + quadtree_desc.size();
    if (streams_size > kDefaultMaxPayloadBytes) return {};

    // Build RBSP.
    const std::size_t top_bytes = 2u * static_cast<std::size_t>(top_count);
    const std::size_t rbsp_size =
        kHeaderFixedBytes + top_bytes
        + 4 + rans_top.size()
        + 4 + rans_b.size()
        + 4 + rans_r.size()
        + quadtree_desc.size();

    std::vector<std::uint8_t> rbsp(rbsp_size);
    std::uint8_t* p = rbsp.data();

    // [REQ-013] UUID.
    std::memcpy(p, kSccSeiUuid.data(), 16);
    p += 16;

    // Version.
    *p++ = kVersionMajor;
    *p++ = kVersionMinor;

    // FrameHeader.
    detail::write_u16_be(p, hdr.width);  p += 2;
    detail::write_u16_be(p, hdr.height); p += 2;
    *p++ = hdr.bit_depth;
    *p++ = hdr.mode_flags;

    // top_count + top values.
    *p++ = top_count;
    for (std::uint8_t i = 0; i < top_count; ++i) {
        detail::write_u16_be(p, static_cast<std::uint16_t>(top[i]));
        p += 2;
    }

    // rans_top.
    detail::write_u32_be(p, static_cast<std::uint32_t>(rans_top.size()));
    p += 4;
    if (!rans_top.empty()) {
        std::memcpy(p, rans_top.data(), rans_top.size());
        p += rans_top.size();
    }

    // rans_b.
    detail::write_u32_be(p, static_cast<std::uint32_t>(rans_b.size()));
    p += 4;
    if (!rans_b.empty()) {
        std::memcpy(p, rans_b.data(), rans_b.size());
        p += rans_b.size();
    }

    // rans_r.
    detail::write_u32_be(p, static_cast<std::uint32_t>(rans_r.size()));
    p += 4;
    if (!rans_r.empty()) {
        std::memcpy(p, rans_r.data(), rans_r.size());
        p += rans_r.size();
    }

    // quadtree_desc -- consumes the rest of the payload (no length prefix).
    if (!quadtree_desc.empty()) {
        std::memcpy(p, quadtree_desc.data(), quadtree_desc.size());
        p += quadtree_desc.size();
    }

    // ISO/IEC 14496-10 §7.4.1.1 emulation prevention.
    return detail::nal_escape(byte_span(rbsp.data(), rbsp.size()));
}

// ---------------------------------------------------------------------------
// demux  [REQ-014]
// ---------------------------------------------------------------------------
//
// Validation order (Ultrathink #2):
//   (1) Cap the input size BEFORE de-escape (de-escape's allocation is
//       bounded by the input size).
//   (2) De-escape into RBSP. Reject if the result somehow exceeds the cap.
//   (3) Walk RBSP, validate every length field against `bytes remaining`
//       AND `max_payload_bytes` BEFORE allocating the corresponding output
//       vector. The `assign(p, p + len)` is the allocation; bound checks
//       precede it unconditionally.

DemuxResult demux(byte_span sei_payload, std::size_t max_payload_bytes) {
    DemuxResult r;

    // (1)
    if (sei_payload.size() > max_payload_bytes) return r;

    // (2)
    auto rbsp = detail::nal_unescape(sei_payload);
    if (rbsp.size() > max_payload_bytes) return r;

    const std::uint8_t* const begin = rbsp.data();
    const std::uint8_t* const end   = begin + rbsp.size();
    const std::uint8_t*       p     = begin;

    auto remaining = [&]() -> std::size_t {
        return static_cast<std::size_t>(end - p);
    };

    // UUID.
    if (remaining() < 16) return r;
    if (std::memcmp(p, kSccSeiUuid.data(), 16) != 0) return r;
    p += 16;

    // Version.
    if (remaining() < 2) return r;
    const std::uint8_t version_major = *p++;
    const std::uint8_t version_minor = *p++;
    if (version_major != kVersionMajor) return r;
    (void)version_minor; // forward-compat: minor bumps are accepted on read.

    // FrameHeader.
    if (remaining() < 4 + 1 + 1 + 1) return r;
    r.hdr.width      = detail::read_u16_be(p); p += 2;
    r.hdr.height     = detail::read_u16_be(p); p += 2;
    r.hdr.bit_depth  = *p++;
    r.hdr.mode_flags = *p++;
    if (!is_valid_bit_depth(r.hdr.bit_depth)) return r;

    // top_count.
    r.top_count = *p++;
    if (r.top_count < 2 || r.top_count > 16) return r;

    // top values.
    const std::size_t top_bytes = 2u * static_cast<std::size_t>(r.top_count);
    if (remaining() < top_bytes) return r;
    r.top.fill(0);
    for (std::uint8_t i = 0; i < r.top_count; ++i) {
        r.top[i] = static_cast<std::int16_t>(detail::read_u16_be(p));
        p += 2;
    }

    // Three length-prefixed streams.
    auto read_stream = [&](std::vector<std::uint8_t>& out) -> bool {
        if (remaining() < 4) return false;
        const std::uint32_t len = detail::read_u32_be(p);
        p += 4;
        if (len > max_payload_bytes) return false;          // (3) cap check
        if (remaining() < len) return false;                // (3) bounds check
        out.assign(p, p + len);                              // <- allocation
        p += len;
        return true;
    };

    if (!read_stream(r.rans_top)) return r;
    if (!read_stream(r.rans_b))   return r;
    if (!read_stream(r.rans_r))   return r;

    // quadtree_desc consumes the rest of the buffer.
    r.quadtree_desc.assign(p, end);

    r.ok = true;
    return r;
}

} // namespace scc::sei

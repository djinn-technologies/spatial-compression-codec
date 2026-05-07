// codec/include/scc/sei.hpp
//
// SCC SEI muxer / demuxer.
//
// Packages the compressed depth payload (TOP / B / R rANS streams + the
// quad-tree region descriptor) as the body of an H.264
// `user_data_unregistered` SEI message. The output of `mux` is already
// emulation-prevention-escaped (RBSP -> EBSP per ISO/IEC 14496-10
// §7.4.1.1) and is ready to be wrapped in a NAL unit by the caller
// (e.g. libavcodec).
//
// References / evidence tags:
//   [REQ-013]                       SCC SEI payload format
//   [REQ-014]                       Round-trip mux/demux
//   [ISO/IEC 14496-10 §D.1.6]       user_data_unregistered SEI message
//                                   https://www.itu.int/rec/T-REC-H.264
//   [ISO/IEC 14496-10 §7.4.1.1]     emulation_prevention_three_byte rule
//
// Wire format (big-endian for multi-byte fields)
// ---------------------------------------------
//   [16 bytes]   SCC UUID                 -- canonical UUIDv4, see ADR-005
//   [u8]         version_major  (= 1)
//   [u8]         version_minor  (= 0)
//   [u16]        width
//   [u16]        height
//   [u8]         bit_depth                -- {8, 12, 16}
//   [u8]         mode_flags               -- bit0 = lossless, bit1 = motion-on, ...
//   [u8]         top_count                -- [2, 16]
//   [int16 x N]  top values, BE           -- N == top_count, sign-extended on read
//   [u32]        rans_top_len
//   [bytes]      rans_top
//   [u32]        rans_b_len
//   [bytes]      rans_b
//   [u32]        rans_r_len
//   [bytes]      rans_r
//   [bytes]      quadtree_descriptor      -- consumes the rest of the payload
//
// The above is the RBSP (raw byte sequence payload). The muxer additionally
// performs the §7.4.1.1 emulation_prevention transformation:
//
//     for any 4 consecutive RBSP bytes  B0 B1 B2 B3  with
//     B0 == 0x00, B1 == 0x00, B2 in {0x00, 0x01, 0x02, 0x03}:
//         insert 0x03 between B1 and B2.
//
// The demuxer reverses this before parsing (and validates every length
// field before allocating any output buffer; an oversized or truncated
// input never causes a crash and never allocates beyond
// `max_payload_bytes`).
//
// See also: codec/src/common/sei/README.md (annotated hex example),
// docs/adr/ADR-005-sei-uuid-and-wire-format.md.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace scc::sei {

// ---------------------------------------------------------------------------
// byte_span
//
// Minimal stand-in for std::span<const uint8_t> until the codebase migrates
// to C++20. Constructible from std::vector<uint8_t>, std::array<uint8_t, N>,
// raw (ptr, size). Non-owning, no lifetime extension.
// ---------------------------------------------------------------------------

class byte_span {
public:
    using value_type     = std::uint8_t;
    using const_pointer  = const std::uint8_t*;
    using const_iterator = const std::uint8_t*;
    using size_type      = std::size_t;

    constexpr byte_span() noexcept = default;
    constexpr byte_span(const std::uint8_t* data, size_type size) noexcept
        : data_(data), size_(size) {}
    byte_span(const std::vector<std::uint8_t>& v) noexcept
        : data_(v.data()), size_(v.size()) {}
    template <std::size_t N>
    constexpr byte_span(const std::array<std::uint8_t, N>& a) noexcept
        : data_(a.data()), size_(N) {}

    constexpr const_pointer  data()  const noexcept { return data_; }
    constexpr size_type      size()  const noexcept { return size_; }
    constexpr bool           empty() const noexcept { return size_ == 0; }
    constexpr const std::uint8_t& operator[](size_type i) const noexcept { return data_[i]; }
    constexpr const_iterator begin() const noexcept { return data_; }
    constexpr const_iterator end()   const noexcept { return data_ + size_; }

private:
    const std::uint8_t* data_ = nullptr;
    size_type           size_ = 0;
};

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

inline constexpr std::uint8_t kVersionMajor = 1;
inline constexpr std::uint8_t kVersionMinor = 0;

// SCC SEI UUIDv4 -- registered in ADR-005, MUST NOT change without bumping
// the wire format version.
inline constexpr std::array<std::uint8_t, 16> kSccSeiUuid = {
    0x5c, 0xc1, 0xd4, 0xe7, 0x7e, 0x93, 0x4b, 0x51,
    0x9d, 0x3c, 0xfa, 0x20, 0xb8, 0x16, 0x7a, 0x45,
};

inline constexpr std::size_t kDefaultMaxPayloadBytes = 256ull * 1024 * 1024;

// Mode-flag bits (callers set in FrameHeader::mode_flags).
inline constexpr std::uint8_t kModeFlagLossless = 0x01;
inline constexpr std::uint8_t kModeFlagMotionOn = 0x02;

// ---------------------------------------------------------------------------
// FrameHeader / DemuxResult
// ---------------------------------------------------------------------------

struct FrameHeader {
    std::uint16_t width      = 0;
    std::uint16_t height     = 0;
    std::uint8_t  bit_depth  = 0;   // {8, 12, 16}
    std::uint8_t  mode_flags = 0;
};

struct DemuxResult {
    bool                         ok = false;
    FrameHeader                  hdr{};
    std::array<std::int16_t, 16> top{};
    std::uint8_t                 top_count = 0;
    std::vector<std::uint8_t>    rans_top;
    std::vector<std::uint8_t>    rans_b;
    std::vector<std::uint8_t>    rans_r;
    std::vector<std::uint8_t>    quadtree_desc;
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// [REQ-013] Mux. Returns an empty vector if any contract is violated:
//     - hdr.bit_depth not in {8, 12, 16}
//     - top_count not in [2, 16]
// (No exception: the codec layer prefers result-based reporting.)
//
// The output is already NAL-emulation-prevention-escaped (§7.4.1.1) and is
// the body of a `user_data_unregistered` SEI message ready for the H.264
// muxer to wrap in a NAL unit.
std::vector<std::uint8_t> mux(const FrameHeader&                  hdr,
                              const std::array<std::int16_t, 16>& top,
                              std::uint8_t                        top_count,
                              byte_span                           rans_top,
                              byte_span                           rans_b,
                              byte_span                           rans_r,
                              byte_span                           quadtree_desc);

// [REQ-014] Demux. Validates the input against the wire format and the
// `max_payload_bytes` ceiling BEFORE allocating any output buffer. A
// malformed input produces `DemuxResult{ok = false}` and never throws or
// crashes. See ADR-005 §"Validation order".
DemuxResult demux(byte_span    sei_payload,
                  std::size_t  max_payload_bytes = kDefaultMaxPayloadBytes);

} // namespace scc::sei

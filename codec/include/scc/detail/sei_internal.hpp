// codec/include/scc/detail/sei_internal.hpp
//
// Internal — exposes NAL escape/unescape and the BE serialisation helpers
// for direct testing. NOT a stable public API; not installed.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "scc/sei.hpp"

namespace scc::sei::detail {

// Encode RBSP -> EBSP per §7.4.1.1. Inserts 0x03 between every "0x00 0x00"
// and a following byte in {0x00, 0x01, 0x02, 0x03}.
std::vector<std::uint8_t> nal_escape(byte_span rbsp);

// Decode EBSP -> RBSP. Output is at most input.size() bytes (no growth).
std::vector<std::uint8_t> nal_unescape(byte_span ebsp);

// Big-endian helpers exposed for tests; production code uses these inline.
void          write_u16_be(std::uint8_t* dst, std::uint16_t v) noexcept;
void          write_u32_be(std::uint8_t* dst, std::uint32_t v) noexcept;
std::uint16_t read_u16_be (const std::uint8_t* src) noexcept;
std::uint32_t read_u32_be (const std::uint8_t* src) noexcept;

} // namespace scc::sei::detail

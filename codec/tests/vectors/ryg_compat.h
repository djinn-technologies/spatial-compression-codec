// codec/tests/vectors/ryg_compat.h
//
// Glue layer: drives the ryg-reference rANS encoders/decoders with the SCC
// probability tables so unit tests can compare the production encoder's
// output byte-for-byte against an independent reference.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "scc/rans.hpp"

namespace ryg_compat {

// Encode `data[0..n)` with the rans-byte reference using the same SCC
// 12-bit probability scale. Returns the bytestream in forward decode order.
std::vector<uint8_t> ryg_encode8(const uint8_t* data, std::size_t n,
                                 const scc::rans::ProbTable8& tab);

// Decode a stream produced by ryg_encode8 (or scc::rans::Encoder8).
std::vector<uint8_t> ryg_decode8(const uint8_t* enc, std::size_t n_enc,
                                 std::size_t n_symbols,
                                 const scc::rans::ProbTable8& tab);

// rans-word reference (16-bit chunks).
std::vector<uint8_t> ryg_encode16(const uint16_t* data, std::size_t n,
                                  const scc::rans::ProbTable16& tab);

std::vector<uint16_t> ryg_decode16(const uint8_t* enc, std::size_t n_enc,
                                   std::size_t n_symbols,
                                   const scc::rans::ProbTable16& tab);

} // namespace ryg_compat

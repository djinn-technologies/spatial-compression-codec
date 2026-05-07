// codec/tests/vectors/ryg_rans_byte.h
//
// Reference rANS implementation with 32-bit accumulator and 8-bit (byte)
// renormalisation chunks. API and constant choices follow Fabian Giesen's
// public-domain ryg_rans (rans_byte.h):
//
//   https://github.com/rygorous/ryg_rans
//
// This file is an independent re-implementation of that algorithm written
// from Duda 2014 §3.3 ("Asymmetric Numeral Systems", arXiv:1311.2540).
// Its sole purpose is to provide a byte-equality oracle for the SCC rANS
// encoder/decoder under test (Ultrathink Block #2 of AI prompt #1).
//
// The algorithm and renormalisation thresholds are deliberately byte-
// for-byte compatible with ryg's reference. Distinct buffer-orientation
// idiom (backward writes via `*--ptr`) vs. our production encoder's
// forward-push-then-reverse — two independent bug surfaces.
//
// Public-domain dedication: this file (like the algorithm it follows) may
// be used freely. Attribution to Fabian Giesen for the original design.

#pragma once

#include <cassert>
#include <cstdint>

namespace ryg_ref {

using RansState = uint32_t;

// Renormalisation floor. State is always in [RANS_BYTE_L, b * RANS_BYTE_L)
// = [2^23, 2^31) outside the renorm loop body. b = 256.
constexpr uint32_t RANS_BYTE_L = 1u << 23;

// --- Encoder --------------------------------------------------------------

inline void RansEncInit(RansState* r) {
    *r = RANS_BYTE_L;
}

// Encode one symbol with cumulative `start` and frequency `freq` under a
// probability scale of (1 << scale_bits). For SCC, scale_bits = 12.
//
// `pptr` points at the *current write position* (one past the next byte to
// write). Bytes are written backward into the caller-owned buffer.
inline void RansEncPut(RansState* r,
                       uint8_t**   pptr,
                       uint32_t    start,
                       uint32_t    freq,
                       uint32_t    scale_bits) {
    assert(freq != 0);
    // Single-symbol alphabet: C(s, x) is the identity; skip to avoid the
    // overflow trap in x_max when freq == (1 << scale_bits).
    if (freq == (1u << scale_bits)) return;
    uint32_t       x     = *r;
    const uint32_t x_max = ((RANS_BYTE_L >> scale_bits) << 8) * freq;
    if (x >= x_max) {
        uint8_t* ptr = *pptr;
        do {
            *--ptr = static_cast<uint8_t>(x & 0xFFu);
            x >>= 8;
        } while (x >= x_max);
        *pptr = ptr;
    }
    *r = ((x / freq) << scale_bits) + (x % freq) + start;
}

// Flush state into 4 bytes (little-endian: low byte ends up at the lowest
// address, i.e. the first byte the decoder reads).
inline void RansEncFlush(RansState* r, uint8_t** pptr) {
    const uint32_t x   = *r;
    uint8_t*       ptr = *pptr;
    *--ptr = static_cast<uint8_t>((x >> 24) & 0xFFu);
    *--ptr = static_cast<uint8_t>((x >> 16) & 0xFFu);
    *--ptr = static_cast<uint8_t>((x >>  8) & 0xFFu);
    *--ptr = static_cast<uint8_t>( x        & 0xFFu);
    *pptr  = ptr;
}

// --- Decoder --------------------------------------------------------------

inline void RansDecInit(RansState* r, uint8_t** pptr) {
    uint8_t* ptr = *pptr;
    uint32_t x   =  static_cast<uint32_t>(ptr[0])
                 | (static_cast<uint32_t>(ptr[1]) <<  8)
                 | (static_cast<uint32_t>(ptr[2]) << 16)
                 | (static_cast<uint32_t>(ptr[3]) << 24);
    *r    = x;
    *pptr = ptr + 4;
}

inline uint32_t RansDecGet(const RansState* r, uint32_t scale_bits) {
    return *r & ((1u << scale_bits) - 1u);
}

inline void RansDecAdvance(RansState* r,
                           uint8_t**  pptr,
                           uint32_t   start,
                           uint32_t   freq,
                           uint32_t   scale_bits) {
    const uint32_t mask = (1u << scale_bits) - 1u;
    uint32_t       x    = *r;
    x = freq * (x >> scale_bits) + (x & mask) - start;
    if (x < RANS_BYTE_L) {
        uint8_t* ptr = *pptr;
        do {
            x = (x << 8) | static_cast<uint32_t>(*ptr++);
        } while (x < RANS_BYTE_L);
        *pptr = ptr;
    }
    *r = x;
}

} // namespace ryg_ref

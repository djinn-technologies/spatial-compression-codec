// codec/tests/vectors/ryg_rans_word.h
//
// Reference rANS implementation with 32-bit accumulator and 16-bit
// renormalisation chunks. API and constant choices follow Fabian Giesen's
// public-domain ryg_rans (rans_word.h / rans_word_sse41.h scalar variant).
//
// Independent re-implementation from Duda 2014 §3.3 ("Asymmetric Numeral
// Systems", arXiv:1311.2540), written for use as a byte-equality oracle
// against the SCC rANS-16 encoder/decoder (Ultrathink Block #2 of AI
// prompt #1).
//
// Buffer-orientation idiom: backward writes via `*--ptr16` of two-byte
// chunks. The wire byte order is little-endian (low byte at lower
// address) -- matching ryg, and matching the SCC production encoder.
//
// Public-domain dedication; attribution to Fabian Giesen for the design.

#pragma once

#include <cassert>
#include <cstdint>

namespace ryg_ref {

using RansWordState = uint32_t;

// Renormalisation floor for the word-sized variant.
// State is in [RANS_WORD_L, b * RANS_WORD_L) = [2^16, 2^32) outside the
// renorm loop body. b = 65536.
constexpr uint32_t RANS_WORD_L = 1u << 16;

// --- Encoder --------------------------------------------------------------

inline void RansWordEncInit(RansWordState* r) {
    *r = RANS_WORD_L;
}

// Encode one symbol. `pptr` is a uint8_t** pointing at the current backward
// write head; writes proceed in 16-bit chunks (two bytes, little-endian).
inline void RansWordEncPut(RansWordState* r,
                           uint8_t**      pptr,
                           uint32_t       start,
                           uint32_t       freq,
                           uint32_t       scale_bits) {
    assert(freq != 0);
    if (freq == (1u << scale_bits)) return;       // single-symbol no-op
    uint32_t       x     = *r;
    const uint32_t x_max = ((RANS_WORD_L >> scale_bits) << 16) * freq;
    if (x >= x_max) {
        uint8_t* ptr = *pptr;
        do {
            // Write LO at higher address (it becomes the LATER byte in the
            // forward decode stream because we're walking backward).
            // Wait -- we're walking backward, so the byte at the LOWER
            // address is read FIRST by the decoder. We want the decoder
            // to read LO first, HI second (LE 16-bit), so LO must be at
            // the lower address: write HI first (decrements ptr; HI sits
            // at higher address) and LO last (decrements ptr again; LO
            // sits at lower address).
            *--ptr = static_cast<uint8_t>((x >> 8) & 0xFFu);   // HI -- higher addr
            *--ptr = static_cast<uint8_t>( x       & 0xFFu);   // LO -- lower addr (read first)
            x >>= 16;
        } while (x >= x_max);
        *pptr = ptr;
    }
    *r = ((x / freq) << scale_bits) + (x % freq) + start;
}

// Flush state into 4 bytes, low byte at lowest address.
inline void RansWordEncFlush(RansWordState* r, uint8_t** pptr) {
    const uint32_t x   = *r;
    uint8_t*       ptr = *pptr;
    *--ptr = static_cast<uint8_t>((x >> 24) & 0xFFu);
    *--ptr = static_cast<uint8_t>((x >> 16) & 0xFFu);
    *--ptr = static_cast<uint8_t>((x >>  8) & 0xFFu);
    *--ptr = static_cast<uint8_t>( x        & 0xFFu);
    *pptr  = ptr;
}

// --- Decoder --------------------------------------------------------------

inline void RansWordDecInit(RansWordState* r, uint8_t** pptr) {
    uint8_t* ptr = *pptr;
    uint32_t x   =  static_cast<uint32_t>(ptr[0])
                 | (static_cast<uint32_t>(ptr[1]) <<  8)
                 | (static_cast<uint32_t>(ptr[2]) << 16)
                 | (static_cast<uint32_t>(ptr[3]) << 24);
    *r    = x;
    *pptr = ptr + 4;
}

inline uint32_t RansWordDecGet(const RansWordState* r, uint32_t scale_bits) {
    return *r & ((1u << scale_bits) - 1u);
}

inline void RansWordDecAdvance(RansWordState* r,
                               uint8_t**      pptr,
                               uint32_t       start,
                               uint32_t       freq,
                               uint32_t       scale_bits) {
    const uint32_t mask = (1u << scale_bits) - 1u;
    uint32_t       x    = *r;
    x = freq * (x >> scale_bits) + (x & mask) - start;
    if (x < RANS_WORD_L) {
        uint8_t* ptr = *pptr;
        do {
            const uint32_t lo = *ptr++;
            const uint32_t hi = *ptr++;
            x = (x << 16) | (lo | (hi << 8));
        } while (x < RANS_WORD_L);
        *pptr = ptr;
    }
    *r = x;
}

} // namespace ryg_ref

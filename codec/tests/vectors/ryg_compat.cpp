// codec/tests/vectors/ryg_compat.cpp
//
// Implementation of the ryg-reference glue. The reference encoders write
// backward into a generously-sized scratch buffer and return only the
// occupied tail.

#include "ryg_compat.h"

#include "ryg_rans_byte.h"
#include "ryg_rans_word.h"

#include <cstring>
#include <stdexcept>

namespace ryg_compat {

namespace {

constexpr uint32_t kScaleBits = 12;  // matches scc::rans::kProbBits

// Worst-case bytes per symbol: 12 bits = 1.5 bytes. Add a generous head for
// the 4-byte flush plus rounding. 4 bytes per symbol is plenty for any input.
constexpr std::size_t kBytesPerSymbol8  = 4;
constexpr std::size_t kBytesPerSymbol16 = 6;
constexpr std::size_t kBaseHead         = 64;

} // namespace

std::vector<uint8_t>
ryg_encode8(const uint8_t* data, std::size_t n, const scc::rans::ProbTable8& tab) {
    const std::size_t cap = kBaseHead + n * kBytesPerSymbol8;
    std::vector<uint8_t> scratch(cap, 0);
    uint8_t* end = scratch.data() + cap;
    uint8_t* ptr = end;

    ryg_ref::RansState x;
    ryg_ref::RansEncInit(&x);
    // Encode in reverse: rANS is a stack.
    for (std::size_t i = n; i-- > 0;) {
        const uint8_t  s     = data[i];
        const uint32_t freq  = tab.freqs[s];
        const uint32_t start = tab.cum[s];
        if (freq == 0) throw std::invalid_argument("ryg_encode8: zero-prob symbol");
        ryg_ref::RansEncPut(&x, &ptr, start, freq, kScaleBits);
    }
    ryg_ref::RansEncFlush(&x, &ptr);

    return std::vector<uint8_t>(ptr, end);
}

std::vector<uint8_t>
ryg_decode8(const uint8_t* enc, std::size_t n_enc, std::size_t n_symbols,
            const scc::rans::ProbTable8& tab) {
    std::vector<uint8_t> mutable_buf(enc, enc + n_enc);
    uint8_t* ptr = mutable_buf.data();

    ryg_ref::RansState x;
    ryg_ref::RansDecInit(&x, &ptr);

    std::vector<uint8_t> out(n_symbols);
    for (std::size_t i = 0; i < n_symbols; ++i) {
        const uint32_t slot = ryg_ref::RansDecGet(&x, kScaleBits);
        const uint8_t  s    = tab.slot_to_symbol[slot];
        const uint32_t freq = tab.freqs[s];
        const uint32_t cum  = tab.cum[s];
        out[i] = s;
        ryg_ref::RansDecAdvance(&x, &ptr, cum, freq, kScaleBits);
    }
    return out;
}

std::vector<uint8_t>
ryg_encode16(const uint16_t* data, std::size_t n, const scc::rans::ProbTable16& tab) {
    const std::size_t cap = kBaseHead + n * kBytesPerSymbol16;
    std::vector<uint8_t> scratch(cap, 0);
    uint8_t* end = scratch.data() + cap;
    uint8_t* ptr = end;

    ryg_ref::RansWordState x;
    ryg_ref::RansWordEncInit(&x);
    for (std::size_t i = n; i-- > 0;) {
        const uint16_t s     = data[i];
        const uint32_t freq  = tab.freqs[s];
        const uint32_t start = tab.cum[s];
        if (freq == 0) throw std::invalid_argument("ryg_encode16: zero-prob symbol");
        ryg_ref::RansWordEncPut(&x, &ptr, start, freq, kScaleBits);
    }
    ryg_ref::RansWordEncFlush(&x, &ptr);

    return std::vector<uint8_t>(ptr, end);
}

std::vector<uint16_t>
ryg_decode16(const uint8_t* enc, std::size_t n_enc, std::size_t n_symbols,
             const scc::rans::ProbTable16& tab) {
    std::vector<uint8_t> mutable_buf(enc, enc + n_enc);
    uint8_t* ptr = mutable_buf.data();

    ryg_ref::RansWordState x;
    ryg_ref::RansWordDecInit(&x, &ptr);

    std::vector<uint16_t> out(n_symbols);
    for (std::size_t i = 0; i < n_symbols; ++i) {
        const uint32_t slot = ryg_ref::RansWordDecGet(&x, kScaleBits);
        const uint16_t s    = tab.slot_to_symbol[slot];
        const uint32_t freq = tab.freqs[s];
        const uint32_t cum  = tab.cum[s];
        out[i] = s;
        ryg_ref::RansWordDecAdvance(&x, &ptr, cum, freq, kScaleBits);
    }
    return out;
}

} // namespace ryg_compat

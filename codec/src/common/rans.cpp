// codec/src/common/rans.cpp
//
// rANS entropy coder -- out-of-line implementations.
//
// Hot paths (put / get / renormalisation) live as inline definitions in
// codec/include/scc/rans.hpp so the optimiser can inline through the
// OutIt template instantiations and across translation units. Cold paths
// (probability-table quantiser, ctor bodies, free-function wrappers) live
// here.
//
// References: see file header in codec/include/scc/rans.hpp.
//   [Duda 2014]; [ryg_rans]; [US10827161B2 col. 7-8]; [REQ-009]; [REQ-010].

#include "scc/rans.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace scc::rans {

namespace {

// Scale raw `counts` of length `n` into `freqs` summing to kProbScale, with
// the invariant `counts[s] > 0  =>  freqs[s] >= 1`. Drift correction adjusts
// the largest non-zero frequency without ever letting it drop below 1.
//
// O(n) for the scale pass plus O(n * |drift|) for the correction. Drift is
// bounded by n in the worst case, so total worst case O(n^2). For the
// alphabets the SCC codec actually uses (TOP <= 4 symbols, B = 2 symbols,
// R typically <= a few thousand), this is far cheaper than the encode pass
// it precedes.
template <class FreqT>
void scale_counts_to_4096(const uint32_t* counts, std::size_t n, FreqT* freqs) {
    uint64_t total = 0;
    for (std::size_t i = 0; i < n; ++i) total += counts[i];

    if (total == 0) {
        // Defensive fallback. Caller should not encode with such a table.
        if (n > 0) {
            std::fill_n(freqs, n, FreqT{0});
            freqs[0] = static_cast<FreqT>(kProbScale);
        }
        return;
    }

    // Initial scale: freq = round(counts * M / total), but >= 1 for any
    // present symbol.
    uint32_t sum = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (counts[i] == 0) {
            freqs[i] = 0;
            continue;
        }
        const uint64_t numer = static_cast<uint64_t>(counts[i]) * kProbScale;
        // Round-to-nearest, ties-to-even-ish (we just bias by half).
        uint64_t f = (numer + total / 2) / total;
        if (f == 0) f = 1;                                // present-symbol floor
        if (f > kProbScale - (n - 1)) {
            // Would leave no room for other present symbols -- clamp later
            // via drift correction. Allow the temporary overshoot here.
        }
        freqs[i] = static_cast<FreqT>(f);
        sum += static_cast<uint32_t>(f);
    }

    // Drift correction: nudge the largest-freq present symbol by +/- 1 until
    // sum == kProbScale. Never reduce a present-symbol freq below 1.
    while (sum != kProbScale) {
        // Find argmax over present symbols.
        std::size_t arg = 0;
        uint32_t    best = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (freqs[i] > best) {
                best = freqs[i];
                arg  = i;
            }
        }
        if (sum > kProbScale) {
            // Decrement -- but if best == 1 we cannot decrement that symbol;
            // pick the next-largest > 1.
            if (best <= 1) {
                std::size_t arg2 = n;
                uint32_t    best2 = 0;
                for (std::size_t i = 0; i < n; ++i) {
                    if (freqs[i] > 1 && freqs[i] > best2) {
                        best2 = freqs[i];
                        arg2  = i;
                    }
                }
                assert(arg2 != n && "drift correction has no room to shrink");
                freqs[arg2] = static_cast<FreqT>(freqs[arg2] - 1);
            } else {
                freqs[arg] = static_cast<FreqT>(freqs[arg] - 1);
            }
            --sum;
        } else {
            freqs[arg] = static_cast<FreqT>(freqs[arg] + 1);
            ++sum;
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// ProbTable8
// ---------------------------------------------------------------------------

ProbTable8 ProbTable8::from_counts(const uint32_t* counts, std::size_t n) {
    if (n > 256) throw std::invalid_argument("ProbTable8: alphabet > 256");
    ProbTable8 t;
    t.alphabet_size = static_cast<uint16_t>(n);
    if (n == 0) {
        // Empty alphabet: a degenerate but legal table for an empty stream.
        // No symbol may be encoded; the table is shape-valid.
        return t;
    }
    scale_counts_to_4096(counts, n, t.freqs.data());

    // Cumulative.
    uint32_t acc = 0;
    for (std::size_t i = 0; i < n; ++i) {
        t.cum[i] = static_cast<uint16_t>(acc);
        acc += t.freqs[i];
    }
    t.cum[n] = static_cast<uint16_t>(acc);
    assert(acc == kProbScale && "ProbTable8: freqs do not sum to kProbScale");

    // slot_to_symbol[0..4096): for slot s, find symbol i with cum[i] <= s < cum[i+1].
    std::size_t s_idx = 0;
    for (uint32_t slot = 0; slot < kProbScale; ++slot) {
        while (slot >= t.cum[s_idx + 1]) ++s_idx;
        t.slot_to_symbol[slot] = static_cast<uint8_t>(s_idx);
    }
    return t;
}

ProbTable8 ProbTable8::from_freqs(const uint16_t* freqs, std::size_t n) {
    if (n > 256) throw std::invalid_argument("ProbTable8: alphabet > 256");
    ProbTable8 t;
    t.alphabet_size = static_cast<uint16_t>(n);
    if (n == 0) return t;
    uint32_t acc = 0;
    for (std::size_t i = 0; i < n; ++i) {
        t.freqs[i] = freqs[i];
        t.cum[i]   = static_cast<uint16_t>(acc);
        acc += freqs[i];
    }
    t.cum[n] = static_cast<uint16_t>(acc);
    if (acc != kProbScale) throw std::invalid_argument("ProbTable8: freqs do not sum to 4096");

    std::size_t s_idx = 0;
    for (uint32_t slot = 0; slot < kProbScale; ++slot) {
        while (slot >= t.cum[s_idx + 1]) ++s_idx;
        t.slot_to_symbol[slot] = static_cast<uint8_t>(s_idx);
    }
    return t;
}

// ---------------------------------------------------------------------------
// ProbTable16
// ---------------------------------------------------------------------------

ProbTable16 ProbTable16::from_counts(const uint32_t* counts, std::size_t n) {
    if (n > 65536) throw std::invalid_argument("ProbTable16: alphabet > 65536");
    ProbTable16 t;
    t.alphabet_size = static_cast<uint32_t>(n);
    t.freqs.assign(n, 0);
    t.cum.assign(n + 1, 0);
    if (n == 0) return t;
    scale_counts_to_4096(counts, n, t.freqs.data());

    uint32_t acc = 0;
    for (std::size_t i = 0; i < n; ++i) {
        t.cum[i] = acc;
        acc += t.freqs[i];
    }
    t.cum[n] = acc;
    assert(acc == kProbScale && "ProbTable16: freqs do not sum to kProbScale");

    std::size_t s_idx = 0;
    for (uint32_t slot = 0; slot < kProbScale; ++slot) {
        while (slot >= t.cum[s_idx + 1]) ++s_idx;
        t.slot_to_symbol[slot] = static_cast<uint16_t>(s_idx);
    }
    return t;
}

ProbTable16 ProbTable16::from_freqs(const uint16_t* freqs, std::size_t n) {
    if (n > 65536) throw std::invalid_argument("ProbTable16: alphabet > 65536");
    ProbTable16 t;
    t.alphabet_size = static_cast<uint32_t>(n);
    t.freqs.assign(freqs, freqs + n);
    t.cum.assign(n + 1, 0);
    if (n == 0) return t;

    uint32_t acc = 0;
    for (std::size_t i = 0; i < n; ++i) {
        t.cum[i] = acc;
        acc += freqs[i];
    }
    t.cum[n] = acc;
    if (acc != kProbScale) throw std::invalid_argument("ProbTable16: freqs do not sum to 4096");

    std::size_t s_idx = 0;
    for (uint32_t slot = 0; slot < kProbScale; ++slot) {
        while (slot >= t.cum[s_idx + 1]) ++s_idx;
        t.slot_to_symbol[slot] = static_cast<uint16_t>(s_idx);
    }
    return t;
}

// ---------------------------------------------------------------------------
// Encoder8 / Encoder16 -- ctors and clear()
// ---------------------------------------------------------------------------

Encoder8::Encoder8(const ProbTable8& tab) noexcept
    : tab_(&tab), x_(kRans8L), buf_() {
    buf_.reserve(64);  // small head start; grows naturally
}

void Encoder8::clear() noexcept {
    x_ = kRans8L;
    buf_.clear();
}

Encoder16::Encoder16(const ProbTable16& tab) noexcept
    : tab_(&tab), x_(kRans16L), buf_() {
    buf_.reserve(64);
}

void Encoder16::clear() noexcept {
    x_ = kRans16L;
    buf_.clear();
}

// ---------------------------------------------------------------------------
// Decoder8 / Decoder16 -- ctors
// ---------------------------------------------------------------------------
//
// The first 4 bytes of the bytestream are the encoder's flushed state, in
// little-endian byte order: byte[0] is the LSB of x.

Decoder8::Decoder8(const uint8_t* data, std::size_t n, const ProbTable8& tab) noexcept
    : tab_(&tab), p_(data), end_(data + n), x_(0) {
    if (n >= 4) {
        x_ =  static_cast<uint32_t>(p_[0])
           | (static_cast<uint32_t>(p_[1]) <<  8)
           | (static_cast<uint32_t>(p_[2]) << 16)
           | (static_cast<uint32_t>(p_[3]) << 24);
        p_ += 4;
    }
}

Decoder16::Decoder16(const uint8_t* data, std::size_t n, const ProbTable16& tab) noexcept
    : tab_(&tab), p_(data), end_(data + n), x_(0) {
    if (n >= 4) {
        x_ =  static_cast<uint32_t>(p_[0])
           | (static_cast<uint32_t>(p_[1]) <<  8)
           | (static_cast<uint32_t>(p_[2]) << 16)
           | (static_cast<uint32_t>(p_[3]) << 24);
        p_ += 4;
    }
}

// ---------------------------------------------------------------------------
// Free-function wrappers
// ---------------------------------------------------------------------------
//
// rANS encodes in reverse, so we walk the input from the end. The bytestream
// the encoder emits via finish() is already in forward decode order.

std::vector<uint8_t> encode(const uint8_t* data, std::size_t n, const ProbTable8& tab) {
    std::vector<uint8_t> out;
    out.reserve(n + 16);
    Encoder8 enc(tab);
    enc.reserve(n + 16);
    for (std::size_t i = n; i-- > 0;) enc.put(data[i]);
    enc.finish(std::back_inserter(out));
    return out;
}

std::vector<uint8_t> encode(const uint16_t* data, std::size_t n, const ProbTable16& tab) {
    std::vector<uint8_t> out;
    out.reserve(2 * n + 16);
    Encoder16 enc(tab);
    enc.reserve(2 * n + 16);
    for (std::size_t i = n; i-- > 0;) enc.put(data[i]);
    enc.finish(std::back_inserter(out));
    return out;
}

void decode(const uint8_t* enc, std::size_t n_enc, std::size_t n_symbols,
            const ProbTable8& tab, uint8_t* out) {
    Decoder8 dec(enc, n_enc, tab);
    for (std::size_t i = 0; i < n_symbols; ++i) out[i] = dec.get();
}

void decode(const uint8_t* enc, std::size_t n_enc, std::size_t n_symbols,
            const ProbTable16& tab, uint16_t* out) {
    Decoder16 dec(enc, n_enc, tab);
    for (std::size_t i = 0; i < n_symbols; ++i) out[i] = dec.get();
}

} // namespace scc::rans

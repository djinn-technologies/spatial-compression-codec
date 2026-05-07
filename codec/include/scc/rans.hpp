// codec/include/scc/rans.hpp
//
// rANS (Range Asymmetric Numeral Systems) entropy coder for the
// Spatial Compression Codec.
//
// References:
//   [Duda 2014]      Duda, J. "Asymmetric Numeral Systems: entropy coding
//                    combining speed of Huffman coding with compression rate
//                    of arithmetic coding". arXiv:1311.2540, 2014. §3.3.
//   [ryg_rans]       Giesen, F. "ryg_rans" reference implementation
//                    (rans_byte.h, rans_word.h). Public domain.
//   [US10827161B2]   Patent: cols. 7-8 disclose the rANS use for TOP / B / R
//                    streams in the SCC frame format.
//   [REQ-009]        TOP and B lists MUST be compressed using 8-bit-state rANS.
//   [REQ-010]        R list MUST be compressed using 16-bit-state rANS.
//
// Wire-format design choices (matching ryg_rans defaults):
//
//                    | rANS-8 (TOP / B)        | rANS-16 (R)
//   -----------------+-------------------------+----------------------------
//   accumulator      | uint32_t                | uint32_t
//   M (prob scale)   | 4096 (12-bit)           | 4096 (12-bit)
//   L (renorm floor) | 1 << 23                 | 1 << 16
//   b (renorm base)  | 256                     | 65536
//   chunk on wire    | 1 byte                  | 2 bytes (little-endian)
//
// State machine (shared between Encoder8 / Encoder16; only b and L differ):
//
//   put(s):                              [Duda 2014 eq. (5); REQ-009 / REQ-010]
//       f, c := tab.freqs[s], tab.cum[s]
//       x_max := ((L >> 12) << OUT_BITS) * f
//       while x >= x_max:                       <-- renormalise
//           emit_low_OUT_BITS(x); x >>= OUT_BITS
//       x := ((x / f) << 12) + (x % f) + c       <-- C(s, x)
//
//   get():                               [Duda 2014 eq. (4)]
//       slot := x & 0xFFF
//       s    := tab.slot_to_symbol[slot]
//       f, c := tab.freqs[s], tab.cum[s]
//       x    := f * (x >> 12) + slot - c          <-- D(s, x)
//       while x < L:                              <-- refill
//           x = (x << OUT_BITS) | read_OUT_BITS()
//       return s
//
// Wire-byte order: little-endian, matching ryg's `*--ptr = x & 0xFF; x >>= 8`
// renorm convention. Encoders push bytes forward into a buffer and reverse on
// finish(); decoders read forward through the bytestream.
//
// Determinism: same input + same probability table -> bit-identical output on
// every supported platform. No floating-point in the hot path.
//
// rANS is a stack: the LAST symbol encoded is the FIRST symbol decoded. The
// caller of the streaming Encoder API must therefore drive put() with the
// input *in reverse*. The free-function `encode(...)` overloads accept input
// in forward order and reverse internally.

#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

namespace scc::rans {

// ---------------------------------------------------------------------------
// Shared constants
// ---------------------------------------------------------------------------

inline constexpr unsigned kProbBits  = 12;
inline constexpr uint32_t kProbScale = 1u << kProbBits;        // 4096
static_assert(kProbScale == 4096, "12-bit probability precision");

inline constexpr uint32_t kRans8L       = 1u << 23;
inline constexpr unsigned kRans8OutBits = 8;
inline constexpr uint32_t kRans8OutMask = 0xFFu;

inline constexpr uint32_t kRans16L       = 1u << 16;
inline constexpr unsigned kRans16OutBits = 16;
inline constexpr uint32_t kRans16OutMask = 0xFFFFu;

// x_max(s) = ((L >> kProbBits) << OUT_BITS) * f. With f <= kProbScale - 1, the
// product must fit in uint32_t; otherwise the renormalisation comparison
// would wrap. Verify both variants at compile time.  [REQ-009 / REQ-010]
static_assert(((kRans8L  >> kProbBits) << kRans8OutBits ) <= 0xFFFFFFFFu / (kProbScale - 1),
              "rANS-8 x_max overflows uint32_t");
static_assert(((kRans16L >> kProbBits) << kRans16OutBits) <= 0xFFFFFFFFu / (kProbScale - 1),
              "rANS-16 x_max overflows uint32_t");

// ---------------------------------------------------------------------------
// Probability tables
// ---------------------------------------------------------------------------

// ProbTable8: alphabet size up to 256. Fixed-capacity arrays so the table is
// trivially copyable and stack-allocatable.
struct ProbTable8 {
    uint16_t                       alphabet_size = 0;       // 0..256
    std::array<uint16_t, 256>      freqs{};                 // sum == kProbScale
    std::array<uint16_t, 257>      cum{};                   // cum[N] == kProbScale
    std::array<uint8_t,  4096>     slot_to_symbol{};        // O(1) decode lookup

    // Quantises raw counts into (freqs, cum) such that:
    //   sum(freqs) == kProbScale,
    //   counts[s] > 0  =>  freqs[s] >= 1,
    //   counts[s] == 0 =>  freqs[s] == 0.
    static ProbTable8 from_counts(const uint32_t* counts, std::size_t n);

    // Builds the table from already-quantised freqs. Asserts the precondition
    // (sum == kProbScale, freqs[s] >= 1 for any intended-present symbol).
    static ProbTable8 from_freqs(const uint16_t* freqs, std::size_t n);
};

// ProbTable16: alphabet up to 65536 symbols (full uint16_t / int16_t range).
// Uses dynamic vectors to avoid a 256 KiB static footprint for sparse tables.
// slot_to_symbol stays a fixed 4096 entries: the slot is always in [0, 4096).
struct ProbTable16 {
    uint32_t                       alphabet_size = 0;       // 0..65536
    std::vector<uint16_t>          freqs;                   // size == alphabet_size
    std::vector<uint32_t>          cum;                     // size == alphabet_size + 1
    std::array<uint16_t, 4096>     slot_to_symbol{};

    static ProbTable16 from_counts(const uint32_t* counts, std::size_t n);
    static ProbTable16 from_freqs(const uint16_t* freqs, std::size_t n);
};

// ---------------------------------------------------------------------------
// Encoder8 / Encoder16 -- streaming API
// ---------------------------------------------------------------------------
//
// Usage (Encoder8 shown; Encoder16 mirrors with uint16_t symbols):
//
//     scc::rans::Encoder8 enc(table);
//     for (auto s : reverse(input)) enc.put(s);
//     std::vector<uint8_t> bytes;
//     enc.finish(std::back_inserter(bytes));
//
// Symbols are passed in *reverse* order; finish() emits the bytestream in
// decode (forward) order. After finish() the encoder is reset to a fresh
// state (x_ = L, buf_ empty) and may be reused.

class Encoder8 {
public:
    explicit Encoder8(const ProbTable8& tab) noexcept;

    inline void put(uint8_t s) noexcept;        // hot, header-defined

    // Flushes the 4-byte state, reverses the byte buffer, and copies the
    // bytestream to `out` in forward decode order. Resets internal state.
    template <class OutIt>
    OutIt finish(OutIt out);

    // Resets internal state without emitting. Cheap; reuses the buffer.
    void clear() noexcept;

    // Pre-reserve the internal byte buffer. For best throughput on a known
    // payload, call reserve(N + 16) for rans8 (or 2*N + 16 for rans16) before
    // the first put().
    void reserve(std::size_t n_bytes) { buf_.reserve(n_bytes); }

    // Diagnostic.
    std::size_t pending_bytes() const noexcept { return buf_.size(); }

private:
    const ProbTable8* tab_;
    uint32_t          x_;
    std::vector<uint8_t> buf_;   // grows forward; reversed on finish()
};

class Encoder16 {
public:
    explicit Encoder16(const ProbTable16& tab) noexcept;

    inline void put(uint16_t s) noexcept;       // hot, header-defined

    template <class OutIt>
    OutIt finish(OutIt out);

    void clear() noexcept;

    void reserve(std::size_t n_bytes) { buf_.reserve(n_bytes); }

    std::size_t pending_bytes() const noexcept { return buf_.size(); }

private:
    const ProbTable16* tab_;
    uint32_t           x_;
    std::vector<uint8_t> buf_;
};

// ---------------------------------------------------------------------------
// Decoder8 / Decoder16
// ---------------------------------------------------------------------------
//
// The caller knows how many symbols the stream contains; rANS does not
// self-describe symbol count. (See ADR-001.)

class Decoder8 {
public:
    Decoder8(const uint8_t* data, std::size_t n, const ProbTable8& tab) noexcept;

    inline uint8_t get() noexcept;              // hot, header-defined

    bool consumed() const noexcept { return p_ == end_; }

private:
    const ProbTable8* tab_;
    const uint8_t*    p_;
    const uint8_t*    end_;
    uint32_t          x_;
};

class Decoder16 {
public:
    Decoder16(const uint8_t* data, std::size_t n, const ProbTable16& tab) noexcept;

    inline uint16_t get() noexcept;             // hot, header-defined

    bool consumed() const noexcept { return p_ == end_; }

private:
    const ProbTable16* tab_;
    const uint8_t*     p_;
    const uint8_t*     end_;
    uint32_t           x_;
};

// ---------------------------------------------------------------------------
// Free-function convenience wrappers
// ---------------------------------------------------------------------------
//
// Forward-order in / forward-order out. These allocate. Use the streaming
// classes for zero-alloc workflows.

std::vector<uint8_t> encode(const uint8_t*  data, std::size_t n, const ProbTable8&  tab);
std::vector<uint8_t> encode(const uint16_t* data, std::size_t n, const ProbTable16& tab);

void decode(const uint8_t* enc, std::size_t n_enc, std::size_t n_symbols,
            const ProbTable8&  tab, uint8_t*  out);
void decode(const uint8_t* enc, std::size_t n_enc, std::size_t n_symbols,
            const ProbTable16& tab, uint16_t* out);

// ===========================================================================
// Inline definitions (hot path)
// ===========================================================================

// --- Encoder8::put ---------------------------------------------------------
//
// Renormalisation: emit one byte at a time while x is too large to admit the
// next encoded symbol without exceeding b * L = 2^31. After the loop,
//     x in [L >> kProbBits * f, x_max(s))
// ensures that the encode step lands x in [L, b*L).         [REQ-009; Duda §3.3]

inline void Encoder8::put(uint8_t s) noexcept {
    assert(tab_ != nullptr);
    assert(s < tab_->alphabet_size);
    const uint32_t f = tab_->freqs[s];
    const uint32_t c = tab_->cum[s];
    assert(f != 0 && "encoder symbol has zero probability");
    // Single-symbol alphabet: f == M means C(s, x) is the identity. Skipping
    // here also keeps `((L >> 12) << 8) * f` from overflowing uint32_t when
    // f == kProbScale (rans-8 stays inside but rans-16 doesn't; symmetric for
    // safety). [REQ-009]
    if (f == kProbScale) return;
    uint32_t x = x_;
    const uint32_t x_max = ((kRans8L >> kProbBits) << kRans8OutBits) * f;
    while (x >= x_max) {
        buf_.push_back(static_cast<uint8_t>(x & kRans8OutMask));
        x >>= kRans8OutBits;
    }
    x_ = ((x / f) << kProbBits) + (x % f) + c;
}

// --- Encoder16::put --------------------------------------------------------
//
// Same shape as rans8 but emits a 16-bit chunk (two bytes, little-endian).
// HI byte pushed first so that, after the finish()-time reversal, the LO byte
// appears first in decode order: decoder recombines as `lo | (hi << 8)`.
//                                                          [REQ-010; Duda §3.3]

inline void Encoder16::put(uint16_t s) noexcept {
    assert(tab_ != nullptr);
    assert(s < tab_->alphabet_size);
    const uint32_t f = tab_->freqs[s];
    const uint32_t c = tab_->cum[s];
    assert(f != 0 && "encoder symbol has zero probability");
    // Single-symbol alphabet: f == M means C(s, x) is the identity AND
    // (L >> 12) << 16) * f overflows uint32_t to 0, which would make the
    // renorm condition `x >= 0` an infinite loop. Early return instead.
    // [REQ-010]
    if (f == kProbScale) return;
    uint32_t x = x_;
    const uint32_t x_max = ((kRans16L >> kProbBits) << kRans16OutBits) * f;
    while (x >= x_max) {
        buf_.push_back(static_cast<uint8_t>((x >> 8) & 0xFFu));   // HI -- pushed first
        buf_.push_back(static_cast<uint8_t>( x       & 0xFFu));   // LO -- pushed last
        x >>= kRans16OutBits;
    }
    x_ = ((x / f) << kProbBits) + (x % f) + c;
}

// --- Encoder*::finish ------------------------------------------------------
//
// Flush x as four little-endian bytes (HI byte pushed first; LO last). Reverse
// the full buffer so decode order = forward iteration. Reset to fresh state.

template <class OutIt>
OutIt Encoder8::finish(OutIt out) {
    const uint32_t x = x_;
    buf_.push_back(static_cast<uint8_t>((x >> 24) & 0xFFu));
    buf_.push_back(static_cast<uint8_t>((x >> 16) & 0xFFu));
    buf_.push_back(static_cast<uint8_t>((x >>  8) & 0xFFu));
    buf_.push_back(static_cast<uint8_t>( x        & 0xFFu));
    std::reverse(buf_.begin(), buf_.end());
    out = std::copy(buf_.begin(), buf_.end(), out);
    clear();
    return out;
}

template <class OutIt>
OutIt Encoder16::finish(OutIt out) {
    const uint32_t x = x_;
    buf_.push_back(static_cast<uint8_t>((x >> 24) & 0xFFu));
    buf_.push_back(static_cast<uint8_t>((x >> 16) & 0xFFu));
    buf_.push_back(static_cast<uint8_t>((x >>  8) & 0xFFu));
    buf_.push_back(static_cast<uint8_t>( x        & 0xFFu));
    std::reverse(buf_.begin(), buf_.end());
    out = std::copy(buf_.begin(), buf_.end(), out);
    clear();
    return out;
}

// --- Decoder8::get ---------------------------------------------------------

inline uint8_t Decoder8::get() noexcept {
    assert(tab_ != nullptr);
    uint32_t x = x_;
    const uint32_t slot = x & (kProbScale - 1);
    const uint8_t  s    = tab_->slot_to_symbol[slot];
    const uint32_t f    = tab_->freqs[s];
    const uint32_t c    = tab_->cum[s];
    x = f * (x >> kProbBits) + slot - c;
    while (x < kRans8L) {
        assert(p_ != end_ && "rANS-8 decoder underflow: bytestream too short");
        x = (x << kRans8OutBits) | static_cast<uint32_t>(*p_++);
    }
    x_ = x;
    return s;
}

// --- Decoder16::get --------------------------------------------------------

inline uint16_t Decoder16::get() noexcept {
    assert(tab_ != nullptr);
    uint32_t x = x_;
    const uint32_t slot = x & (kProbScale - 1);
    const uint16_t s    = tab_->slot_to_symbol[slot];
    const uint32_t f    = tab_->freqs[s];
    const uint32_t c    = tab_->cum[s];
    x = f * (x >> kProbBits) + slot - c;
    while (x < kRans16L) {
        assert(p_ + 2 <= end_ && "rANS-16 decoder underflow: bytestream too short");
        const uint32_t lo = *p_++;
        const uint32_t hi = *p_++;
        x = (x << kRans16OutBits) | (lo | (hi << 8));
    }
    x_ = x;
    return s;
}

} // namespace scc::rans

// bench/src/codecs/codec.hpp
//
// ICodec abstracts a single codec under test. Every codec the harness
// supports implements this interface; the timing loop in main.cpp is
// IDENTICAL for all of them, which is how we satisfy Ultrathink #1
// (fairness): the only variable across codecs is the codec name.
//
// Lifecycle (called in this order, per worker thread):
//   1. configure(profile, w, h, bit_depth) -> string error      (once per cfg)
//   2. encode(frame) -> EncodeResult                            (per frame)
//   3. decode(bytes, w, h, bit_depth) -> DecodeResult           (per frame)
//
// Errors are returned as strings, NOT thrown -- exceptions in a tight
// timing loop would force every codec to wrap calls in try/catch and
// would make the loop's branch profile non-uniform. Empty error string
// == success.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../frame.hpp"

namespace scc::bench {

struct EncodeResult {
    std::vector<std::uint8_t> bytes;
    std::string               error;     // empty == ok
};

struct DecodeResult {
    Frame       frame;                   // pixel buffer is sized by codec
    std::string error;
};

// Profile names are codec-specific strings parsed by the codec. The
// bench passes them through unchanged so we don't have to maintain a
// global enum of every codec's supported quality knobs.
//
// Conventions used by the included codecs:
//   - scc      : "lossless" | "lossy:high" | "lossy:streaming"
//   - jpeg2000 : "lossless" | "lossy:Q<n>"   (n = compression ratio)
//   - png      : "lossless"                  (only mode supported)
//   - tiff     : "lossless"                  (deflate-compressed)
//   - zlib     : "level:<0..9>"
//
// "lossless" and "lossy:high" are accepted by every codec; they map to
// the codec's nearest equivalent if the codec doesn't natively support
// the named mode.

class ICodec {
public:
    virtual ~ICodec() = default;
    [[nodiscard]] virtual const char* name() const noexcept = 0;

    // Returns "" on success; an error string on failure.
    [[nodiscard]] virtual std::string configure(const std::string& profile,
                                                std::uint32_t width,
                                                std::uint32_t height,
                                                std::uint8_t  bit_depth) = 0;

    [[nodiscard]] virtual EncodeResult encode(const Frame& frame) = 0;
    [[nodiscard]] virtual DecodeResult decode(const std::uint8_t* bytes,
                                              std::size_t          n_bytes,
                                              std::uint32_t        width,
                                              std::uint32_t        height,
                                              std::uint8_t         bit_depth) = 0;

    // Cheap reset between sequences -- avoids reconstruction cost when
    // the next sequence has the same (profile, w, h, bit_depth).
    virtual void reset() {}
};

// Factory functions, one per concrete codec. Defined in their own .cpp
// files so the build only links the codec library when the codec is
// selected (transitively through the executable's link line, but the
// compile is one TU per codec for fast incremental rebuilds).
std::unique_ptr<ICodec> make_codec_scc();
std::unique_ptr<ICodec> make_codec_jpeg2000();
std::unique_ptr<ICodec> make_codec_png();
std::unique_ptr<ICodec> make_codec_tiff();
std::unique_ptr<ICodec> make_codec_zlib();

// Registry-style lookup: returns nullptr for unknown names. Names are
// matched case-insensitively.
std::unique_ptr<ICodec> make_codec_by_name(const std::string& name);

} // namespace scc::bench

// bench/src/codecs/scc.cpp
//
// SCC's wrapper for the comparator harness. Links against the in-tree
// scc::codec target (the C++ classes), NOT the C ABI -- the bench is
// in-process so there's no FFI overhead to pay. The C ABI is verified
// separately by libscc's own tests (ADR-006).
//
// The C++ surface assumed here is the user-facing one declared in
// codec/include/scc/scc.hpp:
//   namespace scc {
//     enum class Profile { Lossless, LossyHigh, LossyStreaming };
//     class Encoder {
//       Encoder(uint32_t w, uint32_t h, uint8_t bit_depth, Profile);
//       std::vector<uint8_t> encode_frame(const uint16_t* depth, size_t n);
//       void reset();
//     };
//     struct DecodedFrame { std::vector<uint16_t> data; uint32_t w, h; uint8_t bd; };
//     class Decoder {
//       Decoder();
//       DecodedFrame decode_frame(const uint8_t* bytes, size_t n);
//       void reset();
//     };
//   }
//
// If the public API names drift, this file is the single point of
// adjustment.

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

#include <scc/scc.hpp>

#include "codec.hpp"

namespace scc::bench {

namespace {

scc::Profile parse_profile(const std::string& p) {
    std::string lc(p);
    std::transform(lc.begin(), lc.end(), lc.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lc == "lossless")        return scc::Profile::Lossless;
    if (lc == "lossy:high")      return scc::Profile::LossyHigh;
    if (lc == "lossy:streaming") return scc::Profile::LossyStreaming;
    // Bench convention: unknown profile maps to LossyHigh with a
    // surfaced error so the harness records this as a configure
    // failure rather than silently choosing a different profile.
    throw std::invalid_argument("scc: unknown profile: " + p);
}

class SccCodec final : public ICodec {
public:
    [[nodiscard]] const char* name() const noexcept override { return "scc"; }

    [[nodiscard]] std::string configure(const std::string& profile,
                                        std::uint32_t width,
                                        std::uint32_t height,
                                        std::uint8_t  bit_depth) override {
        try {
            const auto p = parse_profile(profile);
            encoder_ = std::make_unique<scc::Encoder>(width, height, bit_depth, p);
            decoder_ = std::make_unique<scc::Decoder>();
            cfg_w_ = width; cfg_h_ = height; cfg_bd_ = bit_depth;
            return {};
        } catch (const std::exception& e) {
            return std::string{"scc.configure: "} + e.what();
        }
    }

    [[nodiscard]] EncodeResult encode(const Frame& frame) override {
        if (!encoder_) return {{}, "scc.encode: codec not configured"};
        if (frame.width != cfg_w_ || frame.height != cfg_h_ || frame.bit_depth != cfg_bd_) {
            return {{}, "scc.encode: frame dims do not match configured dims"};
        }
        try {
            auto out = encoder_->encode_frame(frame.data.data(), frame.data.size());
            return {std::move(out), {}};
        } catch (const std::exception& e) {
            return {{}, std::string{"scc.encode: "} + e.what()};
        }
    }

    [[nodiscard]] DecodeResult decode(const std::uint8_t* bytes,
                                      std::size_t          n_bytes,
                                      std::uint32_t        width,
                                      std::uint32_t        height,
                                      std::uint8_t         bit_depth) override {
        if (!decoder_) return {{}, "scc.decode: codec not configured"};
        try {
            scc::DecodedFrame df = decoder_->decode_frame(bytes, n_bytes);
            // Trust the decoded header; the bench cross-checks against
            // configured dims when it receives the result.
            DecodeResult r;
            r.frame.width     = df.w;
            r.frame.height    = df.h;
            r.frame.bit_depth = df.bd;
            r.frame.data      = std::move(df.data);
            // Defensive: even if the wire said different dims, we also
            // have what the caller expected -- if they disagree, the
            // metric pass will catch it and mark the row as "decode
            // dimension mismatch".
            (void)width; (void)height; (void)bit_depth;
            return r;
        } catch (const std::exception& e) {
            return {{}, std::string{"scc.decode: "} + e.what()};
        }
    }

    void reset() override {
        if (encoder_) encoder_->reset();
        if (decoder_) decoder_->reset();
    }

private:
    // std::optional<scc::Encoder/Decoder> would be cleaner, but the
    // optional storage requires Encoder/Decoder to be default-
    // constructible OR move-constructible. We use a pair of
    // unique_ptr's instead so we don't constrain the public API.
    std::unique_ptr<scc::Encoder> encoder_;
    std::unique_ptr<scc::Decoder> decoder_;
    std::uint32_t cfg_w_  = 0;
    std::uint32_t cfg_h_  = 0;
    std::uint8_t  cfg_bd_ = 0;
};

} // namespace

std::unique_ptr<ICodec> make_codec_scc() {
    return std::make_unique<SccCodec>();
}

} // namespace scc::bench

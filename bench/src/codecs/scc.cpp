// bench/src/codecs/scc.cpp
//
// SCC's wrapper for the comparator harness. Links against libscc -- the
// stable C ABI shim from cabi/. Going through the public ABI is the
// most honest comparison point: that's exactly the surface every
// production downstream (Python, Rust, Node, Unity, Unreal) uses, so
// any throughput overhead from the C-ABI boundary is part of the
// number we ship to users.
//
// Profile mapping is JSON-driven via scc_load_profile, since the C ABI
// does not expose a "profile" concept directly -- it exposes scalar
// parameters (top_count, tau_*, leaf_size_px, mode_flags). The bench's
// named profiles are translated to those scalars below; the values are
// reasonable defaults that the codec maintainer can tune by editing
// the profile_json() table.

#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <libscc.h>

#include "codec.hpp"

namespace scc::bench {

namespace {

// Translate a bench profile name to a flat JSON parameter set
// understood by scc_load_profile. mode_flags is the only knob that
// switches between lossless and lossy in the v1 ABI; bit 0 = "any
// lossy stage allowed", bit 1 = "low-bitrate streaming preferences".
std::string profile_json(const std::string& profile_name) {
    std::string lc(profile_name);
    std::transform(lc.begin(), lc.end(), lc.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lc == "lossless") {
        // mode_flags = 0 -> reversible everywhere
        return R"({"top_count":4,"leaf_size_px":16,"mode_flags":0,)"
               R"("tau_static":5,"tau_low":50,"tau_high":200})";
    }
    if (lc == "lossy:high") {
        // mode_flags = 1 -> visually-lossless lossy stages enabled
        return R"({"top_count":4,"leaf_size_px":16,"mode_flags":1,)"
               R"("tau_static":5,"tau_low":50,"tau_high":200})";
    }
    if (lc == "lossy:streaming") {
        // mode_flags = 3 -> low-bitrate streaming bias on top of lossy
        return R"({"top_count":4,"leaf_size_px":32,"mode_flags":3,)"
               R"("tau_static":10,"tau_low":80,"tau_high":255})";
    }
    throw std::invalid_argument("scc: unknown profile: " + profile_name);
}

class SccCodec final : public ICodec {
public:
    SccCodec()  = default;
    ~SccCodec() override { destroy_(); }

    SccCodec(const SccCodec&) = delete;
    SccCodec& operator=(const SccCodec&) = delete;

    [[nodiscard]] const char* name() const noexcept override { return "scc"; }

    [[nodiscard]] std::string configure(const std::string& profile,
                                        std::uint32_t width,
                                        std::uint32_t height,
                                        std::uint8_t  bit_depth) override {
        destroy_();
        try {
            const std::string json = profile_json(profile);
            ctx_ = scc_init();
            if (!ctx_) return "scc.configure: scc_init returned null";
            const scc_result rc = scc_load_profile(ctx_, json.c_str());
            if (rc != SCC_OK) {
                std::string err = std::string{"scc.configure: scc_load_profile rc="}
                                + std::to_string(static_cast<int>(rc));
                if (const char* lasterr = scc_get_last_error(ctx_); lasterr && *lasterr) {
                    err += " ("; err += lasterr; err += ")";
                }
                destroy_();
                return err;
            }
            cfg_w_ = width; cfg_h_ = height; cfg_bd_ = bit_depth;
            return {};
        } catch (const std::exception& e) {
            destroy_();
            return std::string{"scc.configure: "} + e.what();
        }
    }

    [[nodiscard]] EncodeResult encode(const Frame& frame) override {
        if (!ctx_) return {{}, "scc.encode: codec not configured"};
        if (frame.width != cfg_w_ || frame.height != cfg_h_ || frame.bit_depth != cfg_bd_) {
            return {{}, "scc.encode: frame dims do not match configured dims"};
        }
        const auto* depth_bytes = reinterpret_cast<const std::uint8_t*>(frame.data.data());
        const std::size_t stride = static_cast<std::size_t>(frame.width) * sizeof(std::uint16_t);

        // Two-phase buffer-too-small protocol per libscc.h.
        std::size_t need = 0;
        scc_result rc = scc_encode_frame(ctx_, depth_bytes, stride,
                                         static_cast<int>(frame.width),
                                         static_cast<int>(frame.height),
                                         static_cast<int>(frame.bit_depth),
                                         /*out_sei*/ nullptr,
                                         /*out_cap*/ 0,
                                         &need);
        if (rc != SCC_INVALID_ARG && rc != SCC_OK) {
            return {{}, last_error_("scc.encode (probe)", rc)};
        }
        std::vector<std::uint8_t> out(need);
        std::size_t actual = 0;
        rc = scc_encode_frame(ctx_, depth_bytes, stride,
                              static_cast<int>(frame.width),
                              static_cast<int>(frame.height),
                              static_cast<int>(frame.bit_depth),
                              out.data(), out.size(), &actual);
        if (rc != SCC_OK) {
            return {{}, last_error_("scc.encode", rc)};
        }
        out.resize(actual);
        return {std::move(out), {}};
    }

    [[nodiscard]] DecodeResult decode(const std::uint8_t* bytes,
                                      std::size_t          n_bytes,
                                      std::uint32_t        width,
                                      std::uint32_t        height,
                                      std::uint8_t         bit_depth) override {
        if (!ctx_) return {{}, "scc.decode: codec not configured"};

        DecodeResult r;
        r.frame.width     = width;
        r.frame.height    = height;
        r.frame.bit_depth = bit_depth;
        r.frame.data.resize(static_cast<std::size_t>(width) * height);
        const std::size_t stride = static_cast<std::size_t>(width) * sizeof(std::uint16_t);

        int got_w = 0, got_h = 0, got_bd = 0;
        const scc_result rc = scc_decode_sei(
            ctx_, bytes, n_bytes,
            reinterpret_cast<std::uint8_t*>(r.frame.data.data()),
            stride, &got_w, &got_h, &got_bd);
        if (rc != SCC_OK) {
            return {{}, last_error_("scc.decode", rc)};
        }
        // Trust the wire dims; the metric pass cross-checks against the
        // source frame and will mark dimension drift as a failure.
        r.frame.width     = static_cast<std::uint32_t>(got_w);
        r.frame.height    = static_cast<std::uint32_t>(got_h);
        r.frame.bit_depth = static_cast<std::uint8_t>(got_bd);
        return r;
    }

    void reset() override {
        // The C ABI has no explicit reset; teardown + re-init is cheap
        // and matches the lifecycle every other codec wrapper performs.
        // Callers wanting reset across many sequences should re-call
        // configure() instead, which already destroys + recreates.
    }

private:
    void destroy_() noexcept {
        if (ctx_) { scc_destroy(ctx_); ctx_ = nullptr; }
    }

    std::string last_error_(const char* where, scc_result rc) const {
        std::string out = where;
        out += ": rc=" + std::to_string(static_cast<int>(rc));
        if (ctx_) {
            if (const char* lasterr = scc_get_last_error(ctx_); lasterr && *lasterr) {
                out += " ("; out += lasterr; out += ")";
            }
        }
        return out;
    }

    scc_ctx*      ctx_   = nullptr;
    std::uint32_t cfg_w_ = 0;
    std::uint32_t cfg_h_ = 0;
    std::uint8_t  cfg_bd_ = 0;
};

} // namespace

std::unique_ptr<ICodec> make_codec_scc() {
    return std::make_unique<SccCodec>();
}

} // namespace scc::bench

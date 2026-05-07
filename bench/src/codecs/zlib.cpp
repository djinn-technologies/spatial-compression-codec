// bench/src/codecs/zlib.cpp
//
// Raw zlib over the u16 depth buffer. This is the "headless baseline":
// no container, no per-frame header, just `compress2` over the bytes
// of the raster. It establishes a *floor* for the value SCC is adding
// over a generic byte-level compressor.
//
// Profile mapping:
//   "lossless"   -> level 6 (zlib default)
//   "lossy:high" -> level 9 (best ratio; we accept the alias as a level dial)
//   "level:<N>"  -> N in [0..9]

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include <zlib.h>

#include "codec.hpp"

namespace scc::bench {

namespace {

class ZlibCodec final : public ICodec {
public:
    [[nodiscard]] const char* name() const noexcept override { return "zlib"; }

    [[nodiscard]] std::string configure(const std::string& profile,
                                        std::uint32_t,
                                        std::uint32_t,
                                        std::uint8_t) override {
        std::string lc(profile);
        std::transform(lc.begin(), lc.end(), lc.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lc == "lossless")        cfg_level_ = 6;
        else if (lc == "lossy:high") cfg_level_ = 9;
        else if (lc.rfind("level:", 0) == 0) {
            try {
                cfg_level_ = std::stoi(lc.substr(6));
                if (cfg_level_ < 0 || cfg_level_ > 9) return "zlib: level out of range";
            } catch (...) {
                return "zlib: failed to parse profile " + profile;
            }
        }
        else return "zlib: unsupported profile " + profile;
        return {};
    }

    [[nodiscard]] EncodeResult encode(const Frame& frame) override {
        const auto* src = reinterpret_cast<const Bytef*>(frame.data.data());
        const uLong  src_len = static_cast<uLong>(frame.data.size() * sizeof(std::uint16_t));
        uLong dst_len = compressBound(src_len);
        std::vector<std::uint8_t> out(dst_len);
        const int rc = compress2(out.data(), &dst_len, src, src_len, cfg_level_);
        if (rc != Z_OK) {
            return {{}, "zlib.encode: compress2 rc=" + std::to_string(rc)};
        }
        out.resize(dst_len);
        return {std::move(out), {}};
    }

    [[nodiscard]] DecodeResult decode(const std::uint8_t* bytes,
                                      std::size_t          n_bytes,
                                      std::uint32_t        width,
                                      std::uint32_t        height,
                                      std::uint8_t         bit_depth) override {
        DecodeResult r;
        r.frame.width = width; r.frame.height = height; r.frame.bit_depth = bit_depth;
        r.frame.data.resize(static_cast<std::size_t>(width) * height);
        uLong dst_len = static_cast<uLong>(r.frame.data.size() * sizeof(std::uint16_t));
        const int rc = uncompress(reinterpret_cast<Bytef*>(r.frame.data.data()),
                                  &dst_len, bytes, static_cast<uLong>(n_bytes));
        if (rc != Z_OK) {
            return {{}, "zlib.decode: uncompress rc=" + std::to_string(rc)};
        }
        if (dst_len != static_cast<uLong>(r.frame.data.size() * sizeof(std::uint16_t))) {
            return {{}, "zlib.decode: short output"};
        }
        const std::uint16_t mask = static_cast<std::uint16_t>((1u << bit_depth) - 1u);
        for (auto& v : r.frame.data) v &= mask;
        return r;
    }

private:
    int cfg_level_ = 6;
};

} // namespace

std::unique_ptr<ICodec> make_codec_zlib() {
    return std::make_unique<ZlibCodec>();
}

// Registry. Defined here because zlib is the only TU sure to be linked
// in every build configuration, and the registry needs to live in
// exactly one place.
std::unique_ptr<ICodec> make_codec_by_name(const std::string& name) {
    std::string lc(name);
    std::transform(lc.begin(), lc.end(), lc.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lc == "scc")      return make_codec_scc();
    if (lc == "jpeg2000" || lc == "jp2") return make_codec_jpeg2000();
    if (lc == "png")      return make_codec_png();
    if (lc == "tiff")     return make_codec_tiff();
    if (lc == "zlib")     return make_codec_zlib();
    return nullptr;
}

} // namespace scc::bench

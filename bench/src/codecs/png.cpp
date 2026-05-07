// bench/src/codecs/png.cpp
//
// libpng wrapper. PNG is lossless by definition; we only support the
// "lossless" profile and the "lossy:high" alias (which we accept but
// document as a no-op -- PNG has no quality knob, only a deflate
// compression-level dial).
//
// Sub-16-bit data is stored at 16bpp because libpng's grayscale modes
// are 1, 2, 4, 8, 16 -- there is no native 12-bit grayscale. Decoding
// the result re-masks to the source bit depth so the metric pass sees
// the same value range.
//
// Wire byte order for 16-bit grayscale is BIG-ENDIAN per the PNG spec
// (RFC 2083 §2.1). We swap on encode and decode.

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include <png.h>

#include "codec.hpp"

namespace scc::bench {

namespace {

// Memory write callback. libpng writes to user-supplied output via this.
struct MemWriter { std::vector<std::uint8_t>* out; };
void png_mem_write(png_structp p, png_bytep data, png_size_t n) {
    auto* w = static_cast<MemWriter*>(png_get_io_ptr(p));
    auto& v = *w->out;
    const std::size_t old = v.size();
    v.resize(old + n);
    std::memcpy(v.data() + old, data, n);
}
void png_mem_flush(png_structp) {}

struct MemReader {
    const std::uint8_t* buf;
    std::size_t         size;
    std::size_t         pos;
};
void png_mem_read(png_structp p, png_bytep data, png_size_t n) {
    auto* r = static_cast<MemReader*>(png_get_io_ptr(p));
    if (r->pos + n > r->size) {
        png_error(p, "scc-bench png: short read");
        return;
    }
    std::memcpy(data, r->buf + r->pos, n);
    r->pos += n;
}

void silent_warn(png_structp, png_const_charp) {}
void fatal_error(png_structp p, png_const_charp msg) {
    // jump out of libpng -- the calling layer catches via setjmp.
    auto* err = static_cast<std::string*>(png_get_error_ptr(p));
    if (err) *err = msg ? msg : "unknown";
    longjmp(png_jmpbuf(p), 1);
}

class PngCodec final : public ICodec {
public:
    [[nodiscard]] const char* name() const noexcept override { return "png"; }

    [[nodiscard]] std::string configure(const std::string& profile,
                                        std::uint32_t,
                                        std::uint32_t,
                                        std::uint8_t bit_depth) override {
        if (bit_depth > 16) return "png: bit_depth>16 not supported";
        std::string lc(profile);
        std::transform(lc.begin(), lc.end(), lc.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lc != "lossless" && lc != "lossy:high") {
            return "png: unsupported profile " + profile + " (only lossless / lossy:high alias)";
        }
        // libpng has 6 deflate levels; level 6 is the default. Bench
        // lossy:high uses level 6; lossless uses level 9 (slowest, smallest).
        cfg_level_ = (lc == "lossless") ? 9 : 6;
        return {};
    }

    [[nodiscard]] EncodeResult encode(const Frame& frame) override {
        std::string err_msg;
        png_structp pw = png_create_write_struct(
            PNG_LIBPNG_VER_STRING, &err_msg, fatal_error, silent_warn);
        if (!pw) return {{}, "png.encode: png_create_write_struct failed"};
        png_infop info = png_create_info_struct(pw);
        if (!info) {
            png_destroy_write_struct(&pw, nullptr);
            return {{}, "png.encode: png_create_info_struct failed"};
        }

        std::vector<std::uint8_t> out;
        MemWriter writer{&out};

        if (setjmp(png_jmpbuf(pw))) {
            png_destroy_write_struct(&pw, &info);
            return {{}, "png.encode: " + (err_msg.empty() ? "longjmp" : err_msg)};
        }
        png_set_write_fn(pw, &writer, png_mem_write, png_mem_flush);
        png_set_compression_level(pw, cfg_level_);

        // PNG always writes the bit-depth we declare. Sub-16-bit
        // sources go on the wire as 16-bit (PNG has no 12-bit mode).
        int wire_bd = (frame.bit_depth <= 8) ? 8 : 16;
        png_set_IHDR(pw, info, frame.width, frame.height,
            wire_bd,
            PNG_COLOR_TYPE_GRAY,
            PNG_INTERLACE_NONE,
            PNG_COMPRESSION_TYPE_DEFAULT,
            PNG_FILTER_TYPE_DEFAULT);
        png_write_info(pw, info);

        // Write rows. For 16bpp wire, byte order is big-endian.
        std::vector<std::uint8_t> row_buf(frame.width * (wire_bd == 8 ? 1 : 2));
        for (std::uint32_t y = 0; y < frame.height; ++y) {
            const std::uint16_t* src = frame.data.data() + y * frame.width;
            if (wire_bd == 8) {
                for (std::uint32_t x = 0; x < frame.width; ++x) {
                    row_buf[x] = static_cast<std::uint8_t>(src[x] & 0xFFu);
                }
            } else {
                for (std::uint32_t x = 0; x < frame.width; ++x) {
                    row_buf[x*2 + 0] = static_cast<std::uint8_t>((src[x] >> 8) & 0xFFu);
                    row_buf[x*2 + 1] = static_cast<std::uint8_t>(src[x] & 0xFFu);
                }
            }
            png_write_row(pw, row_buf.data());
        }
        png_write_end(pw, info);
        png_destroy_write_struct(&pw, &info);
        return {std::move(out), {}};
    }

    [[nodiscard]] DecodeResult decode(const std::uint8_t* bytes,
                                      std::size_t          n_bytes,
                                      std::uint32_t        width,
                                      std::uint32_t        height,
                                      std::uint8_t         bit_depth) override {
        std::string err_msg;
        png_structp pr = png_create_read_struct(
            PNG_LIBPNG_VER_STRING, &err_msg, fatal_error, silent_warn);
        if (!pr) return {{}, "png.decode: png_create_read_struct failed"};
        png_infop info = png_create_info_struct(pr);
        if (!info) {
            png_destroy_read_struct(&pr, nullptr, nullptr);
            return {{}, "png.decode: png_create_info_struct failed"};
        }

        if (setjmp(png_jmpbuf(pr))) {
            png_destroy_read_struct(&pr, &info, nullptr);
            return {{}, "png.decode: " + (err_msg.empty() ? "longjmp" : err_msg)};
        }

        MemReader reader{bytes, n_bytes, 0};
        png_set_read_fn(pr, &reader, png_mem_read);
        png_read_info(pr, info);
        const std::uint32_t w  = png_get_image_width(pr, info);
        const std::uint32_t h  = png_get_image_height(pr, info);
        const int           bd = png_get_bit_depth(pr, info);
        const int           ct = png_get_color_type(pr, info);
        if (ct != PNG_COLOR_TYPE_GRAY) {
            png_destroy_read_struct(&pr, &info, nullptr);
            return {{}, "png.decode: non-gray PNG"};
        }

        DecodeResult r;
        r.frame.width = w; r.frame.height = h;
        r.frame.bit_depth = bit_depth;
        r.frame.data.resize(static_cast<std::size_t>(w) * h);

        const int row_bytes = w * (bd == 8 ? 1 : 2);
        std::vector<std::uint8_t> row_buf(row_bytes);
        const std::uint16_t mask = static_cast<std::uint16_t>((1u << bit_depth) - 1u);

        for (std::uint32_t y = 0; y < h; ++y) {
            png_read_row(pr, row_buf.data(), nullptr);
            std::uint16_t* dst = r.frame.data.data() + y * w;
            if (bd == 8) {
                for (std::uint32_t x = 0; x < w; ++x) {
                    dst[x] = static_cast<std::uint16_t>(row_buf[x] & mask);
                }
            } else {
                for (std::uint32_t x = 0; x < w; ++x) {
                    std::uint16_t v = static_cast<std::uint16_t>(
                        (static_cast<std::uint16_t>(row_buf[x*2]) << 8) |
                        row_buf[x*2 + 1]);
                    dst[x] = static_cast<std::uint16_t>(v & mask);
                }
            }
        }
        png_read_end(pr, nullptr);
        png_destroy_read_struct(&pr, &info, nullptr);
        (void)width; (void)height;
        return r;
    }

private:
    int cfg_level_ = 6;
};

} // namespace

std::unique_ptr<ICodec> make_codec_png() {
    return std::make_unique<PngCodec>();
}

} // namespace scc::bench

// bench/src/codecs/jpeg2000.cpp
//
// JPEG 2000 wrapper using OpenJPEG 2.5 (libopenjp2). We use the
// memory-stream API so we never touch the filesystem -- timing measures
// codec work, not disk IO.
//
// Profile mapping:
//   "lossless"    -> opj_set_default_encoder_parameters then irreversible=0
//   "lossy:high"  -> tile-quality 50 (psnr ~50 dB on depth, similar to SCC's high)
//   "lossy:Q<n>"  -> tile-quality n
//
// Why irreversible=0 for lossless: OpenJPEG's reversible 5/3 wavelet is
// the lossless path; the irreversible 9/7 wavelet path is faster but
// adds rounding error.

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

#include <openjpeg.h>

#include "codec.hpp"

namespace scc::bench {

namespace {

// In-memory stream support for OpenJPEG. The codec calls these
// callbacks during encode/decode; we hand-hold the position cursor.
struct MemStream {
    const std::uint8_t* read_buf  = nullptr;
    std::size_t         read_size = 0;
    std::size_t         read_pos  = 0;
    std::vector<std::uint8_t>* write_buf = nullptr;
};

OPJ_SIZE_T mem_read(void* p_buf, OPJ_SIZE_T n_bytes, void* p_user) {
    auto* m = static_cast<MemStream*>(p_user);
    if (m->read_pos >= m->read_size) return static_cast<OPJ_SIZE_T>(-1);
    std::size_t avail = m->read_size - m->read_pos;
    std::size_t n = (n_bytes < avail) ? n_bytes : avail;
    std::memcpy(p_buf, m->read_buf + m->read_pos, n);
    m->read_pos += n;
    return n;
}

OPJ_SIZE_T mem_write(void* p_buf, OPJ_SIZE_T n_bytes, void* p_user) {
    auto* m = static_cast<MemStream*>(p_user);
    auto& buf = *m->write_buf;
    const std::size_t old = buf.size();
    buf.resize(old + n_bytes);
    std::memcpy(buf.data() + old, p_buf, n_bytes);
    return n_bytes;
}

OPJ_OFF_T mem_skip_read(OPJ_OFF_T n_bytes, void* p_user) {
    auto* m = static_cast<MemStream*>(p_user);
    if (n_bytes < 0) return -1;
    std::size_t new_pos = m->read_pos + static_cast<std::size_t>(n_bytes);
    if (new_pos > m->read_size) new_pos = m->read_size;
    OPJ_OFF_T skipped = static_cast<OPJ_OFF_T>(new_pos - m->read_pos);
    m->read_pos = new_pos;
    return skipped;
}

OPJ_BOOL mem_seek_read(OPJ_OFF_T pos, void* p_user) {
    auto* m = static_cast<MemStream*>(p_user);
    if (pos < 0 || static_cast<std::size_t>(pos) > m->read_size) return OPJ_FALSE;
    m->read_pos = static_cast<std::size_t>(pos);
    return OPJ_TRUE;
}

void silent(const char*, void*) {}   // suppress OpenJPEG's stderr noise

class Jpeg2000Codec final : public ICodec {
public:
    [[nodiscard]] const char* name() const noexcept override { return "jpeg2000"; }

    [[nodiscard]] std::string configure(const std::string& profile,
                                        std::uint32_t width,
                                        std::uint32_t height,
                                        std::uint8_t  bit_depth) override {
        if (bit_depth > 16) return "jpeg2000: bit_depth>16 not supported";
        cfg_w_ = width; cfg_h_ = height; cfg_bd_ = bit_depth;
        cfg_lossless_ = false;
        cfg_quality_  = 0;

        std::string lc(profile);
        std::transform(lc.begin(), lc.end(), lc.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lc == "lossless") {
            cfg_lossless_ = true;
        } else if (lc == "lossy:high") {
            cfg_lossless_ = false;
            cfg_quality_  = 50;
        } else if (lc.rfind("lossy:q", 0) == 0) {
            try {
                cfg_quality_ = std::stoi(lc.substr(7));
                cfg_lossless_ = false;
            } catch (...) {
                return "jpeg2000: failed to parse profile " + profile;
            }
        } else {
            return "jpeg2000: unknown profile " + profile;
        }
        return {};
    }

    [[nodiscard]] EncodeResult encode(const Frame& frame) override {
        opj_image_cmptparm_t comp{};
        comp.dx   = 1; comp.dy = 1;
        comp.x0   = 0; comp.y0 = 0;
        comp.w    = frame.width;
        comp.h    = frame.height;
        comp.prec = frame.bit_depth;
        comp.bpp  = frame.bit_depth;
        comp.sgnd = 0;

        opj_image_t* img = opj_image_create(1, &comp, OPJ_CLRSPC_GRAY);
        if (!img) return {{}, "jpeg2000.encode: opj_image_create failed"};
        img->x0 = 0; img->y0 = 0;
        img->x1 = frame.width; img->y1 = frame.height;

        // OpenJPEG wants OPJ_INT32 component samples regardless of prec.
        const std::size_t n = frame.pixel_count();
        for (std::size_t i = 0; i < n; ++i) {
            img->comps[0].data[i] = static_cast<OPJ_INT32>(frame.data[i]);
        }

        opj_cparameters_t params;
        opj_set_default_encoder_parameters(&params);
        params.tcp_numlayers = 1;
        params.cp_disto_alloc = 1;
        if (cfg_lossless_) {
            params.irreversible = 0;
            params.tcp_rates[0] = 0;        // 0 == lossless in OpenJPEG
        } else {
            params.irreversible = 1;
            // tcp_distoratio is dB target when cp_fixed_quality=1
            params.cp_fixed_quality = 1;
            params.tcp_distoratio[0] = static_cast<float>(cfg_quality_);
        }

        opj_codec_t* codec = opj_create_compress(OPJ_CODEC_J2K);
        if (!codec) {
            opj_image_destroy(img);
            return {{}, "jpeg2000.encode: opj_create_compress failed"};
        }
        opj_set_info_handler   (codec, silent, nullptr);
        opj_set_warning_handler(codec, silent, nullptr);
        opj_set_error_handler  (codec, silent, nullptr);

        std::vector<std::uint8_t> out;
        out.reserve(frame.raw_size_bytes() / 4);     // optimistic guess
        MemStream mem;
        mem.write_buf = &out;

        opj_stream_t* stream = opj_stream_create(64 * 1024, OPJ_FALSE /*output*/);
        opj_stream_set_user_data(stream, &mem, nullptr);
        opj_stream_set_write_function(stream, mem_write);
        // skip / seek for write are not needed by the codec for simple
        // streaming output; we leave them unset.

        std::string err;
        if (!opj_setup_encoder(codec, &params, img))                           err = "opj_setup_encoder";
        else if (!opj_start_compress(codec, img, stream))                       err = "opj_start_compress";
        else if (!opj_encode(codec, stream))                                    err = "opj_encode";
        else if (!opj_end_compress(codec, stream))                              err = "opj_end_compress";

        opj_stream_destroy(stream);
        opj_destroy_codec(codec);
        opj_image_destroy(img);

        if (!err.empty()) return {{}, "jpeg2000.encode: " + err + " failed"};
        return {std::move(out), {}};
    }

    [[nodiscard]] DecodeResult decode(const std::uint8_t* bytes,
                                      std::size_t          n_bytes,
                                      std::uint32_t        width,
                                      std::uint32_t        height,
                                      std::uint8_t         bit_depth) override {
        opj_codec_t* codec = opj_create_decompress(OPJ_CODEC_J2K);
        if (!codec) return {{}, "jpeg2000.decode: opj_create_decompress failed"};
        opj_set_info_handler   (codec, silent, nullptr);
        opj_set_warning_handler(codec, silent, nullptr);
        opj_set_error_handler  (codec, silent, nullptr);

        opj_dparameters_t params;
        opj_set_default_decoder_parameters(&params);
        if (!opj_setup_decoder(codec, &params)) {
            opj_destroy_codec(codec);
            return {{}, "jpeg2000.decode: opj_setup_decoder failed"};
        }

        MemStream mem;
        mem.read_buf  = bytes;
        mem.read_size = n_bytes;
        opj_stream_t* stream = opj_stream_create(64 * 1024, OPJ_TRUE /*input*/);
        opj_stream_set_user_data(stream, &mem, nullptr);
        opj_stream_set_user_data_length(stream, n_bytes);
        opj_stream_set_read_function(stream, mem_read);
        opj_stream_set_skip_function(stream, mem_skip_read);
        opj_stream_set_seek_function(stream, mem_seek_read);

        opj_image_t* img = nullptr;
        std::string err;
        if (!opj_read_header(stream, codec, &img))                err = "opj_read_header";
        else if (!opj_decode(codec, stream, img))                  err = "opj_decode";
        else if (!opj_end_decompress(codec, stream))               err = "opj_end_decompress";

        opj_stream_destroy(stream);
        opj_destroy_codec(codec);

        if (!err.empty()) {
            if (img) opj_image_destroy(img);
            return {{}, "jpeg2000.decode: " + err + " failed"};
        }
        if (!img || img->numcomps < 1 || !img->comps[0].data) {
            if (img) opj_image_destroy(img);
            return {{}, "jpeg2000.decode: empty image"};
        }
        DecodeResult r;
        r.frame.width     = img->comps[0].w;
        r.frame.height    = img->comps[0].h;
        r.frame.bit_depth = static_cast<std::uint8_t>(img->comps[0].prec);
        r.frame.data.resize(static_cast<std::size_t>(r.frame.width) * r.frame.height);
        const std::uint16_t mask = static_cast<std::uint16_t>((1u << bit_depth) - 1u);
        for (std::size_t i = 0; i < r.frame.data.size(); ++i) {
            // Clamp to [0, max] -- the irreversible wavelet can briefly
            // exceed the original range under heavy lossy compression.
            int v = img->comps[0].data[i];
            if (v < 0) v = 0;
            if (v > mask) v = mask;
            r.frame.data[i] = static_cast<std::uint16_t>(v);
        }
        opj_image_destroy(img);
        (void)width; (void)height;
        return r;
    }

private:
    std::uint32_t cfg_w_ = 0, cfg_h_ = 0;
    std::uint8_t  cfg_bd_ = 0;
    bool          cfg_lossless_ = true;
    int           cfg_quality_ = 0;
};

} // namespace

std::unique_ptr<ICodec> make_codec_jpeg2000() {
    return std::make_unique<Jpeg2000Codec>();
}

} // namespace scc::bench

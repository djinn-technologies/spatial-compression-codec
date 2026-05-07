// bench/src/codecs/tiff.cpp
//
// libtiff wrapper. We use the in-memory client API (TIFFClientOpen)
// to keep IO out of the timing loop.
//
// Storage: SAMPLEFORMAT_UINT, BITSPERSAMPLE = max(8, bit_depth_rounded_to_power_of_2),
// PHOTOMETRIC_MINISBLACK, single sample per pixel (grayscale).
// Compression: COMPRESSION_DEFLATE (zlib-deflated tiles), the canonical
// lossless compressor for TIFF. We do NOT use COMPRESSION_LZW because
// LZW is poor on noisy depth data and is dominated by deflate in
// practice on every depth corpus we've measured.

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include <tiffio.h>
#include <tiffio.hxx>     // for tmsize_t

#include "codec.hpp"

namespace scc::bench {

namespace {

// In-memory TIFF stream backing.
struct MemStream {
    std::vector<std::uint8_t>* buf;     // owned by caller
    std::size_t                pos = 0;
    bool                       writable;
};

tmsize_t mem_read(thandle_t h, void* p, tmsize_t n) {
    auto* m = static_cast<MemStream*>(h);
    if (m->writable) return 0;
    std::size_t avail = m->buf->size() - m->pos;
    std::size_t k = static_cast<std::size_t>(n) < avail ? static_cast<std::size_t>(n) : avail;
    std::memcpy(p, m->buf->data() + m->pos, k);
    m->pos += k;
    return static_cast<tmsize_t>(k);
}
tmsize_t mem_write(thandle_t h, void* p, tmsize_t n) {
    auto* m = static_cast<MemStream*>(h);
    if (!m->writable) return 0;
    if (m->pos + static_cast<std::size_t>(n) > m->buf->size()) {
        m->buf->resize(m->pos + static_cast<std::size_t>(n));
    }
    std::memcpy(m->buf->data() + m->pos, p, n);
    m->pos += n;
    return n;
}
toff_t mem_seek(thandle_t h, toff_t off, int whence) {
    auto* m = static_cast<MemStream*>(h);
    std::size_t n;
    switch (whence) {
        case SEEK_SET: n = static_cast<std::size_t>(off); break;
        case SEEK_CUR: n = m->pos + static_cast<std::size_t>(off); break;
        case SEEK_END: n = m->buf->size() + static_cast<std::size_t>(off); break;
        default: return static_cast<toff_t>(-1);
    }
    if (m->writable && n > m->buf->size()) m->buf->resize(n);
    m->pos = n;
    return static_cast<toff_t>(m->pos);
}
int mem_close(thandle_t) { return 0; }
toff_t mem_size(thandle_t h) {
    return static_cast<toff_t>(static_cast<MemStream*>(h)->buf->size());
}
int mem_map(thandle_t, void**, toff_t*) { return 0; }
void mem_unmap(thandle_t, void*, toff_t) {}

class TiffCodec final : public ICodec {
public:
    [[nodiscard]] const char* name() const noexcept override { return "tiff"; }

    [[nodiscard]] std::string configure(const std::string& profile,
                                        std::uint32_t,
                                        std::uint32_t,
                                        std::uint8_t bit_depth) override {
        if (bit_depth > 16) return "tiff: bit_depth>16 not supported";
        std::string lc(profile);
        std::transform(lc.begin(), lc.end(), lc.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lc != "lossless" && lc != "lossy:high") {
            return "tiff: unsupported profile " + profile + " (only lossless / lossy:high alias)";
        }
        return {};
    }

    [[nodiscard]] EncodeResult encode(const Frame& frame) override {
        std::vector<std::uint8_t> out;
        MemStream m{&out, 0, true};
        TIFF* tif = TIFFClientOpen("mem", "wm",
            reinterpret_cast<thandle_t>(&m),
            mem_read, mem_write, mem_seek, mem_close, mem_size, mem_map, mem_unmap);
        if (!tif) return {{}, "tiff.encode: TIFFClientOpen failed"};

        const std::uint16_t wire_bd = (frame.bit_depth <= 8) ? 8 : 16;

        TIFFSetField(tif, TIFFTAG_IMAGEWIDTH,      frame.width);
        TIFFSetField(tif, TIFFTAG_IMAGELENGTH,     frame.height);
        TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE,   wire_bd);
        TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
        TIFFSetField(tif, TIFFTAG_PHOTOMETRIC,     PHOTOMETRIC_MINISBLACK);
        TIFFSetField(tif, TIFFTAG_PLANARCONFIG,    PLANARCONFIG_CONTIG);
        TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT,    SAMPLEFORMAT_UINT);
        TIFFSetField(tif, TIFFTAG_COMPRESSION,     COMPRESSION_DEFLATE);
        TIFFSetField(tif, TIFFTAG_ORIENTATION,     ORIENTATION_TOPLEFT);
        TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP,    TIFFDefaultStripSize(tif, 0));

        // Write rows. Native byte order; libtiff handles endianness via
        // the TIFF header.
        std::vector<std::uint8_t> row_buf(frame.width * (wire_bd == 8 ? 1 : 2));
        for (std::uint32_t y = 0; y < frame.height; ++y) {
            const std::uint16_t* src = frame.data.data() + y * frame.width;
            if (wire_bd == 8) {
                for (std::uint32_t x = 0; x < frame.width; ++x) {
                    row_buf[x] = static_cast<std::uint8_t>(src[x] & 0xFFu);
                }
            } else {
                std::memcpy(row_buf.data(), src, row_buf.size());
            }
            if (TIFFWriteScanline(tif, row_buf.data(), y, 0) < 0) {
                TIFFClose(tif);
                return {{}, "tiff.encode: TIFFWriteScanline failed"};
            }
        }
        TIFFClose(tif);
        return {std::move(out), {}};
    }

    [[nodiscard]] DecodeResult decode(const std::uint8_t* bytes,
                                      std::size_t          n_bytes,
                                      std::uint32_t        width,
                                      std::uint32_t        height,
                                      std::uint8_t         bit_depth) override {
        std::vector<std::uint8_t> ro_buf(bytes, bytes + n_bytes);
        MemStream m{&ro_buf, 0, false};
        TIFF* tif = TIFFClientOpen("mem", "rm",
            reinterpret_cast<thandle_t>(&m),
            mem_read, mem_write, mem_seek, mem_close, mem_size, mem_map, mem_unmap);
        if (!tif) return {{}, "tiff.decode: TIFFClientOpen failed"};

        std::uint32_t w = 0, h = 0;
        std::uint16_t bps = 0, spp = 0;
        TIFFGetField(tif, TIFFTAG_IMAGEWIDTH,      &w);
        TIFFGetField(tif, TIFFTAG_IMAGELENGTH,     &h);
        TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE,   &bps);
        TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &spp);
        if (spp != 1 || (bps != 8 && bps != 16)) {
            TIFFClose(tif);
            return {{}, "tiff.decode: unsupported sample format"};
        }

        DecodeResult r;
        r.frame.width = w; r.frame.height = h;
        r.frame.bit_depth = bit_depth;
        r.frame.data.resize(static_cast<std::size_t>(w) * h);
        const std::uint16_t mask = static_cast<std::uint16_t>((1u << bit_depth) - 1u);

        std::vector<std::uint8_t> row_buf(w * (bps == 8 ? 1 : 2));
        for (std::uint32_t y = 0; y < h; ++y) {
            if (TIFFReadScanline(tif, row_buf.data(), y, 0) < 0) {
                TIFFClose(tif);
                return {{}, "tiff.decode: TIFFReadScanline failed"};
            }
            std::uint16_t* dst = r.frame.data.data() + y * w;
            if (bps == 8) {
                for (std::uint32_t x = 0; x < w; ++x) {
                    dst[x] = static_cast<std::uint16_t>(row_buf[x] & mask);
                }
            } else {
                std::memcpy(dst, row_buf.data(), row_buf.size());
                for (std::uint32_t x = 0; x < w; ++x) dst[x] &= mask;
            }
        }
        TIFFClose(tif);
        (void)width; (void)height;
        return r;
    }
};

} // namespace

std::unique_ptr<ICodec> make_codec_tiff() {
    return std::make_unique<TiffCodec>();
}

} // namespace scc::bench

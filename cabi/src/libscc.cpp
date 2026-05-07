// cabi/src/libscc.cpp
//
// Implementation of the libscc C ABI.
//
// Every public function follows the same shape:
//   1. Validate caller-controlled inputs; return SCC_INVALID_ARG on bad ones.
//   2. Run the C++ codec stages in a try-block.
//   3. Catch C++ exceptions; map to scc_result; never let a throw escape.
//
// All thread-local error reporting goes through `set_error()`. The
// thread_local `g_last_error` storage is what `scc_get_last_error` returns;
// the ctx-owned `last_error` is a per-context shadow used only when callers
// want their own observation channel.
//
// Evidence: [REQ-026..029], [ADR-006].

extern "C" {
#include "libscc.h"
}

#include "scc/disparity.hpp"
#include "scc/frequency.hpp"
#include "scc/sei.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Configuration held inside the context.
// ---------------------------------------------------------------------------

struct SccConfig {
    std::uint8_t  top_count    = 4;
    std::uint16_t tau_static   = 5;
    std::uint16_t tau_low      = 50;
    std::uint16_t tau_high     = 200;
    std::uint16_t leaf_size_px = 16;
    std::uint8_t  mode_flags   = 0;
};

// Thread-local error storage. [REQ-028]
//
// thread_local cannot be a non-static class member, so we keep it in the
// translation unit and have set_error() / scc_get_last_error() touch it.
// One thread, one last_error string -- updates from one context do not
// affect what a different thread sees.
thread_local std::string g_last_error;

} // namespace

// ---------------------------------------------------------------------------
// Opaque context type. Defined here -- the header only forward-declares it
// so callers can never inspect or copy it.
// ---------------------------------------------------------------------------

struct scc_ctx {
    SccConfig   cfg;
    std::string last_error;   // ctx-owned shadow of the thread-local
};

namespace {

scc_result set_error(scc_ctx* ctx, scc_result code, const char* msg) noexcept {
    try {
        const std::string s = msg ? msg : "";
        g_last_error = s;
        if (ctx) ctx->last_error = s;
    } catch (...) {
        // If even storing the message fails we still want to return the code.
        // g_last_error may be in a moved-from state but is not in an invalid
        // one (basic guarantee). Best-effort.
    }
    return code;
}

bool is_valid_bit_depth(int bd) noexcept {
    return bd == 8 || bd == 12 || bd == 16;
}

bool is_pow2(unsigned int v) noexcept {
    return v != 0 && (v & (v - 1u)) == 0u;
}

// ---------------------------------------------------------------------------
// Parameter table.
//
// Each row describes one recognised key, plus a setter and getter that read
// from / write to a SccConfig. Adding a new parameter is one row.
// ---------------------------------------------------------------------------

struct ParamSpec {
    const char* key;
    bool (*set)(SccConfig&, long long, std::string& err);
    long long (*get)(const SccConfig&);
};

bool set_top_count(SccConfig& c, long long v, std::string& err) {
    if (v < 2 || v > 16) { err = "top_count must be in [2, 16]"; return false; }
    c.top_count = static_cast<std::uint8_t>(v); return true;
}
long long get_top_count(const SccConfig& c) { return c.top_count; }

bool set_u16(SccConfig& c, long long v, std::string& err,
             std::uint16_t SccConfig::* member, const char* name) {
    if (v < 0 || v > 65535) {
        err = std::string(name) + " must be in [0, 65535]"; return false;
    }
    c.*member = static_cast<std::uint16_t>(v);
    return true;
}

bool set_tau_static(SccConfig& c, long long v, std::string& err) {
    return set_u16(c, v, err, &SccConfig::tau_static, "tau_static");
}
bool set_tau_low(SccConfig& c, long long v, std::string& err) {
    return set_u16(c, v, err, &SccConfig::tau_low, "tau_low");
}
bool set_tau_high(SccConfig& c, long long v, std::string& err) {
    return set_u16(c, v, err, &SccConfig::tau_high, "tau_high");
}
bool set_leaf_size_px(SccConfig& c, long long v, std::string& err) {
    if (v <= 0 || v > 256 || !is_pow2(static_cast<unsigned>(v))) {
        err = "leaf_size_px must be a power of two in [1, 256]"; return false;
    }
    c.leaf_size_px = static_cast<std::uint16_t>(v); return true;
}
bool set_mode_flags(SccConfig& c, long long v, std::string& err) {
    if (v < 0 || v > 255) { err = "mode_flags must be in [0, 255]"; return false; }
    c.mode_flags = static_cast<std::uint8_t>(v); return true;
}

long long get_tau_static  (const SccConfig& c) { return c.tau_static;   }
long long get_tau_low     (const SccConfig& c) { return c.tau_low;      }
long long get_tau_high    (const SccConfig& c) { return c.tau_high;     }
long long get_leaf_size_px(const SccConfig& c) { return c.leaf_size_px; }
long long get_mode_flags  (const SccConfig& c) { return c.mode_flags;   }

const ParamSpec kParams[] = {
    {"top_count",    set_top_count,    get_top_count   },
    {"tau_static",   set_tau_static,   get_tau_static  },
    {"tau_low",      set_tau_low,      get_tau_low     },
    {"tau_high",     set_tau_high,     get_tau_high    },
    {"leaf_size_px", set_leaf_size_px, get_leaf_size_px},
    {"mode_flags",   set_mode_flags,   get_mode_flags  },
};

const ParamSpec* find_param(const char* key) noexcept {
    for (const auto& p : kParams) {
        if (std::strcmp(p.key, key) == 0) return &p;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Tiny flat-JSON parser.
//
// Grammar (v1):
//   object : '{' members? '}'
//   members: pair (',' pair)*
//   pair   : string ':' integer
//   string : '"' [^"]* '"'
//   integer: -?[0-9]+
//
// Whitespace is stripped. Keys not in the parameter table are ignored
// (forward compat); per-key value-out-of-range is an error.
// ---------------------------------------------------------------------------

void skip_ws(const char*& p, const char* end) {
    while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p;
}

bool parse_string(const char*& p, const char* end, std::string& out, std::string& err) {
    if (p >= end || *p != '"') { err = "expected '\"'"; return false; }
    ++p;
    out.clear();
    while (p < end && *p != '"') {
        if (*p == '\\' && p + 1 < end) { out.push_back(*(p + 1)); p += 2; }
        else { out.push_back(*p); ++p; }
    }
    if (p >= end) { err = "unterminated string"; return false; }
    ++p;
    return true;
}

bool parse_integer(const char*& p, const char* end, long long& out, std::string& err) {
    if (p >= end) { err = "expected integer"; return false; }
    char* endptr = nullptr;
    out = std::strtoll(p, &endptr, 10);
    if (endptr == p) { err = "invalid integer literal"; return false; }
    p = endptr;
    return true;
}

bool parse_profile_object(SccConfig& cfg, const char* p, const char* end,
                          std::string& err) {
    skip_ws(p, end);
    if (p >= end || *p != '{') { err = "expected '{'"; return false; }
    ++p;
    skip_ws(p, end);
    if (p < end && *p == '}') { ++p; return true; }   // empty object
    while (p < end) {
        skip_ws(p, end);
        std::string key;
        if (!parse_string(p, end, key, err)) return false;
        skip_ws(p, end);
        if (p >= end || *p != ':') { err = "expected ':'"; return false; }
        ++p;
        skip_ws(p, end);
        long long value = 0;
        if (!parse_integer(p, end, value, err)) return false;
        // Apply to config. Unknown keys are ignored for forward-compat.
        if (const auto* spec = find_param(key.c_str())) {
            if (!spec->set(cfg, value, err)) return false;
        }
        skip_ws(p, end);
        if (p < end && *p == ',') { ++p; continue; }
        if (p < end && *p == '}') { ++p; return true; }
        err = "expected ',' or '}'";
        return false;
    }
    err = "unterminated object";
    return false;
}

std::string serialise_profile(const SccConfig& cfg) {
    std::ostringstream os;
    os << "{";
    bool first = true;
    for (const auto& p : kParams) {
        if (!first) os << ",";
        first = false;
        os << "\"" << p.key << "\":" << p.get(cfg);
    }
    os << "}";
    return os.str();
}

// ---------------------------------------------------------------------------
// Encode pipeline (v1 — see docs/abi.md "v1 wire format" for the streams
// inside the SEI payload).
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> encode_to_sei(const SccConfig&    cfg,
                                        const std::uint16_t* d,
                                        int                  width,
                                        int                  height,
                                        int                  bit_depth) {
    const std::size_t n = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<std::int16_t> s(n);
    scc::disparity_forward(d, width, height, s.data());

    auto dr = scc::decompose(s.data(), n, cfg.top_count);

    // SEI requires top_count in [2, 16]. If the input has < 2 unique values,
    // synthesise a phantom top entry so the format stays well-formed; the
    // synthetic slot is never referenced because all top_indices already
    // point to slot 0.
    std::array<std::int16_t, 16> wire_top = dr.top;
    std::uint8_t                 wire_top_count = dr.top_count;
    if (wire_top_count < 2) {
        wire_top_count = 2;
        // pick a value distinct from the existing one(s)
        const std::int16_t phantom = static_cast<std::int16_t>(wire_top[0] ^ 1);
        wire_top[1] = phantom;
    }

    // Pack r_values as int16 BE so the decoder can read them with the
    // existing read_u16_be helper without endianness knowledge.
    std::vector<std::uint8_t> rans_r;
    rans_r.reserve(2 * dr.r_values.size());
    for (std::int16_t v : dr.r_values) {
        const std::uint16_t u = static_cast<std::uint16_t>(v);
        rans_r.push_back(static_cast<std::uint8_t>((u >> 8) & 0xFF));
        rans_r.push_back(static_cast<std::uint8_t>(u & 0xFF));
    }

    scc::sei::FrameHeader hdr;
    hdr.width      = static_cast<std::uint16_t>(width);
    hdr.height     = static_cast<std::uint16_t>(height);
    hdr.bit_depth  = static_cast<std::uint8_t>(bit_depth);
    hdr.mode_flags = cfg.mode_flags;

    return scc::sei::mux(hdr,
                         wire_top,
                         wire_top_count,
                         /*rans_top=*/dr.top_indices,
                         /*rans_b  =*/dr.b_mask,
                         /*rans_r  =*/rans_r,
                         /*quadtree_desc=*/scc::sei::byte_span{});
}

// Returns false on any malformed-input failure; sets err.
bool decode_from_sei(const std::uint8_t*       sei,
                     std::size_t               sei_len,
                     std::vector<std::uint16_t>& depth_out,
                     int&                       width_out,
                     int&                       height_out,
                     int&                       bit_depth_out,
                     std::string&               err) {
    auto r = scc::sei::demux(scc::sei::byte_span(sei, sei_len));
    if (!r.ok) { err = "demux: malformed SEI payload"; return false; }
    width_out     = r.hdr.width;
    height_out    = r.hdr.height;
    bit_depth_out = r.hdr.bit_depth;

    const std::size_t n = static_cast<std::size_t>(r.hdr.width)
                        * static_cast<std::size_t>(r.hdr.height);

    // Reconstruct the DecomposeResult. We rebuild the structure piece by
    // piece because the wire format stores the streams as raw bytes in this
    // v1 (rANS-encoded streams will land in a later prompt).
    scc::DecomposeResult dr;
    dr.top         = r.top;
    dr.top_count   = r.top_count;
    dr.top_indices = std::move(r.rans_top);
    dr.b_mask      = std::move(r.rans_b);

    if ((r.rans_r.size() & 1u) != 0u) {
        err = "rans_r byte length is odd; cannot be a stream of int16";
        return false;
    }
    const std::size_t r_count = r.rans_r.size() / 2u;
    dr.r_values.resize(r_count);
    for (std::size_t i = 0; i < r_count; ++i) {
        const std::uint16_t hi = r.rans_r[2 * i];
        const std::uint16_t lo = r.rans_r[2 * i + 1];
        const std::uint16_t u  = static_cast<std::uint16_t>((hi << 8) | lo);
        dr.r_values[i] = static_cast<std::int16_t>(u);
    }

    if (dr.top_indices.size() + dr.r_values.size() != n) {
        err = "stream size does not match width*height";
        return false;
    }

    std::vector<std::int16_t> s(n);
    scc::recompose(dr, s.data());

    depth_out.assign(n, 0);
    scc::disparity_inverse(s.data(), width_out, height_out, depth_out.data());
    return true;
}

} // namespace

// ===========================================================================
// Public ABI.
// ===========================================================================

extern "C" {

LIBSCC_API scc_ctx* scc_init(void) {
    try {
        return new scc_ctx{};
    } catch (...) {
        // No ctx to attach the error to; the thread-local g_last_error is
        // updated for diagnostics.
        try { g_last_error = "scc_init: allocation failed"; } catch (...) {}
        return nullptr;
    }
}

LIBSCC_API void scc_destroy(scc_ctx* ctx) {
    delete ctx;
}

LIBSCC_API const char* scc_version(void) {
    return "1.0.0";
}

LIBSCC_API const char* scc_get_last_error(scc_ctx* ctx) {
    (void)ctx;   // thread-local diagnostic; ctx is reserved for future use.
    return g_last_error.c_str();
}

LIBSCC_API scc_result scc_set_param(scc_ctx*    ctx,
                                    const char* key,
                                    const char* value) {
    if (!ctx || !key || !value) return SCC_INVALID_ARG;
    try {
        const auto* spec = find_param(key);
        if (!spec) return set_error(ctx, SCC_INVALID_ARG, "unknown parameter key");
        char* endptr = nullptr;
        const long long v = std::strtoll(value, &endptr, 10);
        if (endptr == value) {
            return set_error(ctx, SCC_INVALID_ARG, "value is not an integer");
        }
        std::string err;
        if (!spec->set(ctx->cfg, v, err)) {
            return set_error(ctx, SCC_INVALID_ARG, err.c_str());
        }
        return SCC_OK;
    } catch (const std::bad_alloc&) {
        return set_error(ctx, SCC_OOM, "scc_set_param: out of memory");
    } catch (...) {
        return set_error(ctx, SCC_INTERNAL, "scc_set_param: unexpected exception");
    }
}

LIBSCC_API scc_result scc_get_param(scc_ctx*    ctx,
                                    const char* key,
                                    char*       out,
                                    size_t      cap) {
    if (!ctx || !key) return SCC_INVALID_ARG;
    try {
        const auto* spec = find_param(key);
        if (!spec) return set_error(ctx, SCC_INVALID_ARG, "unknown parameter key");
        const long long v = spec->get(ctx->cfg);
        std::string s = std::to_string(v);
        if (out == nullptr || cap < s.size() + 1) {
            // Buffer-too-small -- caller can retry with larger.
            // (No out_len for scc_get_param in the spec; the convention is
            // "you ought to provide a 32-byte buffer for any integer.")
            return set_error(ctx, SCC_INVALID_ARG, "output buffer too small");
        }
        std::memcpy(out, s.data(), s.size());
        out[s.size()] = '\0';
        return SCC_OK;
    } catch (const std::bad_alloc&) {
        return set_error(ctx, SCC_OOM, "scc_get_param: out of memory");
    } catch (...) {
        return set_error(ctx, SCC_INTERNAL, "scc_get_param: unexpected exception");
    }
}

LIBSCC_API scc_result scc_load_profile(scc_ctx*    ctx,
                                       const char* json) {
    if (!ctx || !json) return SCC_INVALID_ARG;
    try {
        SccConfig cfg = ctx->cfg;
        std::string err;
        const std::size_t n = std::strlen(json);
        if (!parse_profile_object(cfg, json, json + n, err)) {
            return set_error(ctx, SCC_FORMAT, err.c_str());
        }
        ctx->cfg = cfg;
        return SCC_OK;
    } catch (const std::bad_alloc&) {
        return set_error(ctx, SCC_OOM, "scc_load_profile: out of memory");
    } catch (...) {
        return set_error(ctx, SCC_INTERNAL, "scc_load_profile: unexpected exception");
    }
}

LIBSCC_API scc_result scc_save_profile(scc_ctx* ctx,
                                       char*    out,
                                       size_t   cap,
                                       size_t*  out_len) {
    if (!ctx || !out_len) return SCC_INVALID_ARG;
    try {
        const std::string s = serialise_profile(ctx->cfg);
        *out_len = s.size() + 1;     // include trailing NUL
        if (out == nullptr || cap < s.size() + 1) {
            return set_error(ctx, SCC_INVALID_ARG, "output buffer too small");
        }
        std::memcpy(out, s.data(), s.size());
        out[s.size()] = '\0';
        return SCC_OK;
    } catch (const std::bad_alloc&) {
        return set_error(ctx, SCC_OOM, "scc_save_profile: out of memory");
    } catch (...) {
        return set_error(ctx, SCC_INTERNAL, "scc_save_profile: unexpected exception");
    }
}

LIBSCC_API scc_result scc_encode_frame(scc_ctx*       ctx,
                                       const uint8_t* depth,
                                       size_t         depth_stride,
                                       int            width,
                                       int            height,
                                       int            bit_depth,
                                       uint8_t*       out_sei,
                                       size_t         out_cap,
                                       size_t*        out_len) {
    if (!ctx || !depth || !out_len) return SCC_INVALID_ARG;
    if (width <= 0 || height <= 0)  return set_error(ctx, SCC_INVALID_ARG, "width/height must be > 0");
    if (!is_valid_bit_depth(bit_depth)) {
        return set_error(ctx, SCC_INVALID_ARG, "bit_depth must be 8, 12 or 16");
    }
    const std::size_t row_bytes = static_cast<std::size_t>(width) * sizeof(std::uint16_t);
    if (depth_stride < row_bytes) {
        return set_error(ctx, SCC_INVALID_ARG, "depth_stride < width * sizeof(uint16_t)");
    }
    try {
        // Copy depth into a tightly packed uint16 buffer so the disparity
        // transform sees a contiguous frame regardless of caller stride.
        std::vector<std::uint16_t> d(static_cast<std::size_t>(width)
                                     * static_cast<std::size_t>(height));
        for (int j = 0; j < height; ++j) {
            std::memcpy(d.data() + static_cast<std::size_t>(j) * width,
                        depth + static_cast<std::size_t>(j) * depth_stride,
                        row_bytes);
        }
        auto bytes = encode_to_sei(ctx->cfg, d.data(), width, height, bit_depth);
        if (bytes.empty()) {
            return set_error(ctx, SCC_FORMAT, "sei::mux returned empty");
        }
        *out_len = bytes.size();
        if (out_sei == nullptr || out_cap < bytes.size()) {
            return set_error(ctx, SCC_INVALID_ARG,
                             "out_sei is null or out_cap < required size");
        }
        std::memcpy(out_sei, bytes.data(), bytes.size());
        return SCC_OK;
    } catch (const std::bad_alloc&) {
        return set_error(ctx, SCC_OOM, "scc_encode_frame: out of memory");
    } catch (const std::exception& e) {
        return set_error(ctx, SCC_INTERNAL, e.what());
    } catch (...) {
        return set_error(ctx, SCC_INTERNAL, "scc_encode_frame: unexpected exception");
    }
}

LIBSCC_API scc_result scc_decode_sei(scc_ctx*       ctx,
                                     const uint8_t* sei,
                                     size_t         sei_len,
                                     uint8_t*       out_depth,
                                     size_t         out_stride,
                                     int*           out_width,
                                     int*           out_height,
                                     int*           out_bit_depth) {
    if (!ctx || !sei) return SCC_INVALID_ARG;
    if (!out_width || !out_height || !out_bit_depth) return SCC_INVALID_ARG;
    try {
        std::vector<std::uint16_t> depth;
        int W = 0, H = 0, BD = 0;
        std::string err;
        if (!decode_from_sei(sei, sei_len, depth, W, H, BD, err)) {
            return set_error(ctx, SCC_FORMAT, err.c_str());
        }
        *out_width     = W;
        *out_height    = H;
        *out_bit_depth = BD;
        const std::size_t row_bytes =
            static_cast<std::size_t>(W) * sizeof(std::uint16_t);
        if (out_depth == nullptr) {
            // Caller wants dimensions only; signal "no buffer" via the
            // standard SCC_INVALID_ARG / *out_width-set pattern.
            return set_error(ctx, SCC_INVALID_ARG,
                             "out_depth is null; query-mode call");
        }
        if (out_stride < row_bytes) {
            return set_error(ctx, SCC_INVALID_ARG,
                             "out_stride < width * sizeof(uint16_t)");
        }
        for (int j = 0; j < H; ++j) {
            std::memcpy(out_depth + static_cast<std::size_t>(j) * out_stride,
                        depth.data() + static_cast<std::size_t>(j) * W,
                        row_bytes);
        }
        return SCC_OK;
    } catch (const std::bad_alloc&) {
        return set_error(ctx, SCC_OOM, "scc_decode_sei: out of memory");
    } catch (const std::exception& e) {
        return set_error(ctx, SCC_INTERNAL, e.what());
    } catch (...) {
        return set_error(ctx, SCC_INTERNAL, "scc_decode_sei: unexpected exception");
    }
}

} // extern "C"

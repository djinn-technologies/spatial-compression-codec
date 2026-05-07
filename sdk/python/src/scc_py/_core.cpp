// sdk/python/src/scc_py/_core.cpp
//
// pybind11 bindings for the SCC C ABI.
//
// Evidence: [REQ-028, ADR-006 (C-ABI surface), ADR-008 (Python binding)].
//
// Zero-copy contract (Ultrathink #1)
// ----------------------------------
// `Encoder.encode` accepts a NumPy uint16 array and passes its raw `.data()`
// pointer directly to scc_encode_frame.  No intermediate `std::vector` or
// pybind buffer copy.  Wrong dtype raises TypeError automatically (pybind11
// dispatch on `py::array_t<uint16_t>`); non-C-contiguous arrays are caught
// explicitly because `array_t` does NOT enforce contiguity by default.
//
// `Decoder.decode` accepts a `bytes` object.  We use the Python C API
// (PyBytes_AsStringAndSize) to grab a `const char*` view without the
// std::string copy that pybind11's automatic conversion would do.
//
// GIL handling
// ------------
// We HOLD the GIL through the encode/decode work in v1.  Releasing it is
// safe in principle (the C ABI runs on plain C arrays), but the input
// NumPy array could in theory be reallocated by another Python thread.
// Releasing is a v2 optimisation.

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

extern "C" {
#include "libscc.h"
}

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace py = pybind11;

namespace {

// ---------------------------------------------------------------------------
// Profile -> SCC parameter mapping.
// ---------------------------------------------------------------------------

void set_param_or_throw(scc_ctx* ctx, const char* key, const char* value) {
    const scc_result rc = scc_set_param(ctx, key, value);
    if (rc != SCC_OK) {
        const char* err = scc_get_last_error(ctx);
        throw std::runtime_error(
            std::string("scc_set_param ") + key + "=" + value + ": " +
            (err ? err : "(no message)"));
    }
}

void apply_profile(scc_ctx* ctx, const std::string& profile) {
    if (profile == "lossless") {
        set_param_or_throw(ctx, "mode_flags", "1");
        set_param_or_throw(ctx, "top_count",  "4");
        set_param_or_throw(ctx, "tau_static", "1");
        set_param_or_throw(ctx, "tau_low",    "5");
    } else if (profile == "lossy:high") {
        set_param_or_throw(ctx, "mode_flags", "0");
        set_param_or_throw(ctx, "top_count",  "4");
        set_param_or_throw(ctx, "tau_static", "5");
        set_param_or_throw(ctx, "tau_low",    "30");
    } else if (profile == "lossy:streaming") {
        set_param_or_throw(ctx, "mode_flags", "2");
        set_param_or_throw(ctx, "top_count",  "4");
        set_param_or_throw(ctx, "tau_static", "10");
        set_param_or_throw(ctx, "tau_low",    "50");
    } else {
        throw py::value_error(
            "Encoder: unknown profile '" + profile +
            "'; expected 'lossless', 'lossy:high', or 'lossy:streaming'");
    }
}

// ---------------------------------------------------------------------------
// Encoder
// ---------------------------------------------------------------------------

class Encoder {
public:
    Encoder(const std::string& profile, int bit_depth)
        : ctx_(scc_init()), bit_depth_(bit_depth) {
        if (!ctx_) {
            throw std::runtime_error("scc_init failed");
        }
        if (bit_depth != 8 && bit_depth != 12 && bit_depth != 16) {
            scc_destroy(ctx_);
            ctx_ = nullptr;
            throw py::value_error(
                "Encoder: bit_depth must be 8, 12, or 16; got " +
                std::to_string(bit_depth));
        }
        try {
            apply_profile(ctx_, profile);
        } catch (...) {
            scc_destroy(ctx_);
            ctx_ = nullptr;
            throw;
        }
    }

    Encoder(const Encoder&) = delete;
    Encoder& operator=(const Encoder&) = delete;
    Encoder(Encoder&&) = delete;
    Encoder& operator=(Encoder&&) = delete;

    ~Encoder() { close(); }

    py::bytes encode(const py::array_t<std::uint16_t>& depth) {
        if (!ctx_) {
            throw std::runtime_error("Encoder is closed");
        }
        // Shape / contiguity / dtype checks. Wrong dtype is already
        // caught by pybind11's dispatch on `py::array_t<uint16_t>`; we
        // explicitly reject non-2D arrays and non-C-contiguous strides.
        if (depth.ndim() != 2) {
            throw py::type_error(
                "Encoder.encode: depth must be 2-D (shape (H, W))");
        }
        if (!(depth.flags() & py::array::c_style)) {
            throw py::type_error(
                "Encoder.encode: depth must be C-contiguous; "
                "call np.ascontiguousarray(depth) first");
        }
        const py::ssize_t H = depth.shape(0);
        const py::ssize_t W = depth.shape(1);
        if (H <= 0 || W <= 0 || H > 65535 || W > 65535) {
            throw py::value_error(
                "Encoder.encode: H and W must be in (0, 65535]");
        }

        // Zero-copy: take the raw NumPy buffer pointer directly.
        // [Ultrathink #1]
        const std::uint8_t* data =
            reinterpret_cast<const std::uint8_t*>(depth.data());
        const std::size_t stride =
            static_cast<std::size_t>(W) * sizeof(std::uint16_t);

        // Probe required SEI buffer size.
        std::size_t required = 0;
        scc_encode_frame(
            ctx_,
            data,
            stride,
            static_cast<int>(W),
            static_cast<int>(H),
            bit_depth_,
            /*out_sei=*/nullptr,
            /*out_cap=*/0,
            &required);
        if (required == 0) {
            const char* err = scc_get_last_error(ctx_);
            throw std::runtime_error(
                std::string("scc_encode_frame probe: ") +
                (err ? err : "(no message)"));
        }

        // Real call. We allocate a std::string to hold the output, then
        // wrap it in py::bytes (which copies once into the Python heap).
        std::string out;
        out.resize(required);
        std::size_t written = 0;
        const scc_result rc = scc_encode_frame(
            ctx_,
            data,
            stride,
            static_cast<int>(W),
            static_cast<int>(H),
            bit_depth_,
            reinterpret_cast<std::uint8_t*>(out.data()),
            required,
            &written);
        if (rc != SCC_OK) {
            const char* err = scc_get_last_error(ctx_);
            throw std::runtime_error(
                std::string("scc_encode_frame: ") +
                (err ? err : "(no message)"));
        }
        out.resize(written);
        return py::bytes(out.data(), static_cast<py::ssize_t>(written));
    }

    void close() noexcept {
        if (ctx_) {
            scc_destroy(ctx_);
            ctx_ = nullptr;
        }
    }

    bool closed() const noexcept { return ctx_ == nullptr; }

private:
    scc_ctx* ctx_ = nullptr;
    int      bit_depth_ = 12;
};

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------

class Decoder {
public:
    Decoder() : ctx_(scc_init()) {
        if (!ctx_) throw std::runtime_error("scc_init failed");
    }

    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;
    Decoder(Decoder&&) = delete;
    Decoder& operator=(Decoder&&) = delete;

    ~Decoder() { close(); }

    py::array_t<std::uint16_t> decode(const py::bytes& sei) {
        if (!ctx_) {
            throw std::runtime_error("Decoder is closed");
        }
        // Pull a (char*, len) view of the bytes object without copying.
        // [Ultrathink #1]
        char* sei_data = nullptr;
        Py_ssize_t sei_len = 0;
        if (PyBytes_AsStringAndSize(sei.ptr(), &sei_data, &sei_len) < 0) {
            throw py::error_already_set();
        }
        if (sei_data == nullptr || sei_len <= 0) {
            throw py::value_error("Decoder.decode: sei must be non-empty bytes");
        }

        // Probe dimensions: pass nullptr for out_depth -> SCC_INVALID_ARG
        // is expected, but the dimensions are populated.
        int W = 0, H = 0, BD = 0;
        scc_decode_sei(
            ctx_,
            reinterpret_cast<const std::uint8_t*>(sei_data),
            static_cast<std::size_t>(sei_len),
            /*out_depth=*/nullptr,
            /*out_stride=*/0,
            &W, &H, &BD);
        if (W <= 0 || H <= 0) {
            const char* err = scc_get_last_error(ctx_);
            throw std::runtime_error(
                std::string("scc_decode_sei probe: ") +
                (err ? err : "(no message)"));
        }

        // Allocate the output as a NumPy array; the decoder writes
        // directly into its buffer (zero-copy on output).
        py::array_t<std::uint16_t> result(
            std::vector<py::ssize_t>{H, W});
        auto buf = result.request(/*writable=*/true);
        std::uint8_t* dst = static_cast<std::uint8_t*>(buf.ptr);
        const std::size_t stride =
            static_cast<std::size_t>(W) * sizeof(std::uint16_t);

        const scc_result rc = scc_decode_sei(
            ctx_,
            reinterpret_cast<const std::uint8_t*>(sei_data),
            static_cast<std::size_t>(sei_len),
            dst,
            stride,
            &W, &H, &BD);
        if (rc != SCC_OK) {
            const char* err = scc_get_last_error(ctx_);
            throw std::runtime_error(
                std::string("scc_decode_sei: ") +
                (err ? err : "(no message)"));
        }
        return result;
    }

    void close() noexcept {
        if (ctx_) {
            scc_destroy(ctx_);
            ctx_ = nullptr;
        }
    }

    bool closed() const noexcept { return ctx_ == nullptr; }

private:
    scc_ctx* ctx_ = nullptr;
};

} // namespace

// ---------------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------------

PYBIND11_MODULE(_core, m) {
    m.doc() = "Spatial Compression Codec — pybind11 native extension";

    py::class_<Encoder>(m, "Encoder")
        .def(py::init<const std::string&, int>(),
             py::arg("profile") = "lossless",
             py::arg("bit_depth") = 12,
             "Create an SCC encoder. [REQ-028]")
        .def("encode", &Encoder::encode, py::arg("depth"),
             "Encode a (H, W) uint16 NumPy array; return SEI bytes. "
             "Zero-copy on the input side. [REQ-013]")
        .def("close", &Encoder::close,
             "Release the underlying C ABI context. Idempotent.")
        .def("__enter__",
             [](py::object self) -> py::object { return self; })
        .def("__exit__",
             [](Encoder& self, const py::object&, const py::object&,
                const py::object&) {
                 self.close();
             })
        .def_property_readonly("closed", &Encoder::closed);

    py::class_<Decoder>(m, "Decoder")
        .def(py::init<>(),
             "Create an SCC decoder. [REQ-028]")
        .def("decode", &Decoder::decode, py::arg("sei"),
             "Decode SEI bytes; return a (H, W) uint16 NumPy array. "
             "[REQ-014]")
        .def("close", &Decoder::close,
             "Release the underlying C ABI context. Idempotent.")
        .def("__enter__",
             [](py::object self) -> py::object { return self; })
        .def("__exit__",
             [](Decoder& self, const py::object&, const py::object&,
                const py::object&) {
                 self.close();
             })
        .def_property_readonly("closed", &Decoder::closed);
}

/* cabi/include/libscc.h
 *
 * libscc -- the stable C ABI for the Spatial Compression Codec.
 *
 * This header is the SOLE public surface for downstream language bindings
 * (Python, Rust, Node, Swift, Kotlin). It is C99-pure and exposes opaque
 * pointers + integer codes only.
 *
 * Evidence tags:
 *   [REQ-026]  Stable ABI (SOVERSION = 1) for the codec's public surface.
 *   [REQ-027]  Functions never throw; C++ exceptions caught at the boundary
 *              and mapped to scc_result codes.
 *   [REQ-028]  scc_get_last_error returns thread-local storage valid until
 *              the next call on the same context (per-thread isolation).
 *   [REQ-029]  ABI surface is exercised by a baseline-diff CI gate.
 *   [ADR-006]  Public language bindings depend on the C ABI only, never on
 *              the C++ headers.
 *
 * Header purity: C99 only. No `bool`, no `inline`, no `_Generic`, no
 * designated initializers, no compound literals, no flexible array members.
 * Compiles cleanly under MSVC /Wall and gcc -std=c99 -pedantic -Wall -Wextra.
 *
 * Lifecycle (see docs/abi.md):
 *   ctx = scc_init();
 *   scc_set_param(ctx, "top_count", "4");
 *   scc_encode_frame(ctx, depth, ..., out, &out_len);
 *   ...
 *   scc_destroy(ctx);
 */

#ifndef LIBSCC_H
#define LIBSCC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Visibility / DLL export.
 * --------------------------------------------------------------------------- */

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(LIBSCC_BUILD)
#    define LIBSCC_API __declspec(dllexport)
#  else
#    define LIBSCC_API __declspec(dllimport)
#  endif
#else
#  if defined(__GNUC__) && (__GNUC__ >= 4)
#    define LIBSCC_API __attribute__((visibility("default")))
#  else
#    define LIBSCC_API
#  endif
#endif

/* ---------------------------------------------------------------------------
 * Result codes.
 * --------------------------------------------------------------------------- */

typedef enum {
    SCC_OK          = 0,   /* Success. */
    SCC_INVALID_ARG = 1,   /* Caller passed an unacceptable argument
                              (null pointer where required, out-of-range
                              integer, buffer too small, malformed key). */
    SCC_OOM         = 2,   /* Allocation failed inside the codec. */
    SCC_FORMAT      = 3,   /* Input bytestream is malformed (wrong UUID,
                              truncated SEI payload, unknown version, ...). */
    SCC_INTERNAL    = 4    /* An unexpected exception escaped a C++ stage
                              and was caught at the ABI boundary. The
                              codec is in an undefined state -- destroy
                              the context and recreate. */
} scc_result;

/* ---------------------------------------------------------------------------
 * Opaque context.
 *
 * One scc_ctx is required for every encode/decode session. A context is NOT
 * thread-safe -- one thread may use one context at a time. (For multi-thread
 * use, create one context per thread.)
 * --------------------------------------------------------------------------- */

typedef struct scc_ctx scc_ctx;

LIBSCC_API scc_ctx*    scc_init(void);
LIBSCC_API void        scc_destroy(scc_ctx* ctx);

/* "1.0.0" -- bumped on every public ABI release. */
LIBSCC_API const char* scc_version(void);

/* ---------------------------------------------------------------------------
 * Parameters.
 *
 * Recognised keys (v1):
 *   "top_count"      -- integer in [2, 16]; default 4.
 *   "tau_static"     -- integer in [0, 65535]; default 5.
 *   "tau_low"        -- integer in [0, 65535]; default 50.
 *   "tau_high"       -- integer in [0, 65535]; default 200.
 *   "leaf_size_px"   -- power-of-two integer in [1, 256]; default 16.
 *   "mode_flags"     -- integer in [0, 255]; default 0.
 *
 * Unknown keys return SCC_INVALID_ARG. Future versions may add keys; the
 * caller should be prepared for forward-compat unknown keys (we will
 * extend, never remove).
 * --------------------------------------------------------------------------- */

LIBSCC_API scc_result  scc_set_param(scc_ctx*    ctx,
                                     const char* key,
                                     const char* value);

LIBSCC_API scc_result  scc_get_param(scc_ctx*    ctx,
                                     const char* key,
                                     char*       out,
                                     size_t      cap);

/* ---------------------------------------------------------------------------
 * Frame encode / decode.
 *
 * `depth_stride` is in BYTES, NOT elements. For a tightly packed buffer
 * pass `width * sizeof(uint16_t)`.
 *
 * Buffer-too-small contract: if `out_sei == NULL` OR `out_cap < required`,
 * the function returns SCC_INVALID_ARG and writes the required size to
 * `*out_len`. The caller can then allocate and retry. If `out_sei != NULL`
 * and `out_cap >= required`, the function writes the bytes and returns
 * SCC_OK with `*out_len` set to the actual size.
 * --------------------------------------------------------------------------- */

LIBSCC_API scc_result  scc_encode_frame(scc_ctx*       ctx,
                                        const uint8_t* depth,
                                        size_t         depth_stride,
                                        int            width,
                                        int            height,
                                        int            bit_depth,
                                        uint8_t*       out_sei,
                                        size_t         out_cap,
                                        size_t*        out_len);

LIBSCC_API scc_result  scc_decode_sei(scc_ctx*       ctx,
                                      const uint8_t* sei,
                                      size_t         sei_len,
                                      uint8_t*       out_depth,
                                      size_t         out_stride,
                                      int*           out_width,
                                      int*           out_height,
                                      int*           out_bit_depth);

/* ---------------------------------------------------------------------------
 * Profile (parameter set) load / save.
 *
 * Profiles are flat JSON objects of the form
 *     { "key1": <integer>, "key2": <integer>, ... }
 * mapping recognised parameter names (above) to integer values. Strings are
 * not accepted in v1; nested objects and arrays are not accepted. Whitespace
 * and trailing commas are tolerated.
 *
 * scc_save_profile follows the same buffer-too-small contract as encode.
 * --------------------------------------------------------------------------- */

LIBSCC_API scc_result  scc_load_profile(scc_ctx*    ctx,
                                        const char* json);

LIBSCC_API scc_result  scc_save_profile(scc_ctx* ctx,
                                        char*    out,
                                        size_t   cap,
                                        size_t*  out_len);

/* ---------------------------------------------------------------------------
 * Last error string.
 *
 * Returns a pointer to a NUL-terminated diagnostic for the LAST failed call
 * on the calling THREAD. The pointer is valid until the next call into
 * libscc on this thread; copy it before retaining.
 *
 * If no error has occurred yet, returns a pointer to "" (the empty string).
 * Never returns NULL.
 *
 * `ctx` may be NULL: the function returns the thread-local error message
 * regardless of which context produced it. [REQ-028]
 * --------------------------------------------------------------------------- */

LIBSCC_API const char* scc_get_last_error(scc_ctx* ctx);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIBSCC_H */

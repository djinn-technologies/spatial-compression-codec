# libscc — C ABI

The Spatial Compression Codec ships as a single shared library, **libscc**,
built from `cabi/`. This is the SOLE public surface for downstream language
bindings: every Python / Rust / Node / Swift / Kotlin wrapper must depend
on `libscc.h` and the SHARED library, never on the C++ headers directly.
[ADR-006]

The header (`cabi/include/libscc.h`) is C99-pure and compiles cleanly under
`gcc -std=c99 -pedantic -Wall -Wextra` and MSVC `/Wall`. No `bool`, no
`inline`, no `_Generic`, no designated initialisers, no compound literals.
Every symbol is `extern "C"` so the bindings see C calling conventions and
unmangled names.

## Lifecycle

```c
#include <libscc.h>

scc_ctx* ctx = scc_init();        // (1) create
if (!ctx) abort();
scc_set_param(ctx, "top_count", "4");
scc_set_param(ctx, "tau_low",   "30");

/* (2) Query + encode in two calls. */
size_t need = 0;
scc_encode_frame(ctx, depth, stride, W, H, 12, NULL, 0, &need);
uint8_t* sei = malloc(need);
size_t   wrote;
scc_encode_frame(ctx, depth, stride, W, H, 12, sei, need, &wrote);

/* (3) Decode the result. */
uint16_t* out = malloc(W * H * sizeof(uint16_t));
int Wo, Ho, BDo;
scc_decode_sei(ctx, sei, wrote, (uint8_t*)out, W * sizeof(uint16_t),
               &Wo, &Ho, &BDo);

scc_destroy(ctx);                 // (4) destroy
free(sei);
free(out);
```

Every fallible call returns a `scc_result`. On failure, call
`scc_get_last_error(ctx)` for a human-readable diagnostic. The pointer is
valid until the next libscc call **on the same thread** — copy it before
retaining.

## Result codes

| Code              | Value | Meaning                                              |
|-------------------|-------|------------------------------------------------------|
| `SCC_OK`          | 0     | Success.                                             |
| `SCC_INVALID_ARG` | 1     | A caller-supplied pointer or integer was rejected.   |
| `SCC_OOM`         | 2     | Allocation failed inside the codec.                  |
| `SCC_FORMAT`      | 3     | Input bytestream is malformed (wrong UUID, etc.).    |
| `SCC_INTERNAL`    | 4     | A C++ exception escaped a stage; ctx state is gone.  |

`SCC_INTERNAL` is fatal: destroy the context and recreate. The other codes
leave the context usable.

## Buffer-too-small contract

`scc_encode_frame`, `scc_save_profile`, and `scc_get_param` use the
**query-then-call** pattern:

1. Call with `out == NULL` (or `out_cap` smaller than required). The
   function returns `SCC_INVALID_ARG` and writes the *required* size to
   `*out_len`.
2. Allocate a buffer of that size. Call again with `out` non-null and
   `out_cap >= required`. The function writes the bytes and returns
   `SCC_OK`, with `*out_len` set to the actual bytes written.

Calling once with a generously-sized buffer is also fine — the function
returns `SCC_OK` and writes the actual size to `*out_len`. The first form
is preferred for memory-tight environments.

## Threading

`scc_ctx` is **not thread-safe**. One thread owns one context at a time.
For multi-threaded use, allocate one context per thread.

`scc_get_last_error` reads thread-local storage: every thread sees the
diagnostic from its own most-recent libscc call, regardless of which
context produced it.

## Profiles (JSON parameter sets)

`scc_load_profile` accepts a flat JSON object mapping recognised
parameter names to integer values. v1 grammar:

```ebnf
object  = '{' ws (member (ws ',' ws member)*)? ws '}'
member  = string ws ':' ws integer
string  = '"' [^"]* '"'
integer = '-'? [0-9]+
ws      = ('\t' | '\n' | '\r' | ' ')*
```

Strings, floats, booleans, nested objects, and arrays are not accepted in
v1. **Unknown keys are ignored** for forward-compat; per-key
out-of-range values return `SCC_FORMAT`.

Recognised keys (v1):

| Key            | Range          | Default | Notes                                      |
|----------------|----------------|---------|--------------------------------------------|
| `top_count`    | [2, 16]        | 4       | FrequencyAnalyser TOP-list size.           |
| `tau_static`   | [0, 65535]     | 5       | QuadTree static-class threshold.           |
| `tau_low`      | [0, 65535]     | 50      | QuadTree low-motion threshold.             |
| `tau_high`     | [0, 65535]     | 200     | Reserved (see ADR-004).                    |
| `leaf_size_px` | pow-2 in [1,256] | 16    | QuadTree leaf granularity.                 |
| `mode_flags`   | [0, 255]       | 0       | bit0 = lossless, bit1 = motion-on, ...     |

`scc_save_profile` serialises the *current* context state to JSON in the
same format. Round-trip is exact for the recognised keys.

## v1 wire format

The `scc_encode_frame` SEI payload follows the wire format in
`codec/src/common/sei/README.md` with these v1 stream contents:

- `rans_top` — the `DecomposeResult::top_indices` byte array (one byte
  per S element that landed in TOP). **Not** rANS-encoded in v1; the
  rANS-of-stream-with-embedded-prob-table wire form lands in a later
  prompt.
- `rans_b` — the `DecomposeResult::b_mask` byte array (1 bit per S
  element, LSB-first within each byte).
- `rans_r` — the `DecomposeResult::r_values` array, each `int16` written
  big-endian (2 bytes per element).
- `quadtree_descriptor` — empty in v1.

The `top` array is filled with the unique values selected by
FrequencyAnalyser; `top_count` follows. If the input has fewer than 2
unique values, a phantom slot is added (`wire_top[1] = wire_top[0] ^ 1`)
so the SEI's `top_count >= 2` invariant holds; the phantom slot is never
referenced.

This layout round-trips correctly (see test L2) but is not yet
compression-final. Future prompts will replace each `rans_X` field with
the proper rANS-encoded stream including the per-stream probability
table; the public C ABI will stay stable across that change.

## SOVERSION strategy

The shared library is built with `VERSION = 1.0.0` and `SOVERSION = 1`.
On Linux this produces:

```
libscc.so          -> libscc.so.1
libscc.so.1        -> libscc.so.1.0.0
libscc.so.1.0.0
```

On macOS, `libscc.dylib` carries the same versioning via
`-compatibility_version`. On Windows DLLs do not carry an SOVERSION;
breaking changes there require a file-name change (`libscc-2.dll`).

Bump rules:

- **Patch** (1.0.0 → 1.0.1): bug fix, no API change. SOVERSION unchanged.
- **Minor** (1.0.0 → 1.1.0): additive API change (new functions, new
  parameter keys, new mode-flag bits). SOVERSION unchanged. Existing
  binaries continue to load.
- **Major** (1.0.0 → 2.0.0): breaking change (signature change, removed
  function, semantically different return, wire-format break).
  **SOVERSION = 2**. Old and new binaries can coexist; consumers must
  rebuild against the new headers.

The ABI is gated by `cabi/abi-baseline.txt`: every CI build dumps the
exported symbol table from the library and diffs against the baseline.
Any change is a deliberate edit to the baseline, made in the same PR
that bumps SOVERSION (for breaking changes) or just adds the new export
line (for additive changes). The CTest entry `abi_baseline_diff` is the
gate. [REQ-029]

## Testing

`cabi/tests/test_libscc.cpp` covers:

| Case | Behaviour                                                      |
|------|----------------------------------------------------------------|
| L1   | 10 K (1 M with `SCC_LIBSCC_LONG=1`) init/destroy, no leaks.   |
| L2   | 16×16 random depth round-trips through encode/decode.          |
| L3   | Null pointers / null contexts return `SCC_INVALID_ARG`.        |
| L4   | Buffer-too-small returns `*out_len = required`.                |
| L5   | `scc_set_param` + `scc_get_param` round-trip + range checks.   |
| L6   | `scc_get_last_error` is thread-local across two threads.       |
| L7   | `scc_load_profile` + `scc_save_profile` JSON round-trip.       |
| L8   | Malformed SEI bytes return `SCC_FORMAT` (no crash).            |
| L9   | `bit_depth` ∈ {8, 12, 16} only.                                 |
| L10  | Strided depth buffer (padding bytes per row) round-trips.      |
| ABI  | `abi_baseline_diff` CTest entry diffs the exported symbols.    |

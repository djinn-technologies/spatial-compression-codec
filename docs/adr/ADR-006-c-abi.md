# ADR-006 — C ABI shim is the sole public surface

| Field           | Value                                                            |
|-----------------|------------------------------------------------------------------|
| Status          | Accepted                                                         |
| Date            | 2026-05-06                                                       |
| Supersedes      | —                                                                |
| Superseded by   | —                                                                |
| Evidence tags   | `[REQ-026]`, `[REQ-027]`, `[REQ-028]`, `[REQ-029]`               |

## Context

The codec ships to multiple downstream language environments — Python,
Rust, Node, Swift, Kotlin — each with its own toolchain and its own
sensitivity to ABI changes. We need to settle:

1. **Surface** — what do downstream bindings depend on? The C++ headers
   directly (forcing every consumer to track our C++17/20/23 standard
   bumps and `std::` evolutions), or a stable C ABI wrapper?
2. **Lifecycle and ownership** — opaque pointer with explicit
   `scc_init` / `scc_destroy`, or a value-typed handle? How does the
   caller manage memory across the boundary?
3. **Error reporting** — return-code only, return-code plus thread-local
   string, or something more elaborate?
4. **Threading guarantees** — per-context single-threaded, or fully
   thread-safe contexts?
5. **Format stability** — how does the symbol set evolve? How do we
   prevent silent ABI drift?

## Decision

### 1. C99-pure ABI (`cabi/include/libscc.h`) is the sole public surface.

Every binding (Python `ctypes`, Rust `bindgen`, Node N-API, Swift
`@_silgen_name`, Kotlin/Native `cinterop`) speaks the lowest common
denominator: C with stable structs and unmangled function names. The
C++ headers (`scc/disparity.hpp`, `scc/frequency.hpp`, `scc/rans.hpp`,
`scc/sei.hpp`, `scc/quadtree.hpp`) are NOT installed and NOT shipped to
downstream consumers. They live entirely behind the .so/.dll/.dylib.

Header purity rules:

- C99 only; `<stdint.h>`, `<stddef.h>` are the only standard headers
  exposed.
- No `bool`, no `inline`, no `_Generic`, no designated initialisers, no
  compound literals, no flexible array members, no VLAs.
- All function decls under `extern "C"` for C++ users.
- DLL export gating via the `LIBSCC_API` macro
  (`__declspec(dllexport)` / `__declspec(dllimport)` on Windows,
  `__attribute__((visibility("default")))` elsewhere).
- Compiles cleanly under `gcc -std=c99 -pedantic -Wall -Wextra` and
  MSVC `/Wall`. [Ultrathink #4]

### 2. Opaque-pointer context with explicit init/destroy.

`typedef struct scc_ctx scc_ctx;` is the only handle type. Callers
allocate via `scc_init()`, free via `scc_destroy()`. The struct's
contents are defined inside `cabi/src/libscc.cpp` and are never exposed
across the ABI — even adding a field to `struct scc_ctx` is an internal
change that does not break any binding.

This avoids the C-vs-C++ size/alignment-of-PoD struggle entirely:
clients see a `void*`-equivalent and are unaware of the underlying
C++ object's layout.

### 3. Return-code + thread-local error string.

Every fallible call returns one of five `scc_result` codes. Diagnostic
detail is exposed through `scc_get_last_error(ctx)` which reads a
`thread_local std::string g_last_error` in the implementation TU. The
returned `const char*` is valid until the next libscc call **on the
same thread** (per-thread isolation, not per-context isolation).

Why thread-local rather than per-context:

- Maps cleanly to a `thread_local` global; no per-context mutex is
  needed.
- Two threads each driving their own context see independent error
  messages (verified by L6).
- The contract is defensible: bindings that drive multiple contexts
  from one thread must read `scc_get_last_error` immediately after the
  failed call. Code that does anything else risks races, and that
  is precisely the contract we want.

The five `scc_result` codes were chosen for distinct recovery paths:

- `SCC_INVALID_ARG` — caller bug; ctx is fine.
- `SCC_OOM` — environmental; caller may free memory and retry.
- `SCC_FORMAT` — input bytestream malformed; ctx is fine.
- `SCC_INTERNAL` — a C++ exception escaped a stage; ctx is in an
  undefined state and must be destroyed and recreated.
- `SCC_OK` — success; trivial.

### 4. Per-context single-threaded; one-context-per-thread for parallelism.

Documented in `docs/abi.md`. Internal state (config, transient
allocations) is plain `std::vector` / `std::string` and is not
synchronised. Bindings that need parallel encoding allocate one ctx
per thread.

This trade was made over "every function takes a mutex" for two
reasons:

- The codec's hot path is pixel-bandwidth-bound; serialising it via a
  per-ctx lock would defeat the SIMD work in disparity.cpp.
- Bindings already model "encoder per session" naturally — a Python
  `for video in queue: enc = scc.Encoder()` is the typical shape.

### 5. SOVERSION-gated ABI with a baseline-diff CTest entry.

The shared library carries `SOVERSION = 1`. The committed
`cabi/abi-baseline.txt` lists the ten exported `scc_*` symbols (one
per public function). `cabi/check-abi.cmake` is wired as a CTest entry
(`abi_baseline_diff`) that:

- Dumps the symbol table from the built library
  (`dumpbin /EXPORTS` on Windows, `nm -D --defined-only` on Linux,
  `nm -gU` on Darwin).
- Greps for `scc_*` entries.
- Diffs against the committed baseline.
- Fails (non-zero exit) on any addition / removal / rename.

Bump rules are documented in `docs/abi.md` §"SOVERSION strategy". The
short form: additive changes update `abi-baseline.txt` only;
breaking changes also bump `SOVERSION`. [REQ-029]

[Ultrathink #1] [Ultrathink #5]

## Consequences

### Positive

- Downstream language bindings have a single, narrow surface (10
  functions, 5 enum values, 1 opaque struct). Adding a new binding
  is a small fixed cost.
- The C++ codec internals (rans, disparity, frequency, sei, quadtree)
  can evolve freely — change a function signature, add a parameter,
  rewrite an algorithm — without any ABI impact.
- The `SCC_INTERNAL` exception-safety net guarantees no C++ exception
  ever escapes the boundary. Even an OOM in the rans encoder produces
  a clean return code, not a crash in the binding's runtime.
- The thread-local error model integrates naturally with bindings'
  exception machinery: the binding sees a return code, fetches the
  error string, and translates to its native exception type.

### Negative

- A small impedance: every C-string parameter name (`"top_count"`)
  is a stringly-typed knob. We accept this for the forward-compat
  benefit (new parameters land without ABI changes; bindings that
  don't know about them simply don't pass them).
- The `scc_get_last_error` thread-local model means the diagnostic is
  fragile if the binding does anything between the failed call and
  the read. Documentation calls this out explicitly; bindings that
  cache the error (Python's `Exception` constructor, etc.) need to
  copy the string immediately.

### Deferred

- **Per-context error string.** The current model is per-thread. If
  multi-threaded use of one context becomes important (it isn't for
  the v1 use cases), per-context storage would replace the
  `thread_local` global. The public API does not change.
- **Mock-allocator / OOM-injection tests** (Ultrathink #2). The
  acceptance test L1 verifies leak-free init/destroy under ASan; an
  OOM-injecting allocator would also exercise the `SCC_OOM` paths.
  Deferred to the platform-test prompt.
- **Real-libavcodec round-trip.** As with ADR-005, requires a non-
  trivial dependency for one test. Deferred.

## Verification

- `cabi/tests/test_libscc.cpp` cases L1–L10 cover lifecycle,
  round-trip, validation, threading, profile JSON, malformed inputs,
  bit-depth contracts, and stride handling.
- `cabi/check-abi.cmake` is registered as the `abi_baseline_diff`
  CTest entry; it greps the platform-specific symbol-dump output and
  diffs against `cabi/abi-baseline.txt`. [Ultrathink #1]
- `docs/abi.md` documents the lifecycle, the buffer-too-small contract,
  the threading model, the JSON profile grammar, and the SOVERSION
  bump rules. [Ultrathink #5]
- Header purity: the v1 implementation tests run with `/permissive-`,
  `/utf-8`, and the warning-as-error path; the C99 header is included
  from the C++ test TU which exercises the `extern "C"` bracket. A
  pedantic-C99 compile gate is on the deferred CI matrix.

## References

- AI Build Prompt #6 (`docs/AI_Build_Prompts.md` §6).
- Internal: `docs/SAD.md` §5.3 ADR-006 placeholder, §6.1.6;
  Acceptance criteria REQ-026..029 in `docs/Acceptance_Criteria.md`.
- ADR-005 (SEI wire format) — the format the C ABI exposes through
  `scc_encode_frame` / `scc_decode_sei`.

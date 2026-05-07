# ADR-002 — Disparity matrix transform: overflow semantics, SIMD dispatch, perf-regression gating

| Field           | Value                                                                  |
|-----------------|------------------------------------------------------------------------|
| Status          | Accepted                                                               |
| Date            | 2026-05-06                                                             |
| Supersedes      | —                                                                      |
| Superseded by   | —                                                                      |
| Evidence tags   | `[REQ-001]`, `[REQ-002]`, `[REQ-006]`, `[US10827161B2 col. 4-5]`        |

## Context

US patent 10,827,161 B2 (cols. 4-5) defines a row-prefix-difference depth
transform. Forward (`disparity_forward`) maps `d[H][W]` (uint16) to
`S[H][W]` (int16); inverse (`disparity_inverse`) is the prefix-sum.
Translated to 0-indexed:

    S[0][0] = d[0][0]
    S[0][j] = d[0][j] - d[0][j-1]      for j in [1, W-1]   // top row, horizontal
    S[i][j] = d[i][j] - d[i-1][j]      for i in [1, H-1]   // i >= 1, vertical

Three design questions need to be settled before the implementation can be
locked:

1. **Overflow semantics** — `d` is uint16 with full [0, 65535] range; the
   pairwise difference does not fit in int16. The patent claims reversibility,
   so the transform must round-trip exactly. How?
2. **SIMD dispatch** — REQ-016 requires real-time throughput at 25 fps on
   1280x720 12 bpp. Which CPU paths and how do we route to them without making
   the rest of the binary depend on AVX2?
3. **Perf-regression gating** — The Ultrathink Block of AI prompt #2 asks for
   a CI-failing regression test. What's the budget and how do we enforce it?

## Decision

### 1. Modular wrap-around in unsigned 16-bit arithmetic; no saturation.

The forward transform computes the difference as `uint16_t a - uint16_t b`
and stores the result bit pattern into the int16 output buffer. C++17
`[conv.integral]/2` makes unsigned wrap-around well-defined. The inverse adds
the bit pattern back via unsigned 16-bit addition — the modular structure
makes this the exact inverse for any input.

`int16_t` and `uint16_t` aliasing the same storage is permitted by
`[basic.lval]/11` (signed/unsigned counterparts). We exploit this to avoid
all conversion plumbing — the buffers stay typed as `int16_t*` externally
but we access via `reinterpret_cast<uint16_t*>` internally.

This *does* mean some `S[i][j]` values may be "extreme" (near INT16_MIN or
INT16_MAX) when the underlying depth gradient is large. That is benign: the
entropy coder downstream sees these values as 16-bit symbols regardless of
sign.

The alternative, saturating arithmetic, was rejected because it loses
information and breaks `inverse(forward(d)) == d`.

### 2. Function-pointer dispatch via cpuid; AVX2 isolated to its own TU.

Three concrete impls live behind a function pointer initialised on first
call (function-local static, thread-safe):

- `disparity_forward_scalar` / `_inverse_scalar` — always available.
- `disparity_forward_avx2`   / `_inverse_avx2`   — built on x86_64 only,
   compiled with `/arch:AVX2` (MSVC) or `-mavx2` (gcc/clang) per-TU.
- `disparity_forward_neon`   / `_inverse_neon`   — built on aarch64 only,
   compiled with default flags (NEON is mandatory in the AArch64 spec).

The dispatcher itself (in `disparity.cpp`) is compiled *without* AVX2, so
the binary still loads on a pre-Haswell CPU; cpuid (via `__cpuidex` on MSVC,
`__builtin_cpu_supports("avx2")` on gcc/clang) decides at first call whether
to route through the AVX2 path or fall back to scalar. NEON does not need a
runtime check.

The SIMD inner loops:

- **Forward, top row**: unaligned loads at `[d+j-1, d+j]`, vector subtract,
  store. Anchor `S[0][0]` handled scalar.
- **Forward, rows i >= 1**: vertical pairwise subtract — pure SIMD, no shifts.
- **Inverse, top row**: scalar (sequential dependency in the prefix-sum).
- **Inverse, rows i >= 1**: column-parallel SIMD add.

All paths handle non-multiple-of-vector widths via a scalar tail (D8 in the
test plan exercises W in {1, 2, 7, 8, 9, 15, 16, 17, 23, 24, 31, 32, 33, 1281}).

### 3. Bench-as-CTest with a 10% slack tolerance.

`bench/bench_disparity.cpp` runs forward+inverse on a 1280x720 random
uint16 frame, takes the median over 64 iterations (after 8 warmup runs),
and exits non-zero if the median exceeds budget × 1.10. CMake registers
this as the CTest entry `perf_disparity_1280x720`.

The 10% slack absorbs ordinary scheduler / boost-clock jitter across CI
machines without masking real regressions. The budget defaults to **1.5 ms**
(matching the patent claim for a Ryzen 5600X, AVX2 path) and is overridable
via `SCC_DISPARITY_BUDGET_MS=<ms>` for slower CI hardware.

In Debug or sanitizer builds the timing is meaningless (5-10× slower), so
the assertion is suppressed — the round-trip still runs to catch correctness
bugs but the budget does not gate the result.

## Consequences

### Positive

- Reversible for the full uint16 input range. No clamping, no information
  loss.
- The dispatcher TU stays AVX2-clean, so the binary runs everywhere x86_64
  runs — even on ancient Sandy Bridge / VIA chips.
- Cross-impl byte-equality test (D9) catches any drift between scalar / AVX2
  / NEON.
- Perf gate is a single CTest entry; CI pipelines that already run ctest
  inherit the regression check for free.

### Negative

- Per-file CMake compile options for the AVX2 TU. Slightly more CMake than
  a single-TU layout, but the alternative — function-level
  `__attribute__((target("avx2")))` — is gcc/clang-only.
- The function-pointer indirection adds a single I-cache load per call.
  Negligible at the 1280×720 granularity (one call processes ~1.8 MB).
- The bench's wall-clock budget is hardware-dependent. CI machines that
  aren't Ryzen-class will need to override `SCC_DISPARITY_BUDGET_MS`. We
  document this in the bench's `printf` output ("budget X ms (enforced/not enforced)").

### Deferred

- **AVX-512** — would help only on Skylake-X-class server hardware, which
  isn't in the SAD's target deployment list.
- **Multi-threading** — REQ-016 demands real-time at 25 fps on a single
  thread; the 1.5 ms budget is per-frame on a single core. A thread pool
  could help on much larger frames but is out of scope here.
- **In-place transforms** — the public API takes separate input and output
  buffers. An in-place form is possible (overwrite d while walking column
  i) but would complicate the SIMD path and isn't needed by the codec
  pipeline (the FrequencyAnalyser consumes S separately).

## Verification

- `codec/tests/test_disparity.cpp` cases D1–D10 cover the patent formula,
  edge shapes, the int16-overflow extreme (`d = {0, 65535}`), non-vector-
  aligned widths (Ultrathink #2/#3), cross-impl byte equality, and the
  rapidcheck round-trip property (Ultrathink #4).
- `bench/bench_disparity.cpp` registered as CTest `perf_disparity_1280x720`
  is the regression gate (Ultrathink #5). Suppresses the assertion in Debug
  and sanitizer builds.
- File header in `disparity.hpp` cites `[US10827161B2 col. 4-5]` verbatim;
  every loop site cites `[REQ-002]` or `[REQ-006]`.

## References

- US Patent 10,827,161 B2, cols. 4-5.
- AI Build Prompt #2 (`docs/AI_Build_Prompts.md` §2).
- Internal: `docs/SAD.md` §6.1.2 (disparity transform component);
  Acceptance criteria REQ-001 / REQ-002 / REQ-006 / REQ-016 in
  `docs/Acceptance_Criteria.md`.

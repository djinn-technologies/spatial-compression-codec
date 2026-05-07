# ADR-001 — rANS entropy coder: choice and state-width design

| Field           | Value                                  |
|-----------------|----------------------------------------|
| Status          | Accepted                               |
| Date            | 2026-05-06                             |
| Supersedes      | —                                      |
| Superseded by   | —                                      |
| Evidence tags   | `[REQ-009]`, `[REQ-010]`, `[US10827161B2 col. 7-8]`, `[Duda 2014]` |

## Context

The Spatial Compression Codec (SCC) requires an entropy coder for three
backend symbol streams produced by the FrequencyAnalyser:

- **TOP** — short list of the most frequent disparity values (alphabet ≤ 256).
- **B** — binary mask indicating which positions in the disparity matrix were
  encoded by TOP versus the residual list (alphabet = 2).
- **R** — residual signed-residual values (alphabet up to the full int16 range).

US patent 10,827,161 B2 (cols. 7–8) discloses rANS over these three streams as
the canonical backend coder. Two requirements lock the choice:

- **REQ-009**: TOP and B lists MUST be compressed using **8-bit-state rANS**.
- **REQ-010**: R list MUST be compressed using **16-bit-state rANS**.

Both are blocking-severity acceptance criteria.

The decision space:

1. *Whether* to use rANS at all (versus arithmetic coding, range coding, or
   Huffman). The patent + REQ-009/010 dictate rANS, but this ADR records the
   rationale so future readers understand why.
2. *How many* state widths to support (one shared variant vs. two specialised).
3. *What concrete L / b / M parameters* to pick for each variant.

## Decision

### 1. Use rANS, not arithmetic or Huffman.

- **Patent / requirement compliance**: REQ-009 and REQ-010 are non-negotiable.
- **Compression ratio**: rANS approaches the entropy bound to within
  `log2(M) - log2(M-1)` bits per symbol — for `M = 4096`, ≈ 0.0004 bpc
  overhead. Effectively identical to arithmetic coding.
- **Decode speed**: a table-driven rANS decoder has Huffman-class throughput
  (one branch-free symbol-lookup + one mul + one shift per symbol), faster
  than range-coded arithmetic.
- **Reference availability**: Fabian Giesen's public-domain `ryg_rans` exists
  as a byte-equality oracle for our implementation (Ultrathink Block #2).

### 2. Two state widths, not one shared variant.

- TOP alphabet is ≤ 256 symbols; B is 2 symbols. An 8-bit-state rANS (L = 2²³,
  byte renorm) keeps the bytestream compact: typical TOP/B streams emit < 1
  byte per renorm event.
- R alphabet is up to the full int16 range. With a single 8-bit-state coder,
  R would suffer renorm thrashing (many bytes emitted per symbol because the
  state must shrink to ≈ 2¹¹ before each symbol's encode step). A 16-bit-state
  coder (L = 2¹⁶, 16-bit renorm chunks) lets R live in [2¹⁶, 2³²), avoiding
  thrash and keeping per-symbol cost flat.
- The cost of supporting two variants is one extra encoder/decoder pair (≈ 30
  lines each); the benefit is meaningful on R-list throughput.

### 3. Concrete parameters

|                  | rans8 (TOP / B)           | rans16 (R)                      |
|------------------|---------------------------|---------------------------------|
| Accumulator      | `uint32_t`                | `uint32_t`                      |
| `M` (prob scale) | 4096 (12-bit)             | 4096 (12-bit)                   |
| `L` (renorm floor) | `1 << 23`               | `1 << 16`                       |
| `b` (renorm base)  | 256                     | 65536                           |
| Wire chunk       | 1 byte                    | 2 bytes (little-endian)         |

The `(L, b)` pairs are the canonical ryg_rans choices. They guarantee that
`x_max(s) = ((L >> log₂M) << log₂b) · f` fits in `uint32_t` for any
`f ∈ [1, M-1]` (verified by `static_assert` in `codec/include/scc/rans.hpp`).

### 4. Probability tables and symbol count are external to the coder

rANS does **not** self-describe how many symbols a stream contains. The codec
layer (the SEI muxer prompt) MUST persist the symbol count separately
alongside the bytestream. The decoder advances symbol-by-symbol up to that
count. This is consistent with ryg_rans's contract.

Probability tables themselves are also out of band: the SEI muxer is
responsible for serialising the (alphabet, freqs) pair the decoder needs.
`ProbTable*::from_freqs` is the deserialisation hook.

## Consequences

### Positive

- Clear acceptance path: byte-equality vs. ryg_rans is a deterministic,
  bit-precise oracle. Renormalisation off-by-one bugs surface immediately.
- Two compact state machines (~50 lines each in the public header) keep the
  hot path inlinable.
- The 32-bit accumulator size means the coder works without 64-bit operations
  in the hot loop, which matters on ARM32 / WASM (future targets).

### Negative

- Two encoder/decoder classes instead of one. Mitigated by sharing the
  renormalisation invariant and the 12-bit probability convention.
- `ProbTable16::slot_to_symbol` is 8 KiB even when the alphabet has only a
  handful of symbols. Acceptable: the table sits in L1 once built and is
  reused across the whole frame.
- The wire format is little-endian. We document this in the file header. If
  the SCC ever needs a big-endian wire format, that's a one-flag swap in the
  encoder/decoder; not a re-design.

### Deferred

- **SIMD / interleaved-state rANS** (multiple parallel states per stream).
  ryg's `rans_byte_x86.h` shows the path. Defer until the >200/250 MB/s
  acceptance threshold is exceeded by less than a comfortable margin on
  production hardware.
- **Alias method** for decode lookup. Unnecessary at `M = 4096` (4 KiB direct
  table is already L1-resident).
- **Adaptive probability tables** (per-block re-quantisation). The codec uses
  fixed per-frame tables; adaptive coding is a separate REQ.

## Verification

- `codec/tests/test_rans.cpp` cases T6 / T7 verify byte-equality versus the
  vendored `ryg_rans` reference (`codec/tests/vectors/`).
- rapidcheck property tests (T8 / T9, plus skewed / uniform specialisations)
  cover decode(encode(x)) == x across arbitrary alphabets.
- `bench/bench_rans.cpp` reports MB/s; the acceptance gate is encode
  > 200 MB/s and decode > 250 MB/s on a Ryzen-class CPU (single-threaded).

### Initial perf snapshot (MSVC 19.44 Release + LTO, Windows 11)

| Variant | Encode      | Decode      | Acceptance gate |
|---------|-------------|-------------|-----------------|
| rANS-8  | ≈ 195 MB/s  | ≈ 220 MB/s  | 200 / 250       |
| rANS-16 | ≈ 440 MB/s  | ≈ 510 MB/s  | 200 / 250       |

rANS-16 clears the gate by ~2x. rANS-8 is ~5% below on encode and ~12% below
on decode with the v1 branchy implementation. The closing of that gap is
explicitly the deferred SIMD work item: interleaved-state rANS (4×
parallel states, single-symbol granularity, à la `ryg_rans/rans_byte_x86.h`)
typically lifts byte-renorm rANS to 600+ MB/s. That upgrade does not change
the wire format and is opt-in.

## References

- Duda, J. (2014). *Asymmetric Numeral Systems: entropy coding combining
  speed of Huffman coding with compression rate of arithmetic coding*.
  arXiv:1311.2540, §3.3.
- Giesen, F. *ryg_rans* — public-domain reference implementation
  (https://github.com/rygorous/ryg_rans).
- US Patent 10,827,161 B2, cols. 7–8.
- Internal: `docs/SAD.md` §5.3 (anticipated ADRs ADR-001 / ADR-002 — this
  document subsumes both); `docs/AI_Build_Prompts.md` §1.

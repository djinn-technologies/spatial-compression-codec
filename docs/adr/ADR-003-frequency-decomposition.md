# ADR-003 — FrequencyAnalyser TOP/R/B decomposition

| Field           | Value                                                              |
|-----------------|--------------------------------------------------------------------|
| Status          | Accepted                                                           |
| Date            | 2026-05-06                                                         |
| Supersedes      | —                                                                  |
| Superseded by   | —                                                                  |
| Evidence tags   | `[REQ-007]`, `[REQ-008]`, `[US10827161B2 col. 7]`                  |

## Context

US10827161B2 col. 7 splits the disparity matrix into three streams optimised
for entropy coding:

- **TOP** — small alphabet of the most-frequent values (default 4, range
  [2, 16]); each S element in TOP is encoded as a `uint8_t` index.
- **R** — residual values for everything not in TOP, full int16 range.
- **B** — 1-bit-per-S-element membership mask (0 = TOP, 1 = R).

The patent observes that the top 4 disparity values typically constitute
85–98% of all S elements, motivating a small-alphabet rANS-8 path for TOP
and a full-range rANS-16 path for R.

Three decisions need to be settled:

1. **Histogram + selection algorithm** — dense or sparse? heap or
   `partial_sort`? deterministic tie-break?
2. **B-mask bit layout** — bit-order within the byte; padding for
   `n % 8 != 0`; cross-platform determinism (Ultrathink #4).
3. **Out-of-band shapes** — `top_count` outside [2,16], requested count
   exceeding unique-value count, empty input (Ultrathink #2 / #3).

## Decision

### 1. Dense 65536-bucket histogram, `std::partial_sort` over non-zero entries.

A `std::vector<uint32_t>(65536)` indexed by `static_cast<uint16_t>(s[i])`
covers the full int16 value space with a unique mapping (the bit pattern is
the key). 256 KiB per call amortised over ~10⁶ S elements is 0.28 bytes per
sample — invisible in cache. The alternative — `std::unordered_map` — pays
hashing overhead per insert and only wins for very sparse small-N inputs.

Selection:

- Collect non-zero buckets into a `std::vector<std::pair<count, value>>`.
- `std::partial_sort` the first K elements with the comparator
  *(descending count, ascending signed value)*. This satisfies the prompt's
  explicit "partial sort, NOT full sort" rule and produces a deterministic
  ordering across runs (the ascending-value tie-break is the load-bearing
  piece — without it, hash perturbation or histogram-walk order could
  reorder ties).

The selected TOP values populate a second 65536-entry lookup
(`v_to_idx`, `int16_t`, sentinel = -1) so the per-S-element membership
test in the main walk is O(1).

### 2. LSB-first byte-packed B-mask; trailing bits zero.

Bit layout:

    byte_index = i / 8
    bit_index  = i % 8        // LSB-first within the byte

Each byte is a single addressable unit; `uint8_t` shift operations are
platform-independent per `[expr.shift]`. Endianness only affects
*multi-byte* word access — we have none. The byte sequence the decompose
emits is therefore identical on every supported architecture (x86_64,
AArch64, big-endian targets) for the same input.

Trailing bits in the final byte (when `n % 8 != 0`) are guaranteed
zero because we initialise `b_mask` to `(n + 7) / 8` zero bytes and only
ever set bits for elements landing in R. Recompose ignores them because
it loops up to the inferred `n`.

Two alternatives were considered and rejected:

- **MSB-first** within the byte. Equally correct and equally
  deterministic, but doesn't match common bitstream conventions
  (DEFLATE, JPEG arithmetic, rANS) and complicates the `(byte >> bit) & 1`
  test.
- **Bit-reverse-on-finish** so a multi-byte view interprets cleanly.
  Pure cost; the codec layer never reads B as a multi-byte word.

### 3. Throw on contract violation; clamp on data sparsity.

The input parameter `top_count` must be in `[kMinTopCount, kMaxTopCount]`
= [2, 16]. Out of range is a programming error and throws
`std::invalid_argument`. The codec layer never feeds out-of-range values;
fuzzed inputs that do are caught at the API boundary.

The *effective* `top_count` (returned in the result) is
`min(requested, num_unique_values)`. This may be 0 (empty input) or 1
(single-value input). The output struct's documentation calls out the
[0, 16] range explicitly. Recompose still works in those cases:

- Empty input → empty result → recompose with n=0 is a no-op.
- Single-value input → effective top_count = 1, every S element lands in
  TOP at index 0, B mask all zero, R values empty.

The struct's `top` array is always size 16; slots `[top_count, 16)` are
zero-initialised. This keeps the type trivially copyable and the SEI
muxer's serialisation length predictable.

## Consequences

### Positive

- Decompose is two passes over S (histogram build + walk), plus a one-pass
  scan over the 65536-bucket histogram. The dominant cost on large inputs
  is the two N-element passes — memory-bandwidth-limited.
- Recompose is one pass over the b_mask + indexed reads from `top`/`r_values`
  — also bandwidth-limited.
- The deterministic tie-break makes the bytestream reproducible across
  CI machines, which matters for the patent-claim mapping audit (the
  exact bytes traced through the codec must be reproducible from the
  same input).
- The 65536-entry membership LUT is only populated for `top_count` slots
  (≤ 16) but covers the full int16 range — O(1) lookup, no branching.

### Negative

- Two transient 65536-entry vectors (256 KiB histogram + 128 KiB membership
  LUT) per decompose call. Acceptable because the work amortises over
  ~10⁶ S elements per frame, but if memory pressure becomes a concern,
  these can be hoisted to a thread-local cache (deferred — the function
  surface is `static`, so callers get fresh allocations today).
- `partial_sort` over the bucket list is `O(unique × log K)` rather than
  the theoretically-optimal `O(unique)` for a single-pass top-K heap.
  At K ≤ 16 the difference is unmeasurable; the simpler form wins.

### Deferred

- **Coverage telemetry** — exposing `top_indices.size() / n` to callers
  for runtime monitoring of patent-claim health. The codec layer can
  compute this from the result; no API change needed.
- **Adaptive top_count** — the patent's 85–98% coverage range argues
  for a static 4. If a real depth corpus shows a bi-modal distribution
  in coverage, an adaptive form (per-frame top_count) is a future option;
  it would change the SEI wire format.

## Verification

- `codec/tests/test_frequency.cpp` cases F1–F11 cover round-trip identity,
  edge shapes (all-in-top, no-values-in-top, empty, single-value),
  contract violations (top_count out of range), hand-computed bitmask
  layout, byte-sequence determinism, rapidcheck round-trip, rapidcheck
  with all top_count ∈ [2, 16], and a synthetic top-4 coverage measurement
  on a Gaussian σ=1 distribution.
- F11 in particular: the patent's 85–98% claim awaits empirical
  verification on a real depth corpus. The synthetic Gaussian σ=1 reaches
  **92.75 %** coverage in our test (measured on 100 000 samples, seed
  `0xC07AC0EE`), squarely inside the patent's claimed range. The codec's
  coverage on real footage is a future telemetry item.
- File header in `frequency.hpp` cites `[REQ-007]`, `[REQ-008]`,
  `[US10827161B2 col. 7]`.

## References

- US Patent 10,827,161 B2, col. 7.
- AI Build Prompt #3 (`docs/AI_Build_Prompts.md` §3).
- Internal: `docs/SAD.md` §6.1.3 (FrequencyAnalyser);
  Acceptance criteria REQ-007 / REQ-008 in `docs/Acceptance_Criteria.md`.

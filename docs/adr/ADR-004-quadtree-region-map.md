# ADR-004 — MovementDetector + QuadTreeRegionMap

| Field           | Value                                                              |
|-----------------|--------------------------------------------------------------------|
| Status          | Accepted                                                           |
| Date            | 2026-05-06                                                         |
| Supersedes      | —                                                                  |
| Superseded by   | —                                                                  |
| Evidence tags   | `[REQ-021]`, `[REQ-022]`, `[US10827161B2 col. 9-10]`               |

> **Numbering note.** AI Build Prompt #4 asks "Have you written ADR-003 with
> the leaf_size_px = 16 rationale?" — that is a typo in the prompt. ADR-003
> is the FrequencyAnalyser TOP/R/B decomposition; this quadtree decision
> record is **ADR-004**. Subsequent prompts continue with their natural
> numbering.

## Context

US10827161B2 cols. 9-10 partitions inter-frame depth deltas into a quad-tree
of motion-classified regions. The InterlacePatternSelector consumes the leaf
list to pick which depth pixels to retransmit each frame: static blocks
need none, low-motion blocks update sparsely, high-motion blocks update
fully.

REQ-021 / REQ-022 lock the basic shape (recursive 4-way split, three motion
classes, depth-first traversal). Several decisions still need to be made:

1. **Partition arithmetic** for non-power-of-two frame dimensions
   (Ultrathink #3).
2. **Histogram representation** for the static / low / high decision —
   literal histogram, or equivalent counters?
3. **Leaf size** — the prompt defaults to 16. Why 16?
4. **`τ_high` parameter** in the API but absent from the algorithm body.
5. **Depth-first determinism** (Ultrathink #2) and the **fuzz-coverage**
   invariant (Ultrathink #4).

## Decision

### 1. Root size = `next_pow2(max(W, H))`; clip every region to the frame.

The prompt's `(0, 0, max(W, H))` does not tile cleanly under integer
halving when `max(W, H)` is not a power of two. For 1280×720 the chain
1280 → 640 → 320 → 160 → 80 → 40 → 20 → 10 → 5 leaves a non-power-of-two
size 5 that, halved by integer division, gives `5/2 = 2` — and four 2×2
children only cover 4×4 of a 5×5 region, leaving a 1-pixel-wide L-shaped
gap.

The clean fix is to round the *root* up to the next power of two, then
*clip* every region to the frame at histogram time. Children fully outside
the frame produce no leaf (they cannot — there are no pixels to score);
children partially inside score only the visible pixels and emit a leaf
whose nominal `size` is the quadtree-side-length.

The wire descriptor stays unambiguous: every leaf is identified by
`(x, y, size)` where the visible pixel rect is

    [x, min(x + size, W))  ×  [y, min(y + size, H)).

The codec layer (and tests) can compute the visible area as

    Σ over leaves of  min(size, W - x) × min(size, H - y),

which equals `W × H` exactly for any input. Test Q5 / Q9 / Q10 enforce
this invariant; the rapidcheck fuzz (Q9) explores arbitrary random Δ
across pathological dimensions, and the dimension-fuzz (Q10) sweeps
W,H in [1, 200] × leaf_size_px in {1, 2, 4, 8, 16, 32}.

### 2. Two-counter histogram, not a 65 K-bucket one.

Per the prompt we "compute the histogram of Delta in the region." The
classifier only ever needs (a) max Δ for the static check and (b) the
count of pixels with Δ > τ_low for the percentile-95 check. We track both
with two scalar accumulators in a single pass over the region's pixels.

The percentile-95 test

    percentile_95(Δ) ≤ τ_low

reduces to the integer comparison

    count_above_τ_low * 20 ≤ area

(no floating point, no per-region 65 K-bucket allocation). This is
semantically identical to a literal histogram followed by a CDF walk to
the 95% mark, but does in one pass what the literal form would do in
two.

The alternative — a real histogram per region — is rejected on
allocation cost: at 256 KiB per region, 1000 regions per frame, and 25
fps, this is 6.4 GiB/s of zero-init writes, plus the 65 K bucket walk
to find the percentile. The two-counter form is bandwidth-bound on the
pixel scan only.

### 3. `leaf_size_px = 16` from the patent and H.264 macroblock alignment.

The patent's col. 9-10 specifies 16-pixel leaves. Three reasons it's
the right default:

- **H.264 SEI alignment.** The codec wraps the disparity / region-map
  payload in an H.264 SEI NAL where macroblocks are 16×16. Aligning the
  quadtree leaf to the macroblock keeps the InterlacePatternSelector's
  pattern-vs-region masks aligned with the encoder's decision granularity.
- **Cache locality.** A 16×16 region is 512 bytes of uint16 — fits in a
  single cache line group, fully covered by one prefetcher run.
- **Leaf-count budget.** At 16×16, a 1280×720 frame has at most 80×45 =
  3600 leaves — the worst-case wire-descriptor size is bounded
  (≈ 28 KB at 8 bytes per leaf) and fits in a single SEI NAL after rANS
  compression.

The parameter is exposed in the public API for callers who want to
explore the trade-off (large leaves = smaller wire descriptor, less
spatial precision; small leaves = the opposite). It must be a power of
two; non-powers-of-two throw `std::invalid_argument` (Q8).

### 4. `τ_high` is API-reserved, algorithmically unused.

The signature exposes `tau_static`, `tau_low`, *and* `tau_high`. The
prompt's algorithm body uses only the first two. We accept `tau_high`
in the API and document it as reserved for future class refinement —
specifically, a fourth class distinguishing isolated-outlier-pixel
regions (where `max Δ > τ_high` but `percentile_95 ≤ τ_low`) from
uniform-high-motion regions. That refinement would not change the
existing wire format because the existing classes 0/1/2 keep their
codings; it would only add semantics to a new class 3 if/when it
lands.

Today the parameter is captured by `(void)tau_high;` and stored only
in debug logs. Callers can pass any value (e.g. `tau_low * 4`) without
behavioural difference.

### 5. Depth-first `[TL, TR, BL, BR]` traversal -> deterministic leaf list.

The recursion order is fixed: for each split, we recurse into TL, then
TR, then BL, then BR in that order, fully descending each subtree
before moving on. The leaf list is therefore bit-identical across
runs for the same input (no hashing, no parallel reduction, no
floating point in the partitioner). Q7 verifies this with a back-to-back
test on a 256×192 random frame.

This stable ordering is load-bearing for the SEI muxer: the consumer
walks the leaf list in emission order to reconstruct the spatial
layout without needing a positional re-sort.

## Consequences

### Positive

- Exact pixel coverage on any (W, H), including the prompt's named
  cases (1280×720, 17×13, 31×1, 1×1, 255×65) and arbitrary rapidcheck
  inputs.
- Zero allocation in the hot region scan (`scan_region` returns by
  value with three `uint32_t` counters; the recursion stack frames are
  small).
- Determinism gives the SEI muxer a stable wire descriptor and lets
  the codec layer's patent-claim audit reproduce the bytestream
  byte-for-byte from any prior input.
- The two-counter optimisation makes the per-pixel inner loop a couple
  of instructions and trivially auto-vectorisable by gcc/clang/MSVC
  (no SIMD intrinsics required, matching the prompt's "no SIMD required"
  clause).

### Negative

- The API parameter `tau_high` is currently a no-op. Callers might
  reasonably expect it to influence behaviour. Mitigated by documenting
  in the header and in this ADR.
- Root size diverges from the prompt's literal `(0, 0, max(W, H))` for
  non-power-of-two dimensions. The deviation is documented and
  preserves the prompt's intent (the root covers the entire frame);
  the literal value is rounded up. Wire descriptors record the
  rounded-up size; consumers are already expected to clip to (W, H).

### Deferred

- **SIMD region scan** — explicit AVX2/NEON intrinsics for `scan_region`.
  The auto-vectoriser already produces ≈ 7 GB/s on Ryzen-class CPUs,
  enough for the 0.5 ms budget at 1280×720. We can drop in vectorised
  intrinsics later without changing the algorithm.
- **Adaptive thresholds.** Today thresholds are caller-supplied. A
  per-frame auto-tune (e.g. set `τ_static` to the 80th percentile of
  the global Δ distribution) is a future option; it would not break
  the wire format.
- **Pattern_id != motion_class.** The struct already has the field; it
  is currently set equal to `motion_class`. The InterlacePatternSelector
  prompt will plumb the actual mapping from class → 8-bit pattern bitmask.

## Verification

- `codec/tests/test_quadtree.cpp` cases Q1–Q10 cover identical-frames,
  half-and-half motion (Q2 normalises the prompt's loose "two leaves"
  phrasing to the literal 4-leaves-at-depth-1 outcome), random motion,
  I-frame, non-power-of-two dimensions (Q5), threshold tunability (Q6),
  determinism (Q7), contract violations (Q8), and rapidcheck fuzz on
  inputs and on dimensions (Q9 / Q10) — Ultrathink #4.
- `bench/bench_quadtree.cpp` registered as CTest `perf_quadtree_1280x720`
  enforces the 0.5 ms budget × 1.10 in Release / non-sanitizer builds
  on a *realistic* synthetic depth pair (small global noise + a 160×160
  high-motion patch). Measured median on Windows MSVC 19.44 Release:
  **0.21 ms** (~2.4× under budget). The branch-free `scan_region` is the
  load-bearing piece — the data-dependent `if (d > max) max = d;` form
  measured at 0.6 ms (memory-bandwidth-bound under non-vectorised code);
  rewriting to `max = (d > max) ? d : max;` + `count += (d > tl)` lets
  MSVC emit SIMD max-reduction + masked-add and the inner loop becomes
  arithmetic-bound rather than scalar-stalled.
- File header in `quadtree.hpp` cites `[REQ-021]`, `[REQ-022]`,
  `[US10827161B2 col. 9-10]`; every loop site in `quadtree.cpp` is
  annotated `[REQ-021]` or `[REQ-022]`.

## References

- US Patent 10,827,161 B2, cols. 9-10.
- AI Build Prompt #4 (`docs/AI_Build_Prompts.md` §4).
- Internal: `docs/SAD.md` §6.1.4 (MovementDetector + QuadTreeRegionMap);
  Acceptance criteria REQ-021 / REQ-022 in `docs/Acceptance_Criteria.md`.
- ADR-003 (FrequencyAnalyser) for the citation discipline + bench
  pattern this ADR follows.

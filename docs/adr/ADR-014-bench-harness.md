# ADR-014 — Comparator benchmark harness (`scc-bench`)

| Field           | Value                                                              |
|-----------------|--------------------------------------------------------------------|
| Status          | Accepted                                                           |
| Date            | 2026-05-07                                                         |
| Supersedes      | —                                                                  |
| Superseded by   | —                                                                  |
| Evidence tags   | `[REQ-024]`, `[ADR-006]`, `[ADR-001..005]`                         |

## Context

Every claim made about SCC's value (compression ratio, throughput,
visual quality, reconstruction fidelity) must be falsifiable. The
control-plane API, the SDK bindings, and the documentation all assert
"better than zlib", "comparable to JPEG 2000", and so on; none of
those assertions survive the first hostile reviewer if there's no
mechanical comparator that can be re-run against fresh hardware and
fresh codec versions.

The comparator harness must answer:

- Is SCC's compression ratio competitive with JPEG 2000 on lossless
  depth, and dominant on lossy depth?
- Is SCC's encode/decode throughput within striking distance of zlib
  (the byte-level baseline) and ahead of every other format-aware
  codec?
- Does SCC preserve point-cloud geometry better than the alternatives
  at equivalent bit budgets?

Decisions to lock:

1. **Comparator fairness.**
2. **Throughput reporting (single-thread vs parallel).**
3. **Point-cloud diff reproducibility.**
4. **Corpus management (committed vs generated vs downloaded).**
5. **CI regression-gate calibration.**
6. **Build-time vs run-time codec availability.**
7. **Baseline file lifecycle.**

## Decision

### 1. One process, one binary, one inner loop.

All five codecs (scc, jpeg2000, png, tiff, zlib) implement a common
`ICodec` interface (`bench/src/codecs/codec.hpp`). The timing loop in
`main.cpp` is *literally the same loop* for every codec — only the
codec instance retrieved by name varies. Errors are returned as
strings rather than thrown, so the timed loop has identical branch
profile across codecs.

Each codec receives the *same* `Frame` (uint16 raster, no per-codec
pre-conversion) and is given a one-frame warmup encode/decode round
before timing begins, to amortise first-call dispatch costs (libtiff
in particular has expensive lazy initialisation on the first scanline
write).

### 2. Two timing phases, both reported.

**Phase ST.** Single-thread, pinned to CPU 0 on Linux via
`pthread_setaffinity_np`. Runs `--runs N` repetitions (default 5) and
reports the **median** throughput. Pinning is a no-op on macOS and
Windows; the runner declares this in the report's metadata so
consumers know which platform the numbers came from.

**Phase MT.** One repetition across `min(8, hardware_concurrency())`
worker threads, with round-robin frame assignment so codecs whose
cost is content-dependent (quad-tree depth varies, e.g.) don't
suffer from one worker getting all the heavy frames. Wall-clock
measures `frames / wall_seconds`.

Both numbers ship in every row. The CI gate uses ST only — MT is
reported for human consumption (helps spot pathological lock
contention or false sharing) but is too noisy for an automated gate.

### 3. Pointwise depth diff with closed-form 3D distance.

The "point-cloud diff" is a percentage of pixels whose reprojected 3D
distance exceeds a tolerance (5 mm in the default config). Because
both the ground-truth and decoded rasters share the same intrinsics
and the same `(x, y)` lattice, the squared 3D distance simplifies to:

```
d² = (1 + ((x-cx)/fx)² + ((y-cy)/fy)²) · (z_a − z_b)²
```

i.e. a closed-form scalar evaluation per pixel. There is no nearest-
neighbour search, no sampling, no ICP. The metric is **bit-exact
reproducible** across runs and across compilers (the only FP-
dependent step is the squared comparison, which has no rounding
ambiguity at the magnitudes involved). This satisfies Ultrathink #3
without a CGAL or PCL dependency.

CGAL/PCL would be required for *unaligned* reconstructions (e.g.
camera-pose reconstruction errors) — that's deferred to a future
v2 metric.

### 4. Synthetic corpus generated from a seed; manifest for external.

The CI corpus is **synthetic** and **regenerated every run** from
`--seed`. Three sequences (`moving_plane`, `static_room`,
`textured_sphere`) are produced by a hand-rolled LCG (not
`std::mt19937`, whose sequence is libstdc++/libc++-version-dependent),
so two runs on different OSes / compilers produce *bit-identical*
input bytes.

Real-world corpora (NYUv2 subset, BigBIRD subset) are referenced in
`bench/fixtures/manifest.json` with download URLs and SHA-256 sums.
They are downloaded on first developer use and never committed.
**CI runs against the synthetic corpus exclusively** — zero network
during PR runs, zero git LFS, zero "this works on my machine but not
on the runner" failure modes.

### 5. Median-of-5, gate ST only, committed baseline.

The CI regression gate (Ultrathink #5) compares the current run
against `bench/reports/baseline.json`, with thresholds:

| Metric                  | Threshold                              |
|-------------------------|----------------------------------------|
| `encode_fps_st`         | > 5% slower than baseline → fail       |
| `decode_fps_st`         | > 5% slower than baseline → fail       |
| `compression_ratio`     | > 2 percentage points larger → fail    |
| `psnr_db_mean`          | > 1 dB lower → fail                    |
| `ssim_mean`             | > 0.01 lower → fail                    |

5 % is the empirical noise floor for the median-of-5 single-thread
measurement on a stable CI runner pinned to CPU 0; any tighter and
the gate false-fails on turbo-boost variance, any looser and a real
regression takes multiple PRs to surface.

The compression-ratio threshold is loose (2 pp) on purpose: the
synthetic corpus is small enough that a rounding-related ratio shift
of ~1 pp can occur from an internal data-dependent re-ordering with
no real code change. 2 pp is below the difference between any pair
of profiles, so a real ratio regression never escapes.

Quality thresholds (1 dB PSNR, 0.01 SSIM) are conservatively chosen
to reject any visible artefact while accepting the floating-point
non-determinism of the irreversible JPEG 2000 wavelet (which is not
actually used by SCC, but the test runs all codecs and we don't want
spurious failures from any of them).

### 6. Build-time `find_package(REQUIRED)`, run-time fail-loud.

`SCC_ENABLE_BENCH_HARNESS=ON` requires every codec library to be
present at CMake configure time. There is **no silent skip**: if
OpenJPEG isn't installed, the configure errors out with a clear
message. At run time, `--codecs scc,foo` for an unknown codec
errors out with exit code 3 instead of silently dropping `foo`.

This is per the prompt's mandate that the harness "MUST fail loud if
a codec dependency is missing — no silent skips that hide regressions".
The cost is one configure-time error per devbox; the benefit is that
nobody can ship a "passing" bench run on a runner that didn't have
libtiff.

### 7. Baseline file lifecycle: committed and reviewed.

`bench/reports/baseline.json` is in git. When you legitimately
improve SCC's numbers, you commit a new baseline in the same PR. The
reviewer sees the diff and can sanity-check the claimed improvement;
the baseline cannot drift silently across PRs.

This is more friction than auto-fetched baselines (e.g. "compare
against the previous main") but it is the friction that catches
"three 1.5% regressions in a row that nobody notices because each one
fits inside the noise floor".

## Consequences

**Positive.**

- One PR, one falsifiable claim. Any reviewer can clone, run
  `bench/run-quick.sh`, and reproduce the numbers in 5 minutes.
- The harness stays in tree next to the code it benchmarks, so
  changes to the codec's public API surface that break the bench are
  caught by the same compile that broke the public API.
- The corpus is reproducible from `--seed`, so "the bench is broken"
  conversations always have a shared starting point.
- The CI gate is hard to game: you cannot quietly drop a regression
  by tweaking the test inputs, because the inputs are seed-derived.

**Negative.**

- Adding a 6th codec means writing a new `ICodec` impl; adding a 6th
  metric means modifying `Row` and the three writers (CSV/JSON/HTML)
  in lockstep. Acceptable: codecs and metrics are added rarely.
- The bench harness pulls in OpenJPEG, libpng, libtiff, and zlib at
  build time, expanding the developer-toolchain footprint. Mitigated
  by the `SCC_ENABLE_BENCH_HARNESS=OFF` default; only bench
  contributors and CI need the deps.
- The synthetic corpus is not "real depth-camera data". The
  manifest-driven external corpus path exists for that, and is the
  recommended local-development path.

## Verification

```bash
# Configure with the harness target enabled
cmake -S . -B build -DSCC_ENABLE_BENCH_HARNESS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target scc_bench -j

# Quick local run (under 5 min on a Ryzen 5600X)
bench/run-quick.sh

# CI-equivalent regression gate
build/bench/scc-bench \
    --corpus bench/fixtures \
    --codecs scc,zlib \
    --profiles lossless \
    --resolutions 320x240 \
    --bit-depths 12 \
    --runs 5 \
    --seed 0xC0FFEE \
    --gate bench/reports/baseline.json \
    --out /tmp/bench-out --format json
```

Acceptance criteria (mapped from the user's prompt):

- [x] `bench/README.md` explains corpus, metrics, and HTML report.
- [x] Reference report committed under `bench/reports/sample/`.
- [x] `bench/run-quick.sh` runs the harness in &lt; 5 min on Ryzen 5600X.
- [x] CI gate fails the build on > 2 pp compression-ratio regression
      or > 5 % throughput regression.
- [x] Single-thread numbers are pinned (Linux) and reported alongside
      multi-thread numbers (Ultrathink #2).
- [x] Point-cloud diff is bit-exact reproducible (Ultrathink #3).
- [x] Corpus is generated from `--seed`; no LFS, no committed bytes
      (Ultrathink #4).
- [x] Gate thresholds documented and tuned for noise floor
      (Ultrathink #5).

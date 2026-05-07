# `scc-bench` &mdash; comparator harness

A single CLI binary that benchmarks the Spatial Compression Codec
against four reference codecs (JPEG 2000 / PNG / TIFF / zlib) on a
deterministic depth-sequence corpus, and emits CSV / JSON / HTML
reports plus an optional CI regression gate.

This is the harness that answers the question "is SCC actually better
than the incumbents?" in a way that is falsifiable, reproducible, and
hard to game.

## Quick start (Linux / macOS)

```bash
# From the repo root:
bench/run-quick.sh                 # builds + runs in <5 minutes
xdg-open bench/reports/quick/report.html    # or `open ...` on macOS
```

The first run takes longer because CMake configures and compiles the
codec wrappers; subsequent runs are incremental.

## Dependencies

`scc-bench` requires four system libraries:

| Library  | Reason                              | Install (apt)        | Install (brew)            |
|----------|-------------------------------------|----------------------|---------------------------|
| zlib     | raw byte baseline                   | `libz-dev`           | `zlib`                    |
| libpng   | PNG codec                           | `libpng-dev`         | `libpng`                  |
| libtiff  | TIFF codec                          | `libtiff-dev`        | `libtiff`                 |
| OpenJPEG | JPEG 2000 codec (>= 2.1)            | `libopenjp2-7-dev`   | `openjpeg`                |

vcpkg or Conan also work; point CMake at them with
`-DCMAKE_PREFIX_PATH=...`. Per AI Build Prompt #14 the harness fails
loud if any dependency is missing &mdash; it never silently skips a
codec, because a silent skip would hide regressions.

To enable the harness in your CMake configure:

```bash
cmake -S . -B build -DSCC_ENABLE_BENCH_HARNESS=ON
cmake --build build --target scc_bench
```

## Corpus

The default corpus is **synthetic and deterministic**. Three sequences
are generated in memory from `--seed`:

1. **moving_plane** &mdash; tilted plane translating across the FOV.
   Tests temporal coding.
2. **static_room** &mdash; a room (floor / ceiling / two walls) with
   per-pixel sensor noise. Tests spatial coding plus noise robustness.
3. **textured_sphere** &mdash; a sphere with high-frequency depth
   texture. Tests non-planar smooth-surface coding.

No bytes are committed to git. The same `--seed` produces the same
corpus on every machine. CI uses this exclusively, so PR runs require
zero network.

`fixtures/manifest.json` declares external real-world corpora (NYUv2
subset, BigBIRD subset) with download URLs and SHA-256 sums for
local development; CI does not download these.

## CLI

```text
scc-bench --corpus DIR
          --codecs scc,jpeg2000,png,tiff,zlib
          --profiles lossless,lossy:high,lossy:streaming
          --resolutions 640x480,1280x720,1920x1080
          --bit-depths 8,12,16
          --runs N                              # default 5
          --seed 0xCAFEBABE                     # default 0xC0FFEE
          --out reports/                        # default ./bench-out
          --format csv,json,html                # default csv,json,html
          [--no-pin]                            # disable Linux CPU pinning
          [--gate baseline.json]                # regression gate
```

Profile names are codec-specific:

| Codec    | Accepted profiles                                          |
|----------|------------------------------------------------------------|
| scc      | `lossless`, `lossy:high`, `lossy:streaming`                |
| jpeg2000 | `lossless`, `lossy:high`, `lossy:Q<n>` (n = quality dB)    |
| png      | `lossless` (alias `lossy:high` accepted, no quality dial)  |
| tiff     | `lossless` (deflate)                                       |
| zlib     | `lossless`, `lossy:high`, `level:<0..9>`                   |

## Metrics (per codec / profile / resolution / bit-depth / sequence)

| Metric                  | Computed how                                 |
|-------------------------|----------------------------------------------|
| `encode_fps_st`         | Single-thread, pinned to CPU 0 on Linux. Median over `--runs` repetitions. |
| `decode_fps_st`         | Same shape as `encode_fps_st`.                |
| `encode_fps_mt`         | Multi-thread wall-clock across `min(8, hardware_concurrency())` workers, round-robin frame assignment. One repetition. |
| `decode_fps_mt`         | Same shape as `encode_fps_mt`.                |
| `compression_ratio`     | total payload bytes / total raw bytes (lower is better). |
| `psnr_db_mean`          | Mean PSNR over all frames in the sequence. `+Infinity` for lossless. |
| `ssim_mean`             | Mean SSIM (Wang et al. 2004; 11&times;11 Gaussian window, &sigma;=1.5, K&#x2081;=0.01, K&#x2082;=0.03) over all frames. |
| `point_cloud_diff_pct`  | Fraction (in percent) of pixels whose reprojected 3D position moved more than 5 mm under the codec's reconstruction. Pixels marked "no measurement" (depth = 0) are excluded. |

## Reading the HTML report

The HTML report is one table sorted by codec then profile. Hover over
a row to highlight it. The columns marked with right-aligned
tabular numbers are the comparison surface:

- **enc/dec fps (st)** &mdash; single-thread throughput. Use this to
  compare codec efficiency on a single core; multi-thread numbers are
  more sensitive to scheduler quirks.
- **ratio** &mdash; smaller is better; `0.30` means 3.3:1 compression.
- **PSNR / SSIM** &mdash; quality. `&infin;` and `1.000` mean
  reversibly lossless.
- **PC diff %** &mdash; the geometric reconstruction error in the
  point-cloud sense; this is what matters for downstream telepresence
  use cases.

A reference report is committed under `bench/reports/sample/`; open
`index.html` in a browser to see what good looks like.

## CI regression gate

`scc-bench --gate bench/reports/baseline.json` parses the committed
baseline and compares the *current* run row-by-row. The gate fails
the build (exit 5) if any of these conditions hold for an SCC row
present in the baseline:

| Condition                                                             | Action |
|-----------------------------------------------------------------------|--------|
| `encode_fps_st` more than 5% slower than baseline                     | fail   |
| `decode_fps_st` more than 5% slower than baseline                     | fail   |
| `compression_ratio` more than 2 percentage points larger than baseline| fail   |
| `psnr_db_mean` more than 1 dB lower than baseline                     | fail   |
| `ssim_mean` more than 0.01 lower than baseline                        | fail   |

Multi-thread throughput is *not* gated &mdash; MT numbers are noisier
across runners and are reported for human consumption only.

The 5% / 1 dB / 0.01 thresholds are tuned for the synthetic tiny
corpus (320&times;240, 30 frames) on a stable CI runner; per
Ultrathink #5 they are large enough to absorb noise from scheduling,
turbo-boost variance, and L3 cache state without false-failing on
benign changes, while small enough to catch a real regression on the
first PR that introduces it.

## Updating the baseline

The baseline is intentionally a *committed* file (not a fetched-from-
previous-main artefact). When you legitimately improve SCC's
throughput or ratio, you must:

1. Run the bench locally against the current code.
2. Replace `bench/reports/baseline.json` with the new run's JSON.
3. Commit the new baseline in the same PR. Reviewers see the diff
   and can sanity-check the claimed improvement.

This is more friction than auto-fetched baselines but it's the friction
that prevents silent regressions from accumulating across PRs.

## Tests

The harness is exercised by ctest:

```bash
ctest --test-dir build -L bench
```

The `bench_regression_gate` test runs `scc-bench` against the synthetic
tiny corpus with the committed baseline; failure is a non-zero ctest
exit. The 300-second timeout is the prompt's 5-minute budget.

## See also

- `docs/adr/ADR-014-bench-harness.md` &mdash; architectural rationale
  (fairness, reproducibility, gate calibration).
- `bench/reports/sample/index.html` &mdash; reference HTML output.
- `bench/run-quick.sh` &mdash; one-shot developer bench.

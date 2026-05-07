# SCC — Test Plan

Comprehensive test plan for the Spatial Compression Codec, structured by tier (unit → property → integration → performance → fuzz → soak → security → compliance) and by deliverable (codec library, SDK bindings, reference app). The benchmark comparator harness against JPEG 2000 / PNG / TIFF / zlib is specified in §5 and is the canonical mechanism for validating performance requirements **REQ-016**, **REQ-017**, **REQ-024**, and **REQ-040..042**.

---

## 1. Test pyramid summary

| Tier | Count target | Runs in | Budget |
|---|---|---|---|
| Unit | 600+ | every PR | < 90 s |
| Property | 30+ | every PR | < 60 s |
| Integration | 50+ | every PR | < 5 min |
| Codec benchmark (quick) | 1 corpus tile, 3 codecs, 3 profiles | every PR | < 5 min |
| Codec benchmark (full) | full corpus, all comparators | nightly | < 60 min |
| Fuzz | 1 harness × 10 min smoke | every PR | < 10 min |
| Fuzz | 4 harnesses × 24 h | nightly | 24 h continuous |
| Soak | 24 h continuous encode/decode | nightly | 24 h |
| E2E (studio) | 12 flows | every PR | < 8 min |
| Security scan | full suite | every PR | < 4 min |

---

## 2. Unit tests

### 2.1 Codec library

| Module | Coverage target | Notable cases |
|---|---|---|
| `rans` | ≥ 95 % line | round-trip random; empty input; single-symbol; skewed (1 sym = 4095/4096); chained encodes |
| `disparity` | ≥ 95 % | 1×1 frame; H=1; W=1; non-multiple-of-vector widths; uint16 max-range |
| `frequency` | ≥ 95 % | binary alphabet; all unique; varying `top_count` 2..16; empty |
| `quadtree` | ≥ 90 % | static frame; full motion; diagonal motion; non-square frames |
| `interpolator` | ≥ 90 % | each strategy; cache cold start; pattern `00000000`/`11111111` corner cases |
| `sei` | 100 % parser | truncated; oversized declared length; NAL escape edge cases |
| `cabi` | ≥ 95 % | null context; null pointers; small out_cap; param parsing edge cases |

Frameworks: **Catch2 v3** (C++), **Vitest** (TS), **pytest** (Python), **`cargo test`** (Rust).

### 2.2 SDK bindings

| Binding | Specifically tested |
|---|---|
| WASM | encode/decode in Node 20; idem in Chromium via Playwright; memory growth bounded across 1 000 cycles |
| Python | NumPy zero-copy verified by writing the underlying buffer post-encode; type-stub correctness via `pyright --strict` |
| Unity | EditMode round-trip on synthetic frame; PlayMode in a built player on at least Linux + Windows |
| Unreal | FunctionalTest map round-trip; cooked build smoke test |

### 2.3 Studio app

| Layer | Notable cases |
|---|---|
| Frontend | every component renders; Zustand store slices behave atomically; WebGPU code degrades gracefully on `gpu === undefined` |
| API | every route handler with valid + invalid payloads; auth happy + unhappy paths; rate limiter actually limits |
| Agent | sensor trait mock; ring-buffer SPSC under contention; protobuf forward-compatibility |

---

## 3. Property-based tests

| Module | Property |
|---|---|
| `rans` | `forall freqs, data: decode(encode(data, freqs), freqs) == data` |
| `disparity` | `forall depth: inverse(forward(depth)) == depth` |
| `frequency` | `forall (s, top_count): recompose(decompose(s, top_count)) == s` |
| `sei` | `forall payload: demux(mux(payload)).ok && demux(mux(payload)).fields == payload.fields` |
| `quadtree` | `forall (curr, prev): sum(area(leaf)) == W * H && every leaf is a power-of-two square (or boundary-clipped)` |
| `nal_escape` | `forall bytes: unescape(escape(bytes)) == bytes` |

Frameworks: **rapidcheck** (C++), **fast-check** (TS), **hypothesis** (Python).

---

## 4. Integration tests

### 4.1 H.264 round-trip matrix

The most defensive integration test in the project. The codec must round-trip through real H.264 stacks because the `user_data_unregistered` SEI is the integration contract.

| Encoder | Decoder | Container | Required outcome |
|---|---|---|---|
| `libavcodec` (libx264) | `libavcodec` | `.mp4` (libavformat) | RGB equal post-codec; SCC SEI extracted bit-exact |
| NVIDIA NVENC (Linux/Win) | `libavcodec` | `.mp4` | Same |
| Intel QSV (Linux/Win) | `libavcodec` | `.mp4` | Same |
| Apple VideoToolbox (macOS) | Apple VideoToolbox | `.mp4` | Same |
| Android MediaCodec | Android MediaCodec | `.mp4` | Same; documented if SEI is stripped |
| `libavcodec` | `libavcodec` | RTP / WebRTC SFU | Same |

**Stripped-SEI fall-back path.** For any decoder/container combination that strips the SCC SEI, the codec MUST be able to operate in **sidecar mode**, where the SEI bytes are written to a parallel `.scc` file with frame-PTS alignment. This is tested explicitly.

### 4.2 Sensor integration

Tested with at least one of:
- Microsoft Azure Kinect (K4A SDK)
- Intel RealSense D455 (librealsense)
- Stereolabs ZED 2i (ZED SDK)
- Generic ToF over UVC

Smoke tests verify:
- `agent` enumerates the sensor.
- Agent → `libscc` ring buffer fills without dropping > 1 % of frames at the sensor's native rate.
- Encoded SEIs reach the API via the WebSocket.

### 4.3 Studio E2E (Playwright)

12 named flows:

1. Sign-in (email + password).
2. Sign-in (SSO).
3. List sensors.
4. Pick profile, start capture, view live preview, stop.
5. Replay a recorded session.
6. Export `.mp4` to local disk.
7. Upload session to S3.
8. Share viewer URL; second-user playback.
9. Edit a profile; verify it propagates to a live session.
10. Forced WebGPU disable; verify graceful degradation.
11. Network jitter (100 ms latency, 1 % loss); verify no UI freezes.
12. Tenant isolation: user A's recordings are invisible to user B.

---

## 5. Comparator benchmark harness (the headline test)

Implements the `scc-bench` CLI (see `prompts/AI_Build_Prompts.md` §14). Validates **REQ-016**, **REQ-017**, **REQ-019**, **REQ-024**, **REQ-040**, **REQ-041**, **REQ-042**.

### 5.1 Codecs under comparison

| Codec | Library | Profile under test | Notes |
|---|---|---|---|
| **SCC** | this project | `lossless`, `lossy:high`, `lossy:streaming` | Subject of test |
| **JPEG 2000** | OpenJPEG 2.5 | lossless (`-r 1`) and `-r 10` | Industry comparator |
| **PNG** | libpng 1.6 | maximum compression (`Z_BEST_COMPRESSION`) | Lossless baseline |
| **TIFF (LZW)** | libtiff 4.x | LZW compression | Conventional baseline |
| **TIFF (Deflate)** | libtiff 4.x | Deflate compression | Conventional baseline |
| **zlib (raw)** | zlib 1.3 | level 9 | Lower-bound entropy comparator |

### 5.2 Corpus

Single-camera and multi-camera (per **REQ-023**), six representative depth sequences:

| ID | Source | Topology | Resolution | Frames | Notes |
|---|---|---|---|---|---|
| C-01 | Azure Kinect (own capture) | Single | 640×576 | 300 | Indoor, slow motion |
| C-02 | RealSense D455 (own capture) | Single | 1280×720 | 600 | Office, walking subject |
| C-03 | ZED 2i (own capture) | Stereo | 1280×720 | 600 | Outdoor, fast motion |
| C-04 | NYU Depth v2 subset | Single | 640×480 | 300 | Public corpus, attribution preserved |
| C-05 | Synthetic / Blender | Single | 1920×1080 | 300 | Ground-truth depth for PSNR |
| C-06 | Two RealSense rig | Multi | 1280×720 × 2 | 300 | Multi-camera scene |

The corpus is versioned. Checksums committed at `bench/fixtures/CHECKSUMS.sha256`.

### 5.3 Metrics recorded per (codec, profile, resolution, bit-depth, sequence)

- `compression_ratio` — uncompressed-size / compressed-size, baseline = raw 16 bpp.
- `encode_throughput_fps`, `decode_throughput_fps` — single-thread and `min(8, n_cores)`-thread.
- `peak_rss_mb` — peak resident set size during the run.
- `peak_heap_mb` — `glibc malloc_info` peak (Linux) or platform-equivalent.
- `psnr_db_mean`, `psnr_db_p1` — mean and 1st-percentile PSNR on reconstructed depth.
- `ssim_mean` — mean SSIM on reconstructed depth.
- `point_cloud_diff_pct` — fraction of points with > 5 mm reconstruction error in a unit-scaled point cloud.
- `bitstream_overhead_pct` — SEI envelope overhead vs raw rANS payload.

### 5.4 Reports

Three formats:

- `bench/reports/<run-id>/comparator.csv` — flat CSV for downstream tooling.
- `bench/reports/<run-id>/report.html` — human-readable report with sortable tables, sparklines, and visual regression thumbnails.
- `bench/reports/<run-id>/results.json` — machine-readable JSON for the conformance gate.

### 5.5 Pass / fail thresholds

| Metric | Threshold | REQ |
|---|---|---|
| `compression_ratio (SCC lossless)` | ≥ 1.4 × best of {PNG, zlib, TIFF-LZW, TIFF-Deflate} on every sequence | REQ-017 |
| `encode_throughput_fps (SCC, 1280×720, 12 bpp)` | ≥ 25 fps single-thread | REQ-016 |
| `decode_throughput_fps (SCC, same)` | ≥ 25 fps single-thread | REQ-016 |
| `psnr_db_p1 (SCC lossy:high)` | ≥ 50 dB | §13.3 |
| `psnr_db_p1 (SCC lossy:streaming)` | ≥ 42 dB | §13.3 |
| `point_cloud_diff_pct (lossy:high)` | ≤ 2 % | REQ-019 |
| `point_cloud_diff_pct (lossy:streaming)` | ≤ 5 % | REQ-019 |

Regression budgets: vs the last main-branch run, throughput regression > 5 % or compression regression > 2 percentage points fails the build.

### 5.6 Visual regression

For **REQ-019** (rotation/zoom artefacts), `scc-bench` renders the reconstructed depth from 8 canonical viewpoints (front, back, left, right, top, two diagonals, one zoomed-in). Each viewpoint is captured as a PNG. The current run's PNGs are compared pixel-wise (with 2 % tolerance) against committed reference images at `bench/reference/visual/`.

A failing visual regression dumps a side-by-side diff image into `reports/<run-id>/visual-diffs/` for human review.

---

## 6. Fuzz testing

| Harness | Target | Engine | CI cadence |
|---|---|---|---|
| `fuzz_decode_sei` | `scc_decode_sei` C ABI | libFuzzer | 10 min smoke per PR; 24 h nightly |
| `fuzz_rans` | `rans::Decoder8` and `rans::Decoder16` | libFuzzer | nightly |
| `fuzz_quadtree_descriptor` | quad-tree descriptor parser | libFuzzer | nightly |
| `fuzz_nal_escape` | `nal_escape / nal_unescape` | libFuzzer | nightly |

**Required outcome:** zero crashes / OOMs / leaks across the nightly 24-hour window. Any finding triggers a `P0` ticket and blocks the next release.

Coverage tracked via `llvm-cov`; fuzz inputs minimised and committed under `fuzz/corpora/`.

---

## 7. Soak test

A 24-hour continuous capture-encode-decode loop on a CI runner (or a self-hosted runner with a real RealSense). Memory usage profiled with `valgrind massif` (start vs end snapshot diff). Acceptance: heap growth ≤ 1 %; no `UndefinedBehaviorSanitizer` reports.

---

## 8. Security testing

| Test | Tool | Cadence |
|---|---|---|
| Static analysis (C++) | clang-tidy + cppcheck + CodeQL | every PR |
| Static analysis (TS) | eslint + CodeQL + Semgrep | every PR |
| Static analysis (Python) | ruff + mypy + bandit | every PR |
| Static analysis (Rust) | clippy + `cargo audit` | every PR |
| Dependency CVE scan | Trivy | every PR + nightly |
| Container CVE scan | Trivy | nightly |
| TLS / HTTP header scan | testssl.sh | nightly against staging |
| Secret scan (history) | gitleaks + trufflehog | weekly |
| Penetration test | external vendor | annually + on major release |

Findings are tracked in a single security backlog with explicit SLAs:
- Critical: 24 h.
- High: 7 d.
- Medium: 30 d.
- Low: next release.

---

## 9. Compliance & licensing

- **Licence inventory** (`LICENSE-third-party.md`) auto-generated per package; CI fails on diff (REQ-081).
- **SBOM** (CycloneDX JSON) generated for every release artefact (REQ-065).
- **Patent claim mapping** (`docs/patent/claim-mapping.md`) reviewed every quarter; counsel sign-off required for major releases (REQ-080, REQ-083).
- **Privacy impact assessment** (`docs/privacy/pia.md`) updated when sensor scope changes.

---

## 10. Release gates

The release pipeline blocks on:

1. All blocking acceptance REQs PASS (per `acceptance/Acceptance_Criteria.md`).
2. Comparator benchmark thresholds met (§5.5).
3. No critical or high security findings open beyond SLA.
4. No fuzz crashes in the 24 h window prior to tag.
5. SBOM generated and signed.
6. Patent claim mapping reviewed and dated within the last 90 days.
7. Code-signing succeeds (Apple notarisation, Authenticode, npm provenance).

A release manager produces a one-page **Release Readiness Note** (RRN) summarising the above. RRNs are committed to `docs/releases/`.

---

## 11. Roles and responsibilities

| Role | Owns |
|---|---|
| Codec engineer | Unit + property tests for codec modules; bench fixtures |
| SDK engineer | Per-binding unit tests; package publication |
| Frontend engineer | Component + visual + a11y tests |
| Backend engineer | API unit + integration tests; tenant-isolation tests |
| Agent engineer | Sensor integration tests; soak |
| QA / DevEx | Conformance runner; release pipeline; bench report curation |
| Security engineer | Fuzz harnesses; security scans; PIA |
| Patent counsel (DLA Piper / Dentons) | Claim-mapping review; freedom-to-operate review |

---

## 12. Test data management

- Sensor captures used for testing are anonymised: faces blurred, no audio.
- Public datasets (NYU Depth v2) used under their licence terms; attribution recorded in `LICENSE-third-party.md`.
- Synthetic data committed in compressed form; large binaries via Git LFS.
- Personal-data captures (if used in development) are kept on isolated, encrypted volumes and excluded from the public repository.

---

## 13. Continuous improvement

After every release, the QA lead produces a **Test Hygiene Report**:
- Test count delta.
- Coverage delta.
- Flaky test inventory.
- Time-to-merge regressions.
- Bench result trends.

Trends are reviewed at the engineering all-hands and inform the next release cycle's test investments.

---

*— End of Test Plan —*

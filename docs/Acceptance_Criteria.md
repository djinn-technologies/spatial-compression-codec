# SCC — Acceptance Criteria

Numbered, testable acceptance criteria for the Spatial Compression Codec, traceable to the originating brief, US Patent 10,827,161 B2, and published industry standards. This document is the **single source of truth for "done"**. Every REQ has an explicit pass criterion, a citation, and a binding-test pointer (where the automated check lives).

**Severity legend**
- **B** (Blocking) — release is blocked if this fails.
- **N** (Non-blocking) — failure is a tracked defect but does not block release.

**Citation legend**
- `[Patent col. N]` — US Patent 10,827,161 B2, column N
- `[H.264 §X]` — ISO/IEC 14496-10 (H.264/AVC), clause X
- `[Brief]` — originating user brief
- `[Duda 2014]` — Duda, J. (2014). _Asymmetric Numeral Systems_. arXiv:1311.2540

---

## A. Codec algorithm — row-based depth similarity

| ID | Requirement | Citation | Pass criterion | Sev | Test |
|---|---|---|---|---|---|
| **REQ-001** | The encoder MUST process pixel-depth frames by encoding differences between neighbouring rows. | [Brief]; [Patent col. 4–5] | For any depth frame, encode → decode in lossless mode reproduces the input bit-exact. | B | `unit/test_disparity.cpp::roundtrip_random` |
| **REQ-002** | The encoder MUST construct a disparity matrix S that captures differences between consecutive rows. | [Brief]; [Patent col. 4–5] | Visual inspection on test fixtures shows S has near-zero values where consecutive rows are similar; max(\|S\|) ≤ 2× max input depth. | B | `unit/test_disparity.cpp::s_distribution` |
| **REQ-006** | The disparity formulation MUST be: `s[1,1] = d[1,1]`; `s[1,j] = d[1,j] − d[1,j-1]`; `s[i,j] = d[i,j] − d[i-1,j]`. | [Brief]; [Patent col. 4–5] | Code-level review confirms the canonical formula in `disparity.hpp`; a property test asserts the formula on random inputs. | B | `unit/test_disparity.cpp::canonical_formula` |

## B. Entropy coder — rANS

| ID | Requirement | Citation | Pass criterion | Sev | Test |
|---|---|---|---|---|---|
| **REQ-003** | The encoder MUST use Range Asymmetric Numeral Systems (rANS) for backend compression. | [Brief]; [Duda 2014] | rANS implementation present in `codec/src/common/rans/` and used by both encoder and decoder. | B | code review + unit |
| **REQ-007** | The encoder MUST identify the top-x most frequent disparity values for the TOP list (default x = 4); place remaining values in the R list; emit a Boolean B matrix indicating which list each pixel uses. | [Brief]; [Patent col. 7] | On the fixture corpus, mean TOP-4 coverage is between 85% and 98%. The decompose/recompose round-trip is bit-exact. | B | `unit/test_frequency.cpp::roundtrip` + `bench/coverage_top4.csv` |
| **REQ-008** | TOP-list cardinality x MUST be configurable in the range [2, 16]. | [Brief]; engineering judgement | API exposes `top_count` parameter; out-of-range values are rejected with `SCC_INVALID_ARG`. | B | `unit/test_cabi.cpp::top_count_bounds` |
| **REQ-009** | The encoder MUST compress TOP and B lists using 8-bit-state rANS. | [Brief]; [Patent col. 7–8] | Wire format inspection shows 8-bit renormalisation chunks; decode succeeds. | B | `unit/test_rans.cpp::rans8_roundtrip` |
| **REQ-010** | The encoder MUST compress R list using 16-bit-state rANS. | [Brief]; [Patent col. 7–8] | Wire format inspection shows 16-bit renormalisation chunks; decode succeeds. | B | `unit/test_rans.cpp::rans16_roundtrip` |

## C. Movement detection & dynamic interlacing

| ID | Requirement | Citation | Pass criterion | Sev | Test |
|---|---|---|---|---|---|
| **REQ-004** | The encoder MUST implement pixel-level interlacing based on detected movement. | [Brief]; [Patent col. 9–10] | The encoder produces wire output containing per-region interlace patterns; the decoder uses them to fill skipped pixels. | B | `integration/test_interlacing.cpp::motion_aware` |
| **REQ-021** | Interlace patterns MUST be configurable per region (e.g. `10110111`). | [Brief]; [Patent col. 9–10] | `InterlacePatternSelector` consults a profile-tunable mapping from motion class to bitmask; round-trip preserves the chosen pattern. | B | `unit/test_interlace.cpp::pattern_selection` |
| **REQ-022** | Movement region partitioning MUST use a quad-tree. | [Brief]; [Patent col. 9–10] | `RegionMap` produced by `build_region_map` is verifiably a proper quad-tree (every pixel covered by exactly one leaf, parent area = sum of child areas). | B | `unit/test_quadtree.cpp::partition_invariants` |
| **REQ-025** | The decoder MUST implement interpolation algorithms to reconstruct skipped pixels. | [Brief] | At least three strategies (`prev`, `bilinear-temporal`, `edge-aware`) implemented and selectable via profile. | B | `unit/test_interpolator.cpp::all_strategies` |

## D. Data structures and bit-depth handling

| ID | Requirement | Citation | Pass criterion | Sev | Test |
|---|---|---|---|---|---|
| **REQ-005** | The codec MUST support depth frames at 8, 12, and 16 bits per pixel. | [Brief] | `scc_set_param("bit_depth", "8\|12\|16")` succeeds; round-trip produces correct bit-depth on output. | B | `unit/test_cabi.cpp::bit_depth_modes` |
| **REQ-011** | The encoder MUST implement filtering for raw depth (e.g. median, hole-fill). | [Brief] | `DepthFilter` exposes ≥ 2 modes; default profile uses `Median3 + HoleFillNN`. | B | `unit/test_depth_filter.cpp::modes` |
| **REQ-012** | The codec MUST provide both lossless and lossy compression modes. | [Brief] | Lossless mode: bit-exact round-trip. Lossy mode: bounded-error round-trip with tunable error budget. | B | `unit/test_modes.cpp::lossless_lossy` |

## E. H.264 integration

| ID | Requirement | Citation | Pass criterion | Sev | Test |
|---|---|---|---|---|---|
| **REQ-013** | Compressed depth payloads MUST be packaged as `user_data_unregistered` SEI messages in the H.264 stream. | [Brief]; [H.264 §D.1.6] | Output NAL units have `nal_unit_type = 6` (SEI), `payloadType = 5` (user_data_unregistered), prefixed by the SCC UUID. | B | `integration/test_h264.cpp::sei_format` |
| **REQ-014** | The codec MUST integrate with the H.264 framework via libavcodec/libavformat. | [Brief] | Reference integration in `bench/` performs RGB encode + SCC SEI inject + remux + decode + SEI extract end-to-end. | B | `integration/test_libav.cpp::roundtrip` |
| **REQ-015** | The codec MUST provide an interface for SEI extraction and processing during playback. | [Brief] | `scc_decode_sei()` C ABI accepts raw SEI byte buffers and produces depth frames. | B | `unit/test_cabi.cpp::decode_sei` |
| **REQ-030** | The codec MUST be alignment-compatible with MPEG-family standards (per the brief's "Mpeg Variants" requirement). | [Brief] | The wire format does not violate H.264's NAL escape rules; payload survives an MPEG-TS mux/demux. | B | `integration/test_mpegts.cpp::passthrough` |

## F. Performance

| ID | Requirement | Citation | Pass criterion | Sev | Test |
|---|---|---|---|---|---|
| **REQ-016** | Real-time throughput MUST be ≥ 25 fps. | [Brief] | On a Ryzen 5600X (single-threaded, AVX2 path), encode + decode at 1280×720, 12 bpp ≥ 25 fps. | B | `bench/perf_realtime.csv` |
| **REQ-017** | Compression ratio MUST exceed standard lossless algorithms by > 40 %. | [Brief] | On the fixture corpus, SCC lossless mean compression ratio is ≥ 1.4× the best of {PNG, zlib, TIFF-LZW}. | B | `bench/comparator.csv` |
| **REQ-018** | The codec MUST provide configurable quality/compression trade-offs. | [Brief] | At least three named profiles ship: `lossless`, `lossy:high`, `lossy:streaming`. | B | `unit/test_profiles.cpp::all_profiles` |
| **REQ-019** | Visible artefacts MUST be minimised in 3D visualisation, especially during rotation/zoom. | [Brief] | Visual regression test: render reconstructed depth from 8 canonical viewpoints; max pixel diff vs raw < 5 % at lossy:high. | B | `bench/visual_regression/` |

## G. Input/output specifications

| ID | Requirement | Citation | Pass criterion | Sev | Test |
|---|---|---|---|---|---|
| **REQ-020** | The codec MUST accept RGB-D video streams (RGB frames + depth frames) as input. | [Brief] | Reference app's capture agent ingests RGB + depth from at least one supported sensor. | B | `e2e/test_capture.py::sensor_ingest` |
| **REQ-031** | The codec MUST emit a compressed bitstream compatible with the H.264 framework. | [Brief] | Output `.mp4` files play in VLC, ffplay, and Chrome `<video>` (RGB visible; SEI ignored or surfaced via a polyfill). | B | `integration/test_h264.cpp::players` |
| **REQ-032** | The SDK MUST provide functions for encoding, decoding, and parameter configuration. | [Brief] | Public C ABI exposes `scc_encode_frame`, `scc_decode_sei`, `scc_set_param`, `scc_get_param`, `scc_load_profile`, `scc_save_profile`. | B | `unit/test_cabi.cpp::public_surface` |

## H. Testing and benchmarking

| ID | Requirement | Citation | Pass criterion | Sev | Test |
|---|---|---|---|---|---|
| **REQ-023** | The codec MUST be tested with single-camera and multi-camera setups. | [Brief] | Bench corpus contains at least one sequence from each topology (single sensor; stereo / multi-sensor rig). | B | `bench/corpus_index.json` |
| **REQ-024** | Compression performance MUST be compared against JPEG 2000, PNG, TIFF, and zlib. | [Brief] | `bench/comparator.csv` reports all four comparators on every fixture and every profile. | B | `bench/comparator.csv` |
| **REQ-040** | Visual quality of 3D reconstruction after compression/decompression MUST be evaluated. | [Brief] | PSNR + SSIM on reconstructed depth, plus point-cloud diff, reported in `bench/quality.csv`. | B | `bench/quality.csv` |
| **REQ-041** | Processing speed and memory usage MUST be benchmarked. | [Brief] | `bench/perf.csv` reports encode/decode fps, peak RSS, and peak heap allocation per profile. | B | `bench/perf.csv` |
| **REQ-042** | Different bit-depth settings (8, 12, 16) MUST be tested. | [Brief] | All three bit-depths are present in the `bench/perf.csv` and `bench/quality.csv` matrices. | B | bench reports |

## I. SDK packaging

| ID | Requirement | Citation | Pass criterion | Sev | Test |
|---|---|---|---|---|---|
| **REQ-026** | A stable C ABI MUST be exposed in `libscc`. | architecture | `cabi/include/libscc.h` is C99-pure; ABI baseline tracked; CI gates breaking changes. | B | `ci/abi-diff.sh` |
| **REQ-027** | A WebAssembly + TypeScript binding MUST be published as `@djinn/scc-wasm`. | architecture | `npm view @djinn/scc-wasm` succeeds for the target version; bundle ≤ 800 KiB gzip. | B | `sdk/wasm/tests/` |
| **REQ-028** | A Python binding MUST be published as `scc-py` on PyPI. | architecture | Wheels for cp310/11/12/13 × {linux x86_64, linux aarch64, macos universal2, win amd64} are present. | B | `sdk/python/tests/` |
| **REQ-029** | Unity and Unreal Engine plugins MUST be available. | architecture | UPM package and UE plugin both build and pass functional tests. | B | `sdk/unity/tests/`, `sdk/unreal/tests/` |

## J. Studio reference application

| ID | Requirement | Citation | Pass criterion | Sev | Test |
|---|---|---|---|---|---|
| **REQ-050** | Web frontend MUST allow operators to start/stop captures and visualise the live stream in 3D. | architecture | Manual smoke test on Chrome 113+: capture starts, viewer renders point cloud at ≥ 25 fps. | B | `e2e/test_studio.spec.ts::capture` |
| **REQ-051** | Control-plane API MUST expose REST endpoints for sessions, profiles, recordings. | architecture | OpenAPI 3.1 doc lists all routes; `curl` smoke tests pass against staging. | B | `tests/api/openapi-smoke.sh` |
| **REQ-052** | Capture agent MUST run as a single signed binary on Linux, macOS, and Windows. | architecture | CI publishes platform binaries with valid signatures (Apple notarisation, Authenticode, GPG). | B | release pipeline |
| **REQ-053** | Live metrics (fps, bitrate, PSNR, SSIM) MUST be displayed in the frontend during capture. | architecture | `AnalyticsPanel` renders the four metrics, sourced from the WebSocket. | B | Playwright |
| **REQ-054** | The frontend MUST be accessible (WCAG 2.1 AA). | architecture | axe-core in CI reports zero serious or critical violations. | B | `ci/a11y.yml` |
| **REQ-055** | The studio MUST support multiple tenants. | architecture | Postgres RLS policies prevent cross-tenant access; tenant-isolation test passes. | B | `tests/api/tenant-isolation.spec.ts` |

## K. Security & privacy

| ID | Requirement | Citation | Pass criterion | Sev | Test |
|---|---|---|---|---|---|
| **REQ-060** | All transport between operator and platform MUST use TLS 1.3. | NIST SP 800-53 SC-8 | TLS 1.0/1.1/1.2 rejected by ALB; testssl.sh confirms. | B | `ci/tls-scan.yml` |
| **REQ-061** | Recording archives MUST be encrypted at rest with per-tenant CMKs. | NIST SP 800-53 SC-28 | S3 SSE-KMS with `kms:GenerateDataKey` audit logged per tenant. | B | runtime audit |
| **REQ-062** | Passwords MUST be hashed with Argon2id. | OWASP ASVS V2.4 | Code review confirms libsodium Argon2id at default-strong params. | B | code review |
| **REQ-063** | The decoder MUST refuse SEI payloads where declared sizes exceed input length. | NIST SP 800-53 SI-10 | Fuzz harness against `scc_decode_sei` for ≥ 24 h with no crashes. | B | `ci/fuzz.yml` |
| **REQ-064** | No secrets MUST be committed to the repository. | OWASP ASVS V14.3 | `gitleaks` in CI passes; `truffleHog` historical scan clean. | B | `ci/secret-scan.yml` |
| **REQ-065** | Dependencies MUST be tracked via SBOM (CycloneDX). | NIST SSDF | `bom.cdx.json` generated for every release artefact. | B | release pipeline |

## L. Observability

| ID | Requirement | Citation | Pass criterion | Sev | Test |
|---|---|---|---|---|---|
| **REQ-070** | All services MUST emit structured JSON logs with `tenant_id`, `session_id`, `trace_id`. | architecture | Sample log entries match the schema; lint rule fails missing fields. | B | `ci/log-schema.yml` |
| **REQ-071** | Distributed tracing MUST propagate across the FFI boundary. | architecture | A trace span started in the API is visible inside `libscc` calls in Grafana Tempo. | B | `tests/observability/trace-propagation.spec.ts` |
| **REQ-072** | Headline KPI `scc.compression.ratio` MUST be exposed as a Prometheus / OTLP gauge. | architecture | Metric visible in Grafana; alert wired on regressions > 10 %. | B | runtime check |

## M. Documentation & licensing

| ID | Requirement | Citation | Pass criterion | Sev | Test |
|---|---|---|---|---|---|
| **REQ-080** | Each source file implementing a patented technique MUST cite the patent column inline. | DLA Piper / Dentons advisory | `grep -r "US10827161B2"` lists every relevant file; counsel review confirms. | B | release gate |
| **REQ-081** | Public SDK packages MUST ship a `LICENSE-third-party.md` listing every transitive dependency licence. | OWASP ASVS V14.2 | File present; auto-generated from `licensee` / `cargo-deny` reports; CI fails on diff. | B | `ci/licensing.yml` |
| **REQ-082** | A user-facing migration guide MUST be published for every breaking ABI change. | architecture | `docs/abi-migrations/v1-v2.md` exists when v2 is released. | N | release checklist |
| **REQ-083** | A patent-claim-mapping document MUST be maintained mapping each patent claim to its implementing component. | DLA Piper advisory | `docs/patent/claim-mapping.md` keeps a row per claim; reviewed quarterly. | B | release gate |

## N. Operational

| ID | Requirement | Citation | Pass criterion | Sev | Test |
|---|---|---|---|---|---|
| **REQ-090** | Cold start of the studio API container MUST be < 800 ms. | architecture | Measured in CI on every PR; gate fails on regression > 10 %. | N | `ci/perf-cold-start.yml` |
| **REQ-091** | RTO ≤ 4 h, RPO ≤ 15 min for studio app metadata. | architecture | DR runbook tested quarterly; runbook lives at `runbooks/dr.md`. | N | quarterly DR drill |
| **REQ-092** | The codec MUST pass a 24-hour soak test (continuous encode/decode) without leaks. | architecture | Valgrind massif diff on hour 0 vs hour 24 shows ≤ 1 % heap growth. | B | `ci/soak.yml` (nightly) |

---

## How to use this catalogue

1. Every PR runs `ci/conformance/run.py` (see `prompts/AI_Build_Prompts.md` §15) which executes the test pointer for each REQ and reports PASS / FAIL / TIMEOUT.
2. Blocking REQs (severity B) must all PASS before a release tag.
3. New requirements are added by appending; never renumbering.
4. Each REQ either lives forever (kept even when satisfied, as proof of intent) or is explicitly retired with a row in the deprecation table:

| Retired ID | Reason | Date | Replaces / replaced by |
|---|---|---|---|

(empty — the catalogue is at v1.0)

---

*— End of Acceptance Criteria —*

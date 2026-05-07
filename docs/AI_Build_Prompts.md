# SCC — AI Build Prompts (Claude Code Compatible)

**Purpose.** A prompt-per-component pack for driving the implementation of the Spatial Compression Codec with Claude Code. Each prompt is designed to be pasted into Claude Code (or Cowork, or Claude in chat) as a self-contained build instruction. Prompts assume the working tree layout in §0; if you adapt that layout, edit the path references in each prompt.

**House philosophy — "Ultrathink".** Architectural depth, elegant implementation, comprehensive documentation, no minimal-effort outputs. Every prompt below ends with the **Ultrathink Block**: a five-line discipline that the model must satisfy before considering the task complete.

**Citation discipline.** Every prompt below references one or more `[REQ-NNN]`, `[US10827161B2]`, or `[Standard]` evidence tags. The implementation must preserve those tags as inline comments in the source code so that patent counsel can audit claim-mapping later.

---

## 0. Working tree layout (assumed)

```
scc/
├── codec/                   # libscc C++17 source
│   ├── include/scc/
│   ├── src/
│   │   ├── common/          # rANS, quad-tree, disparity, sei
│   │   ├── encoder/
│   │   └── decoder/
│   └── CMakeLists.txt
├── cabi/                    # C ABI shim (libscc.so/.dll/.dylib)
│   ├── include/libscc.h
│   ├── src/
│   └── CMakeLists.txt
├── sdk/
│   ├── wasm/                # @djinn/scc-wasm
│   ├── python/              # scc-py (pybind11)
│   ├── unity/               # Unity plugin
│   └── unreal/              # Unreal plugin
├── studio/
│   ├── frontend/            # React + WebGPU
│   ├── api/                 # Node.js + Fastify
│   └── agent/               # Rust capture agent
├── bench/                   # scc-bench: comparator harness
├── fixtures/                # depth corpus + reference vectors
├── docs/
│   ├── architecture/        # SAD.md + diagrams
│   └── adr/                 # ADR-001..N
├── ci/                      # GitHub Actions workflows
└── README.md
```

---

## 1. Build the rANS-8 / rANS-16 entropy coders

```text
You are a senior C++17 systems engineer. Implement the rANS (Range Asymmetric
Numeral Systems) entropy coder for the Spatial Compression Codec. Reference
Duda, J. (2014), arXiv:1311.2540, §3.3 — implement that algorithm, not a
clever variant.

Constraints:
- Two state widths: rans8_t (32-bit accumulator, 8-bit renormalisation byte
  output) and rans16_t (32-bit accumulator, 16-bit renormalisation chunk
  output). [REQ-009, REQ-010]
- Header-only or single .cpp + header — no external deps.
- Public surface (in codec/include/scc/rans.hpp):
    namespace scc::rans {
      class Encoder8;  // for alphabets <= 256
      class Encoder16; // for alphabets up to int16 range
      class Decoder8;
      class Decoder16;
    }
  Each encoder takes a probability table, emits bytes via an output iterator.
  Each decoder takes the bytestream + the same probability table.
- Probability tables are quantised to 12-bit precision (4096 total).
- MUST be deterministic (same input + same prob table → bit-identical output).

Tests (Catch2, in codec/tests/test_rans.cpp):
1. Round-trip: random alphabet, random data → encode → decode → equal.
2. Empty input → empty output.
3. Single-symbol alphabet → trivial.
4. Worst-case skewed distribution (one symbol = 4095/4096).
5. Property test (rapidcheck): forall (alphabet, freqs, data):
     decode(encode(data, freqs), freqs) == data.

Acceptance:
- All tests pass on Linux x86_64, macOS arm64, Windows x86_64.
- No undefined behaviour under -fsanitize=undefined,address.
- Benchmarked encode > 200 MB/s, decode > 250 MB/s on a Ryzen-class CPU.
- Cite [Duda 2014] in the file header. Cite [REQ-009] and [REQ-010] inline
  near the renormalisation routines.

Ultrathink Block (must be satisfied):
1. Did you read Duda §3.3 before writing the renormalisation loop?
2. Have you compared bit-for-bit against ryg_rans test vectors?
3. Is there an ASCII diagram of the state machine in a comment?
4. Does the property test cover skewed and uniform distributions?
5. Is there a doc/adr/ADR-001-rans.md explaining the state-width decision?
```

---

## 2. Build the disparity matrix transform

```text
You are a senior C++17 engineer. Implement the patent's row-prefix difference
transform for depth frames in the Spatial Compression Codec.

Reference: US Patent 10,827,161 B2, columns 4–5. [REQ-001, REQ-002, REQ-006]

The forward transform takes a depth frame d[H][W] (uint16_t) and produces a
disparity matrix S[H][W] (int16_t) defined as:

    S[1][1]  = d[1][1]                       // anchor
    S[1][j]  = d[1][j]   - d[1][j-1]          // top row, horizontal
    S[i][j]  = d[i][j]   - d[i-1][j]          // i >= 2, column-wise vertical

The inverse is the prefix-sum of the same shape.

Public surface (in codec/include/scc/disparity.hpp):
    namespace scc {
      void disparity_forward(const uint16_t* d, int W, int H, int16_t* s);
      void disparity_inverse(const int16_t* s, int W, int H, uint16_t* d);
    }

Constraints:
- Cache-friendly row-major access.
- AVX2 inner-loop on x86_64; NEON on ARM64; scalar fallback elsewhere.
- Selection by CPU feature detection at runtime, not compile time.
- Boundary semantics MUST match the patent exactly. No "but it's faster if
  we…" deviations.

Tests:
1. Round-trip identity for uniform and random frames.
2. Single-row frame (H = 1).
3. Single-column frame (W = 1).
4. 1×1 frame.
5. Property test: forall (W,H in [1, 4096], depth in U16): inverse(forward(d)) == d.
6. Bench: forward+inverse on 1280×720 16bpp completes in < 1.5 ms on a Ryzen
   5600X (single-threaded, AVX2 path).

Acceptance:
- Tests pass with -Wall -Wextra -Werror -fsanitize=address,undefined.
- File header cites US10827161B2 col. 4–5 verbatim formula block.
- A comment block above each loop says "REQ-002" or "REQ-006".

Ultrathink Block:
1. Did you handle int16 overflow at the largest reasonable depth difference?
2. Is the SIMD path correct on the last partial vector of each row?
3. Are there tests for non-multiple-of-vector widths?
4. Is the inverse transform numerically identical to the forward inverse?
5. Have you committed a perf regression test that fails CI on > 10% slowdown?
```

---

## 3. Build the FrequencyAnalyser (TOP / R / B decomposition)

```text
You are a senior C++17 engineer. Implement the TOP/R/B decomposition for the
Spatial Compression Codec, as described in US10827161B2 col. 7. [REQ-007, REQ-008]

Given a flattened S array of int16, you must:
1. Build a frequency histogram.
2. Select the top-x most frequent values (default x = 4, configurable in [2,16]).
3. Walk S; for each value v:
   - if v ∈ TOP, emit its index in TOP into top_indices and set B[i] = 0.
   - else, emit v into r_values and set B[i] = 1.

Public surface (in codec/include/scc/frequency.hpp):
    namespace scc {
      struct DecomposeResult {
        std::array<int16_t, 16> top;       // unused entries left at 0
        uint8_t                  top_count; // 2..16
        std::vector<uint8_t>     top_indices;
        std::vector<int16_t>     r_values;
        std::vector<uint8_t>     b_mask;    // 1 bit per S element, byte-packed
      };
      DecomposeResult decompose(const int16_t* s, size_t n, uint8_t top_count);
      void recompose(const DecomposeResult& dr, int16_t* s_out);
    }

Constraints:
- For top selection, use a partial sort over the histogram, not a full sort.
- b_mask is byte-packed in bit-position order matching reading order of S.
- recompose MUST be the exact inverse of decompose.

Tests:
1. Round-trip identity (random input, random top_count).
2. Edge: all values in TOP (e.g. binary alphabet).
3. Edge: no values in TOP (all unique).
4. Property test (rapidcheck): forall (s, k): recompose(decompose(s, k)) == s.

Acceptance:
- Cite [REQ-007], [REQ-008], [US10827161B2 col. 7] in the file header.
- Tests pass under sanitizers.
- The patent observation that "top 4 values typically constitute 85–98% of
  disparity values" should be quoted in a comment above `default top_count`.

Ultrathink Block:
1. Have you measured the typical TOP coverage on a real depth corpus?
2. Is recompose the bit-exact inverse, including for empty inputs?
3. What happens if top_count > unique_values? Documented?
4. Is b_mask packing endian-deterministic across platforms?
5. Is there a property test that varies top_count in [2..16]?
```

---

## 4. Build the MovementDetector + QuadTreeRegionMap

```text
You are a senior C++17 engineer. Implement movement detection and quad-tree
region partitioning for the Spatial Compression Codec. [REQ-021, REQ-022,
US10827161B2 col. 9–10]

Public surface (in codec/include/scc/quadtree.hpp):
    namespace scc {
      struct QuadRegion { uint16_t x, y, size; uint8_t motion_class; uint8_t pattern_id; };
      struct RegionMap  { std::vector<QuadRegion> leaves; uint8_t depth; };

      RegionMap build_region_map(
        const uint16_t* curr, const uint16_t* prev,  // or nullptr for I-frame
        int W, int H,
        uint16_t tau_static, uint16_t tau_low, uint16_t tau_high,
        uint16_t leaf_size_px = 16);
    }

Algorithm:
1. If prev is null (I-frame), produce a single root region with motion_class=2
   (high motion) — full update.
2. Else compute Δ(i,j) = |curr[i,j] - prev[i,j]| and:
3. Recursively partition starting from (0, 0, max(W,H)) downwards. At each
   node, compute the histogram of Δ in the region:
   - if max Δ ≤ τ_static → leaf, motion_class = 0 (static).
   - else if percentile_95(Δ) ≤ τ_low → leaf, motion_class = 1 (low).
   - else if region size <= leaf_size_px → leaf, motion_class = 2 (high).
   - else split into 4 children, recurse.
4. Pattern_id assignment: static→0, low→1, high→2. The mapping from
   pattern_id to actual 8-bit bitmask happens in the InterlacePatternSelector.

Constraints:
- Boundary handling: regions on the right/bottom edge that fall off the
  frame are clipped; record actual size.
- Output ordering: depth-first traversal so the wire descriptor is unambiguous.

Tests:
1. Static frame pair → single root leaf, class 0.
2. Two halves with different motion → exactly two leaves at depth 1.
3. Random motion → no crashes, every pixel covered exactly once
   (sum of region areas == W*H).
4. I-frame (prev == nullptr) → single root, class 2.

Acceptance:
- Cite [REQ-021], [REQ-022], [US10827161B2 col. 9–10] in the file header.
- Tests pass under sanitizers.
- Bench: 1280×720 region map built in < 0.5 ms (single-thread, no SIMD
  required for the histogram step).

Ultrathink Block:
1. Are τ thresholds documented as profile-tunable in the public API?
2. Is the depth-first traversal stable across runs (deterministic output)?
3. Have you covered boundary clipping where W or H is not a power of two?
4. Is there a fuzz test that throws random Δ at the partitioner?
5. Have you written ADR-003 with the leaf_size_px = 16 rationale?
```

---

## 5. Build the SEIPayloadMuxer + Demuxer (H.264 integration)

```text
You are a senior C++17 engineer. Implement the SEI muxer/demuxer for the
SCC codec, packaging the compressed depth payload as an H.264
user_data_unregistered SEI message. [REQ-013, REQ-014; ISO/IEC 14496-10 §D.1.6]

Wire format (big-endian for multi-byte):
    [16 bytes  SCC UUID  (build-time constant — see ADR-005)]
    [u8        version_major = 1]
    [u8        version_minor = 0]
    [u16       width]
    [u16       height]
    [u8        bit_depth]              // 8, 12, or 16
    [u8        mode_flags]             // bit0 = lossless, bit1 = motion-on, ...
    [u8        top_count]              // 2..16
    [int16     top_values × top_count] // big-endian
    [u32       rans_top_len][bytes rans_top]
    [u32       rans_b_len  ][bytes rans_b  ]
    [u32       rans_r_len  ][bytes rans_r  ]
    [var       quad_tree_descriptor]   // depth-first, see quadtree.hpp

NAL escaping: insert byte 0x03 after every '0x00 0x00 0x00..0x03' pattern
per ISO/IEC 14496-10 §7.4.1.1. The muxer outputs an already-escaped buffer
ready to be wrapped in a NAL unit by the caller (libavcodec).

Public surface (in codec/include/scc/sei.hpp):
    namespace scc::sei {
      struct FrameHeader { uint16_t width, height; uint8_t bit_depth; uint8_t mode_flags; };
      std::vector<uint8_t> mux(const FrameHeader&,
                               const std::array<int16_t,16>& top, uint8_t top_count,
                               std::span<const uint8_t> rans_top,
                               std::span<const uint8_t> rans_b,
                               std::span<const uint8_t> rans_r,
                               std::span<const uint8_t> quadtree_desc);

      struct DemuxResult {
        bool      ok;
        FrameHeader hdr;
        std::array<int16_t,16> top;
        uint8_t   top_count;
        std::vector<uint8_t> rans_top, rans_b, rans_r, quadtree_desc;
      };
      DemuxResult demux(std::span<const uint8_t> sei_payload);
    }

Constraints:
- The muxer MUST validate that bit_depth ∈ {8, 12, 16} and top_count ∈ [2, 16].
  Out-of-range → return empty vector, do not throw.
- The demuxer MUST validate every length field against the input buffer
  size BEFORE allocating anything. A malformed input never crashes.
- Maximum permitted payload size is configurable; default 256 MiB.
- Endianness conversion uses standard host-to-network helpers; no clever
  bit-fiddling that breaks on big-endian hosts.

Tests:
1. Round-trip: mux → demux → equal.
2. NAL escape round-trip on payloads that contain 0x00 0x00 0x00 patterns.
3. Truncated payload → DemuxResult.ok == false.
4. Oversized length field → rejected, no allocation.
5. libFuzzer harness: feed 1M random bytes, must never crash.

Acceptance:
- Cite [REQ-013], [REQ-014], [ISO/IEC 14496-10 §D.1.6] and [§7.4.1.1] in
  the file header.
- Fuzz target compiles and is wired into CI nightly.
- A README in codec/src/common/sei/ documents the wire format with a
  byte-by-byte annotated example payload.

Ultrathink Block:
1. Have you read §D.1.6 and §7.4.1.1 of the spec? Linked in the comment?
2. Does the demuxer validate length fields BEFORE allocating?
3. Is the SCC UUID a real cryptographically random UUIDv4 (not a placeholder)?
4. Is there a hex-dump example payload in the README for debugging?
5. Have you tested round-trip across a real libavcodec write/read cycle?
```

---

## 6. Build the C ABI shim (`libscc`)

```text
You are a senior C++17 / C99 engineer. Build the stable C ABI for the
Spatial Compression Codec. This is the SOLE public surface for all
downstream language bindings. [REQ-026..029, ADR-006]

Header at cabi/include/libscc.h. Implementation in cabi/src/. Built as a
shared library (libscc.so / libscc.dll / libscc.dylib).

Public surface (verbatim):
    typedef struct scc_ctx scc_ctx;
    typedef enum {
      SCC_OK = 0,
      SCC_INVALID_ARG = 1,
      SCC_OOM = 2,
      SCC_FORMAT = 3,
      SCC_INTERNAL = 4
    } scc_result;

    scc_ctx*    scc_init(void);
    void        scc_destroy(scc_ctx*);
    const char* scc_version(void); // "1.0.0"

    scc_result  scc_set_param(scc_ctx*, const char* key, const char* value);
    scc_result  scc_get_param(scc_ctx*, const char* key, char* out, size_t cap);

    // depth: row-major uint16_t buffer; depth_stride in BYTES.
    scc_result  scc_encode_frame(
                  scc_ctx*,
                  const uint8_t* depth, size_t depth_stride,
                  int width, int height, int bit_depth,
                  uint8_t* out_sei, size_t out_cap, size_t* out_len);

    scc_result  scc_decode_sei(
                  scc_ctx*,
                  const uint8_t* sei, size_t sei_len,
                  uint8_t* out_depth, size_t out_stride,
                  int* out_width, int* out_height, int* out_bit_depth);

    scc_result  scc_load_profile(scc_ctx*, const char* json);
    scc_result  scc_save_profile(scc_ctx*, char* out, size_t cap, size_t* out_len);

    const char* scc_get_last_error(scc_ctx*);

Constraints:
- C99-pure header. No C++ types, no `bool`, no `inline`, no implicit C99
  features that break MSVC.
- Functions MUST NOT throw. C++ exceptions caught at the boundary, mapped
  to scc_result codes.
- scc_get_last_error returns a thread-local string valid until the next
  call on this context.
- ABI versioning: bake SOVERSION = 1 into CMake; bump on breaking changes.
- Profiles are JSON strings (parsed by an inline single-header lib like
  json.hpp; bundled, no external dep).

Tests:
1. Init/destroy cycle 10⁶ times → no leaks (Valgrind / ASan).
2. Round-trip encode/decode of a synthetic depth frame.
3. Bad pointer / null context → SCC_INVALID_ARG, never crash.
4. Buffer too small → SCC_INVALID_ARG, *out_len set to required size.
5. Symbol stability: dump exported symbols and diff against
   cabi/abi-baseline.txt; CI fails on diff.

Acceptance:
- Cite [REQ-026..029] and [ADR-006] in libscc.h.
- libscc.so built on Linux x86_64 + ARM64; libscc.dll on Windows x86_64;
  libscc.dylib on macOS x86_64 + arm64. All in CI.
- One-page docs/abi.md explaining the lifecycle and profile JSON format.

Ultrathink Block:
1. Have you written abi-baseline.txt and a diff check that runs in CI?
2. Are all error paths tested, including allocation failure (mock allocator)?
3. Is scc_get_last_error thread-local-correct?
4. Have you verified the .h compiles cleanly under MSVC /Wall and
   gcc -std=c99 -pedantic?
5. Is the SOVERSION strategy documented in docs/abi.md?
```

---

## 7. Build the WASM / TypeScript binding

```text
You are a senior front-end / WebAssembly engineer. Build the @djinn/scc-wasm
package: a TypeScript-friendly WebAssembly binding for the SCC codec, built
from the C ABI via Emscripten. [REQ-027]

Layout: sdk/wasm/
- src/scc.ts            (TS facade — public)
- src/raw.ts            (auto-generated FFI types, internal)
- src/index.ts          (re-exports)
- emscripten/build.sh   (build script)
- package.json
- README.md
- tests/                (Vitest)

Build command:
  emcc cabi/src/*.cpp codec/src/**/*.cpp \
    -O3 -msimd128 -sWASM=1 \
    -sEXPORTED_FUNCTIONS=_scc_init,_scc_destroy,_scc_encode_frame,_scc_decode_sei,_scc_set_param,_scc_get_last_error,_malloc,_free \
    -sEXPORTED_RUNTIME_METHODS=cwrap,ccall,HEAPU8,HEAPU16,HEAP32 \
    -sMODULARIZE=1 -sEXPORT_NAME=createSCC \
    -sALLOW_MEMORY_GROWTH=1 \
    -sINITIAL_MEMORY=64MB \
    -o dist/scc.js

Public TS surface (verbatim):
    export interface SCCEncoderOptions {
      profile?: 'lossless' | 'lossy:high' | 'lossy:streaming';
      bitDepth?: 8 | 12 | 16;
      width: number;
      height: number;
    }

    export class SCCEncoder {
      static async create(opts: SCCEncoderOptions): Promise<SCCEncoder>;
      encode(depth: Uint16Array): Promise<Uint8Array>; // SEI payload bytes
      close(): void;
    }

    export class SCCDecoder {
      static async create(): Promise<SCCDecoder>;
      decode(sei: Uint8Array): Promise<{ depth: Uint16Array; width: number; height: number; bitDepth: number }>;
      close(): void;
    }

    // WebCodecs adapters
    export function encoderTransformStream(opts: SCCEncoderOptions):
      TransformStream<{ rgb: VideoFrame; depth: Uint16Array }, Uint8Array>;
    export function decoderTransformStream():
      TransformStream<Uint8Array, { depth: Uint16Array; width: number; height: number; bitDepth: number }>;

Constraints:
- Zero npm runtime dependencies. The WASM module is bundled.
- Every async API uses real Promises (no fake polyfills).
- Memory: every encode/decode copies in/out of the WASM heap; no leaking
  pointers across the JS boundary.
- Every public method has a TSDoc block citing the relevant [REQ-NNN].

Tests (Vitest):
1. Round-trip encode/decode in Node 20 with `--experimental-vm-modules`.
2. Browser test via Playwright on Chromium 113+.
3. Memory: 1000 encode/decode cycles → measured WASM heap stable
   (within ±2 MiB).

Acceptance:
- Bundle size: dist/scc.js + dist/scc.wasm ≤ 800 KiB gzip.
- TypeScript declarations exported (.d.ts).
- `npm publish --provenance` works in CI.
- README with copy-pasteable browser and Node examples.

Ultrathink Block:
1. Do you free every malloc on every code path, including error paths?
2. Are the TransformStream adapters actually back-pressure-correct?
3. Is the bundle size under budget? (Run the size script in CI.)
4. Are the .d.ts files complete enough that VS Code IntelliSense works?
5. Is there a Playwright test that runs in real Chromium, not jsdom?
```

---

## 8. Build the Python binding (`scc-py`)

```text
You are a senior Python / C++ engineer. Build scc-py, a pybind11 binding
for the SCC codec with NumPy zero-copy interop. [REQ-028]

Layout: sdk/python/
- pyproject.toml      (PEP 621, build via scikit-build-core)
- src/scc_py/_core.cpp
- src/scc_py/__init__.py
- tests/test_roundtrip.py
- README.md

Public Python API (verbatim, in scc_py/__init__.py):
    from typing import Literal
    import numpy as np

    class Encoder:
        def __init__(self,
                     profile: Literal['lossless','lossy:high','lossy:streaming'] = 'lossless',
                     bit_depth: int = 12) -> None: ...
        def encode(self, depth: np.ndarray) -> bytes: ...   # depth: shape (H, W), dtype uint16
        def close(self) -> None: ...
        def __enter__(self): ...
        def __exit__(self, *a): self.close()

    class Decoder:
        def __init__(self) -> None: ...
        def decode(self, sei: bytes) -> np.ndarray: ...     # returns shape (H, W), dtype uint16
        def close(self) -> None: ...
        def __enter__(self): ...
        def __exit__(self, *a): self.close()

Constraints:
- pybind11 ≥ 2.12.
- NumPy interop: arrays must be C-contiguous and uint16 — validate, raise
  TypeError otherwise.
- Encode path takes an array WITHOUT copying the data into pybind buffers —
  pass the raw pointer directly to the C ABI.
- Type stubs (.pyi) generated and shipped.

Tests (pytest):
1. Round-trip on a synthetic frame.
2. Wrong dtype (e.g. float32) → TypeError.
3. Non-contiguous input → TypeError or auto-copy with a documented warning.
4. Context manager closes the underlying C ABI handle exactly once.

Acceptance:
- Wheels built for cp310/cp311/cp312/cp313 × {linux x86_64, linux aarch64,
  macos universal2, windows amd64} via cibuildwheel.
- mypy --strict passes on the .pyi stubs.
- pip install scc-py works on a fresh venv on each target.

Ultrathink Block:
1. Is encode() truly zero-copy on the input side?
2. Are wheels actually built for all eight platforms in CI?
3. Have you tested the context manager with a deliberate exception inside?
4. Are the type stubs complete (no `Any` leak)?
5. Is README copy-pasteable for `pip install` then a 5-line example?
```

---

## 9. Build the Unity plugin

```text
You are a senior Unity / C# engineer. Build the SCC Unity plugin, wrapping
the libscc C ABI for use in Unity 2022 LTS and 6 LTS. [REQ-029]

Layout: sdk/unity/com.djinn.scc/
- Runtime/SCCEncoder.cs
- Runtime/SCCDecoder.cs
- Runtime/Plugins/<platform>/libscc.<ext>
- Editor/SCCImporter.cs        (asset import for .scc test fixtures)
- Tests/EditMode/...
- package.json                 (UPM)
- README.md

Public C# API:
    namespace Djinn.SCC {
      public sealed class Encoder : IDisposable {
        public Encoder(SCCProfile profile, int bitDepth, int width, int height);
        public byte[] Encode(NativeArray<ushort> depth);
        public void Dispose();
      }
      public sealed class Decoder : IDisposable {
        public Decoder();
        public NativeArray<ushort> Decode(NativeArray<byte> sei,
                                          out int width, out int height, out int bitDepth);
        public void Dispose();
      }
      public enum SCCProfile { Lossless, LossyHigh, LossyStreaming }
    }

Constraints:
- All cross-FFI buffers via `NativeArray<T>` to avoid GC churn.
- Burst-compatible where possible (helper methods marked [BurstCompile]).
- Plugins folder layout matches Unity's per-platform conventions:
    Plugins/x86_64/libscc.dll      (Windows)
    Plugins/x86_64/libscc.so       (Linux)
    Plugins/x86_64/libscc.dylib    (macOS Intel)
    Plugins/AnyCPU/libscc.dylib    (macOS Apple Silicon, with import settings)
    Plugins/Android/arm64-v8a/libscc.so
    Plugins/iOS/libscc.a
- Package distributable via the Unity Package Manager (`package.json`).

Tests (EditMode):
1. Round-trip on a synthetic frame.
2. Disposal cycle (10⁵ iterations) → stable memory.

Acceptance:
- Sample scene under Samples~/BasicCapture demonstrates encode → decode →
  visualise as a Unity Mesh point cloud.
- README documents Unity 2022.3+ and 6.0+ as supported, with platform matrix.
- IL2CPP build passes on all platforms.

Ultrathink Block:
1. Is every NativeArray disposed (Allocator.Persistent → explicit Dispose)?
2. Does the sample scene run in Editor AND in a built player?
3. Are import settings on the .dylib correct for Apple Silicon?
4. Is the plugin signed for macOS/Windows for distribution?
5. Is the package compatible with both Mono and IL2CPP backends?
```

---

## 10. Build the Unreal Engine plugin

```text
You are a senior Unreal Engine / C++ engineer. Build the SCC Unreal plugin
for UE 5.3+. [REQ-029]

Layout: sdk/unreal/SCC/
- SCC.uplugin
- Source/SCC/Public/SCCStreamComponent.h
- Source/SCC/Private/SCCStreamComponent.cpp
- Source/SCC/Private/SCC.Build.cs
- Resources/Icon128.png
- README.md

Public UClass:
    UCLASS(ClassGroup=(SCC), meta=(BlueprintSpawnableComponent))
    class SCC_API USCCStreamComponent : public UActorComponent {
      GENERATED_BODY()
    public:
      UFUNCTION(BlueprintCallable, Category="SCC")
      bool BeginEncode(int32 Width, int32 Height, int32 BitDepth, FString Profile);

      UFUNCTION(BlueprintCallable, Category="SCC")
      TArray<uint8> EncodeFrame(const TArray<uint16>& Depth);

      UFUNCTION(BlueprintCallable, Category="SCC")
      bool DecodeSEI(const TArray<uint8>& SEI, TArray<uint16>& OutDepth,
                     int32& OutWidth, int32& OutHeight, int32& OutBitDepth);

      UFUNCTION(BlueprintCallable, Category="SCC")
      void EndEncode();
    };

Constraints:
- Build module is a Runtime module (no Editor dependencies in the runtime DLL).
- Third-party libscc placed under Source/ThirdParty/SCC/<platform>/.
- SCC.Build.cs handles per-platform PublicSystemLibraries / PublicAdditionalLibraries.
- All UFUNCTIONs expose Blueprint-friendly signatures (TArray, int32, FString —
  no raw pointers or std types in the API).

Tests:
- Functional test map (FunctionalTest actor) under Content/SCC/Tests/ that
  performs an encode → decode round-trip on a procedurally generated depth
  frame and checks bit-equality in lossless mode.

Acceptance:
- Plugin compiles for Win64, Linux, Mac, Android (Quest/Pico), iOS.
- README documents how to drop the plugin into a project.
- Plugin marketplace metadata (Icon128, FAB descriptors) ready.

Ultrathink Block:
1. Are there per-platform Build.cs branches for libscc linkage?
2. Does the FunctionalTest map run in headless `RunUnreal` CI?
3. Are TArray<uint16> conversions efficient (Memcpy, not per-element copy)?
4. Is the plugin marked Runtime-only so it can be cooked into a shipping build?
5. Is the SCC SDK version pinned via a SCC.Build.cs constant?
```

---

## 11. Build the Capture Agent (Rust)

```text
You are a senior Rust engineer. Build the SCC Studio capture agent: a
native single-binary daemon that owns the sensor connection and bridges
to libscc over FFI. Distributed as @djinn/scc-studio-agent on npm and as
platform-native installers.

Layout: studio/agent/
- src/main.rs
- src/sensors/{mod.rs, realsense.rs, k4a.rs, zed.rs, uvc.rs}
- src/scc_bridge.rs       (FFI to libscc)
- src/ipc.rs              (Unix socket / named pipe + protobuf)
- src/muxer.rs            (libavformat wrapper)
- src/proto/control.proto (protobuf schema)
- Cargo.toml

Public IPC contract (protobuf, length-prefixed on the socket):

    message AgentRequest {
      oneof kind {
        ListSensors    list = 1;
        StartSession   start = 2;
        StopSession    stop = 3;
        GetMetrics     metrics = 4;
      }
    }
    message AgentResponse {
      oneof kind {
        SensorList   sensors = 1;
        SessionInfo  session = 2;
        Metrics      metrics = 3;
        Error        err = 4;
      }
    }

Constraints:
- Sensor backends behind a `Sensor` trait. Crate features gate each
  backend (so an agent without the K4A SDK can still build).
- Lock-free SPSC ring buffer between sensor thread and encoder thread,
  using `crossbeam::queue::ArrayQueue` or a custom one.
- libscc accessed via `bindgen`-generated bindings in build.rs.
- All FFI calls inside `unsafe { ... }` blocks with a one-line `// SAFETY:`
  comment explaining the invariant.
- `tracing` for structured logs, `opentelemetry` for traces.
- No async runtime in the hot path; tokio only for IPC.

Tests:
- `cargo test` covering the Sensor trait with a mock sensor.
- Integration test that drives a full session via the IPC socket.
- `cargo clippy -- -D warnings` clean.
- `cargo audit` clean.

Acceptance:
- Single static binary on Linux (musl), single signed binary on Windows
  and macOS.
- npm package wrapping the binary per platform via @djinn/scc-studio-agent.
- README documents privacy implications: agent is the only component that
  can see raw sensor data.

Ultrathink Block:
1. Is every unsafe block paired with a SAFETY comment?
2. Are sensor backends truly feature-gated (build without K4A succeeds)?
3. Is the ring buffer benchmarked under load?
4. Is the IPC protobuf forward-compatible (added fields in tag-number order)?
5. Is the binary stripped, signed, and notarised in CI?
```

---

## 12. Build the Control-plane API (Node.js + Fastify)

```text
You are a senior Node.js / TypeScript engineer. Build the SCC Studio
control-plane API with Node 20 LTS, Fastify 4, and TypeScript strict
mode.

Layout: studio/api/
- src/index.ts            (server bootstrap)
- src/routes/{sensors,sessions,profiles,recordings}.ts
- src/middleware/{auth,rate-limit,tenant,error}.ts
- src/agent-controller.ts (IPC client to the agent)
- src/db/{client.ts, schema.sql, migrations/}
- src/observability/{logger,tracer,metrics}.ts
- prisma/schema.prisma    OR  drizzle/schema.ts
- tests/                  (Vitest)
- Dockerfile

Routes (verbatim):
    GET    /api/sensors                          → list sensors via agent
    POST   /api/sessions                         → create capture session
    GET    /api/sessions/:id                     → get session
    DELETE /api/sessions/:id                     → stop & finalise
    WS     /api/sessions/:id/metrics             → live metrics stream
    GET    /api/profiles
    POST   /api/profiles
    GET    /api/profiles/:id
    PATCH  /api/profiles/:id
    DELETE /api/profiles/:id
    POST   /api/recordings/:id/finalize

Constraints:
- TypeScript strict (no `any` outside auto-generated code).
- Zod schemas for every request body and response shape; reflect into
  OpenAPI 3.1 via fastify-openapi-glue or similar.
- Auth middleware: API key (32-byte URL-safe random) for service callers,
  JWT (RS256) for human users. Tenancy isolation at the query layer
  (every query filters on tenant_id).
- PostgreSQL Row-Level Security policies on every table; enable RLS in
  migrations.
- OpenTelemetry: traces, metrics, logs (Pino) with `tenant_id`,
  `session_id`, `trace_id` on every log record.
- 12-factor: all config from env vars; .env.example shipped, .env gitignored.
- Rate limiter via `@fastify/rate-limit`; defaults 60 rps per API key.

Tests:
- Vitest unit tests per route handler.
- Supertest integration tests against an in-process server.
- Postgres testcontainer for migrations + RLS tests.

Acceptance:
- Docker image < 250 MiB.
- Cold start < 800 ms.
- All routes documented in OpenAPI; UI rendered via @fastify/swagger-ui at
  /api/docs (gated to non-prod).

Ultrathink Block:
1. Is RLS actually enforced? (Test that switches role mid-test.)
2. Is the OpenAPI doc kept in sync via a CI check (zod → openapi diff)?
3. Are all logs tenant-scoped? (Linter rule that fails missing tenant_id.)
4. Is the WebSocket back-pressure handled (slow client cannot OOM the server)?
5. Is the rate limiter per-API-key, not per-IP?
```

---

## 13. Build the Reference Frontend (React + WebGPU)

```text
You are a senior React / WebGPU engineer. Build the SCC Studio frontend.

Layout: studio/frontend/
- src/main.tsx
- src/App.tsx
- src/routes/{Capture,Playback,Profiles,Settings}.tsx
- src/components/{CapturePanel,PlaybackPanel,AnalyticsPanel,EncoderSettings,VolumetricViewer}.tsx
- src/store/{auth,sensors,session,metrics,settings}.ts   (Zustand slices)
- src/api/{client.ts, ws.ts}
- src/wgpu/{viewer.ts, shaders/*.wgsl}
- src/styles/                (Tailwind 3.4)
- tests/                     (Vitest + Playwright)
- index.html, vite.config.ts, package.json

Constraints:
- React 18 + TypeScript strict.
- State: Zustand (single store, sliced).
- Styling: Tailwind 3.4 + CSS modules for the WebGPU canvas overlay.
- Routing: React Router 6.
- WebGPU: a single `<canvas>` rendered inside `VolumetricViewer`. Compute
  shader composites RGB + depth into a coloured point cloud; vertex shader
  applies camera matrix; fragment shader handles point sprites.
- Fallback: if WebGPU is unavailable, render a placeholder + "WebGPU
  unavailable" notice; v1.1 will provide WebGL2 fallback (out of MVP scope).
- No CSS-in-JS runtime. No styled-components. No Material UI.
- Akuma Engineering Ltd. brand: primary #0B4884, accent #1168BD,
  Inter font (or Calibri if Inter is unavailable in the environment).

Components — each owns one concern only:

  CapturePanel: sensor select + profile pick + Start/Stop button.
                Subscribes to sensors slice. No business logic — dispatches
                actions only.

  PlaybackPanel: scrubber + 3D viewport. Hosts VolumetricViewer.

  AnalyticsPanel: live bitrate, PSNR, SSIM, decoder-lag readouts.
                  Subscribes to metrics slice (WebSocket-driven).

  EncoderSettings: bit-depth, mode, motion-sensitivity slider.
                   Writes to settings slice; persisted to API on change.

  VolumetricViewer: ref-prop-driven, takes `{rgb, depth, width, height,
                    bitDepth}` from the metrics WebSocket and renders.
                    Provides orbit/pan/zoom controls.

Tests:
- Unit: Vitest + React Testing Library, ≥ 70% coverage on store slices.
- Visual: Playwright + Storybook (Chromatic optional) — golden screenshots
  for each component at desktop and mobile breakpoints.
- E2E: Playwright drives a full capture → playback flow against a mocked API.

Acceptance:
- Lighthouse: Performance ≥ 90, A11y ≥ 95, Best Practices = 100, SEO ≥ 90.
- Bundle: initial JS ≤ 350 KiB gzip; route-split chunks ≤ 80 KiB gzip.
- Keyboard accessibility on all controls; ARIA labels everywhere.
- Dark mode supported via Tailwind `dark:` variant.

Ultrathink Block:
1. Is the WebGPU code defensive against GPU device lost?
2. Are all controls reachable by keyboard alone? (Run axe-core in CI.)
3. Is the bundle under budget? (size-limit in CI.)
4. Is there a graceful degradation path for browsers without WebGPU?
5. Are brand colours tokenised (Tailwind theme), not hard-coded?
```

---

## 14. Build the Benchmark Harness (`scc-bench`)

```text
You are a senior C++17 engineer. Build the scc-bench CLI: a comparator
harness that benchmarks SCC against JPEG 2000, PNG, TIFF, and zlib on a
corpus of depth sequences. [REQ-024]

Layout: bench/
- src/main.cpp
- src/codecs/{scc.cpp, jpeg2000.cpp, png.cpp, tiff.cpp, zlib.cpp}
- src/metrics.cpp        (PSNR, SSIM, point-cloud diff)
- src/report.cpp         (CSV + HTML + JSON output)
- fixtures/              (depth sequences, see fixtures section)
- CMakeLists.txt
- README.md

CLI:
    scc-bench --corpus fixtures/                    \
              --codecs scc,jpeg2000,png,tiff,zlib   \
              --profiles lossless,lossy:high,lossy:streaming \
              --resolutions 640x480,1280x720,1920x1080       \
              --bit-depths 8,12,16                          \
              --out reports/                                \
              --format csv,html,json

For each (codec, profile, resolution, bit_depth, sequence) it records:
  - encode_throughput_fps
  - decode_throughput_fps
  - compression_ratio (vs raw 16bpp baseline)
  - per_frame_psnr_db
  - per_frame_ssim
  - point_cloud_diff_pct  (fraction of points moved by > tolerance)

External libraries:
  - OpenJPEG (JPEG 2000)
  - libpng
  - libtiff
  - zlib
  - CGAL or PCL for the point-cloud diff (PCL preferred; CGAL fallback)

Constraints:
- Single CLI binary, statically linked where possible.
- Output formats are spec'd: CSV is the canonical machine-readable form;
  HTML is for human review; JSON for downstream tooling.
- Reproducible: a `--seed` flag pins all random choices (frame ordering,
  PCL sampling).
- The bench MUST fail loud if a codec dependency is missing — no silent
  skips that hide regressions.

Tests:
- CI runs scc-bench on a tiny corpus (one 320×240, 30-frame sequence) on
  every PR; fails the build if SCC's compression_ratio regresses by > 2 pp
  or throughput by > 5 % vs the previous main.

Acceptance:
- bench/README.md explains the corpus, the metrics, and how to interpret
  the HTML report.
- A reference HTML report committed under bench/reports/sample/ shows what
  good looks like.
- The bench can be re-run by `bench/run-quick.sh` in under 5 minutes on
  a Ryzen 5600X.

Ultrathink Block:
1. Is the comparator harness fair (same input frames, identical loops)?
2. Are throughput numbers reported single-thread AND with optimal parallelism?
3. Is point-cloud diff reproducible across runs?
4. Is the corpus committed? Or downloaded with checksums?
5. Is the regression gate in CI tuned to avoid false positives from noise?
```

---

## 15. Build the Acceptance & Test plan automation

```text
You are a senior QA / DevEx engineer. Wire `acceptance/Acceptance_Criteria.md`
into the CI as an executable conformance suite.

Layout: ci/
- conformance/run.py
- conformance/cases/REQ-001.yaml ... REQ-080.yaml
- ci/github/workflows/conformance.yml

Each REQ-*.yaml entry has:
  id: REQ-NNN
  description: <one line>
  evidence: <patent col / standard clause>
  test_command: <shell, runs in repo root>
  pass_criteria: <regex, exit-code, or numerical threshold>
  blocking: true | false   # whether failure blocks merge

The runner executes each test, collects results, emits:
  - reports/conformance.html
  - reports/conformance.json
  - GitHub annotations linking failed REQs back to the SAD section

Constraints:
- Pure Python 3.11+ stdlib.
- No external CI dependencies beyond the test_command itself.
- Total runtime budget: 20 min on a standard GitHub-hosted runner; cases
  exceeding their per-case time budget are killed and reported as
  TIMEOUT (≠ FAIL but ≠ PASS).

Acceptance:
- conformance.html groups REQs by SAD section; click-through links to the
  source REQ file.
- A failing REQ produces a useful one-screen diagnostic, not a 10 KB log
  dump.
- The action reports "SCC conformance: 78 / 80 REQs PASS, 2 TIMEOUT" as
  a check status.

Ultrathink Block:
1. Are blocking and non-blocking REQs differentiated visually in the HTML?
2. Is the JSON report stable enough for downstream dashboards?
3. Is there a `--bisect` mode that runs only REQs whose evidence sources
   have changed since the last commit?
4. Is the conformance log linked from the SAD's §14?
5. Is REQ coverage of the SAD complete (every requirement in the doc has
   a YAML entry)?
```

---

## Closing note

Each prompt in this pack is intended to be **self-contained**: a competent
engineer with Claude Code in the loop should be able to drive the
implementation from the prompt alone, with the SAD as the broader
reference. If a prompt is missing something you need, add it, then file
a PR back into this document — the prompts are versioned alongside the SAD.

— Imhotep, on behalf of Akuma Engineering Ltd. — Architecture Practice

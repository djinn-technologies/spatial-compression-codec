# Spatial Compression Codec (SCC)

**A depth-aware codec for RGB-D video.** SCC compresses the depth
channel that accompanies colour video in 3D capture, telepresence, and
volumetric streaming pipelines — and packs the result inside an
ordinary H.264 bitstream so a single video file or RTP stream carries
both colour and depth.

```
┌──────────────────┐    ┌─────────────────────┐    ┌──────────────────┐
│ Depth sensor     │ →  │ SCC encoder (depth) │ →  │ H.264 SEI NAL    │
│ (RealSense /     │    │ + H.264 (colour)    │    │ wrapped bitstream│
│  Azure Kinect /  │    └─────────────────────┘    └──────────────────┘
│  ZED / iToF)     │                                       │
└──────────────────┘                                       ▼
                                                   ┌──────────────────┐
                                                   │ SCC decoder      │
                                                   │ → reconstructed  │
                                                   │   point cloud    │
                                                   └──────────────────┘
```

SCC implements US Patent **10,827,161 B2** ("Method for Real-Time
Compression of 3D Video Streaming Frames"), with a Range Asymmetric
Numeral Systems (rANS) entropy back-end and movement-aware pixel
interlacing. The full architecture is in [`docs/SAD.md`](docs/SAD.md).

---

## What it does

| Capability                              | How                                                          |
| --------------------------------------- | ------------------------------------------------------------ |
| **Lossless depth round-trip**           | Bit-exact reconstruction at 8 / 12 / 16 bpp                  |
| **Bounded-error lossy modes**           | `lossy:high` (visually lossless) and `lossy:streaming` (low-bitrate) |
| **Real-time throughput**                | ≥ 30 fps at 1280×720 / 12 bpp on a single Ryzen-class core   |
| **High compression ratio**              | Beats raw deflate / PNG / TIFF on every fixture; competitive with JPEG 2000 lossless and dominant on lossy depth |
| **H.264 in-band carriage**              | Compressed depth ships as a `user_data_unregistered` SEI NAL — the colour stream stays unmodified |
| **Movement-aware encoding**             | Quad-tree partitions still regions and applies pixel-level interlacing only where motion warrants it |
| **Cross-platform SDKs**                 | TypeScript/WASM, Python, Unity (UPM), Unreal (UPlugin), plus a stable C ABI |
| **Studio reference app**                | React + WebGPU UI, Fastify control plane, Rust capture agent — runs end-to-end against a synthetic depth source if no hardware is available |

---

## Try it in 5 minutes

The fastest way to see SCC working is the comparator harness, which
benchmarks SCC against zlib, PNG, TIFF, and JPEG 2000 on a deterministic
synthetic corpus and writes an HTML report you can open in a browser.

### Linux / macOS

```bash
# 1. Install the comparison codecs (one-time)
sudo apt-get install -y cmake ninja-build build-essential \
                        libpng-dev libtiff-dev libopenjp2-7-dev zlib1g-dev
# (macOS) brew install cmake ninja libpng libtiff openjpeg

# 2. Configure + build the harness
cmake -S . -B build \
      -DSCC_ENABLE_BENCH_HARNESS=ON \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build --target scc_bench -j

# 3. Run the demo bench (≈3 minutes on a modern laptop)
bench/run-quick.sh

# 4. Open the report
xdg-open bench/reports/quick/report.html   # Linux
open      bench/reports/quick/report.html  # macOS
```

You'll see a side-by-side comparison: encode/decode FPS (single-thread
and multi-thread), compression ratio, PSNR, SSIM, and a 3D
reconstruction-error metric, across three synthetic depth sequences
(moving plane, static room with sensor noise, textured sphere).

### Windows (PowerShell)

```powershell
# Install codecs via vcpkg (one-time)
vcpkg install libpng libtiff openjpeg zlib

cmake -S . -B build `
      -DSCC_ENABLE_BENCH_HARNESS=ON `
      -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
cmake --build build --target scc_bench --config Release

# Windows users: invoke the binary directly (the .sh wrapper is bash-only)
build\bench\Release\scc-bench.exe `
      --corpus bench\fixtures `
      --codecs scc,jpeg2000,png,tiff,zlib `
      --profiles lossless,lossy:high `
      --resolutions 320x240 `
      --bit-depths 12 `
      --runs 5 `
      --out bench\reports\quick `
      --format csv,json,html

start bench\reports\quick\report.html
```

A reference report shipped under
[`bench/reports/sample/index.html`](bench/reports/sample/index.html)
shows what good output looks like before you run anything.

---

## Try the Studio app (no depth camera required)

The Studio is the reference application: a web UI for capturing,
playing back, and configuring SCC streams. The capture agent ships
with a **mock sensor** that produces synthetic depth frames — so you
can run the full stack end-to-end on any laptop, no RealSense / Azure
Kinect / ZED required.

You'll need four terminals.

### Terminal 1 — Postgres (control-plane storage)

```bash
docker run --rm -d --name scc-studio-pg \
  -e POSTGRES_PASSWORD=scc -e POSTGRES_USER=scc_app -e POSTGRES_DB=scc_studio \
  -p 5432:5432 postgres:16
```

### Terminal 2 — Build libscc, then start the agent

```bash
# Build the C ABI shared library that the Rust agent links against.
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
cmake --build build --target scc_cabi -j

# Tell the agent where libscc lives, then launch with the mock sensor.
export SCC_LIB_DIR="$PWD/build/cabi"
cd studio/agent
cargo run --release -- --mock-sensor --api-url http://localhost:8080 \
                                     --api-key dev_key_replace_me
```

### Terminal 3 — Studio API

```bash
cd studio/api
npm install
cp .env.example .env       # the default values point at the Postgres above
npm run db:migrate
npm run dev                # listens on :8080
```

### Terminal 4 — Studio frontend

```bash
cd studio/frontend
npm install
npm run dev                # Vite dev server on :5173
```

Open [http://localhost:5173](http://localhost:5173) in a WebGPU-capable
browser (Chrome 113+, Edge 113+, or Firefox Nightly with the WebGPU
flag enabled).

You should be able to:

1. Pick **mock-0** from the sensor list.
2. Click **Start capture** — live synthetic depth frames render in the
   volumetric viewer; bitrate / FPS / encode-ms tick in the analytics
   panel.
3. Click **Stop**, then visit **Playback** — the recorded session
   appears and is replayable.

If WebGPU is unavailable, the viewer renders a fallback card; the rest
of the UI works unaffected.

---

## Use the codec from your own code

SCC ships first-class bindings so you don't have to wire up the C ABI
yourself unless you want to.

### TypeScript / WebAssembly

```bash
npm install @djinn/scc-wasm
```

```ts
import { Encoder, Decoder } from '@djinn/scc-wasm';

const enc = await Encoder.create({ width: 640, height: 480, bitDepth: 12,
                                    profile: 'lossy:high' });
const sei = enc.encode(depthFrame);   // depthFrame: Uint16Array

const dec = await Decoder.create();
const { data, width, height } = dec.decode(sei);
```

### Python

```bash
pip install scc-py
```

```python
import numpy as np, scc

enc = scc.Encoder(width=640, height=480, bit_depth=12, profile="lossy:high")
sei = enc.encode(depth_frame)            # depth_frame: np.uint16 array

dec = scc.Decoder()
out = dec.decode(sei)                    # zero-copy numpy view
```

### Unity

Add the package via *Window → Package Manager → Add package from disk*
and point at `sdk/unity/com.djinn.scc/package.json`.

```csharp
using Djinn.SCC;

using var encoder = new Encoder(SCCProfile.LossyHigh, bitDepth: 12, w, h);
byte[] sei = encoder.Encode(depthBuffer);   // NativeArray<ushort>
```

### Unreal Engine 5.3+

Copy `sdk/unreal/SCC/` into your project's `Plugins/` directory and
regenerate Visual Studio project files. The `USCCStreamComponent`
class exposes `BeginEncode` / `EncodeFrame` / `DecodeSEI` as Blueprint
nodes.

### C / C++ (libscc)

```c
#include <libscc.h>

scc_encoder_t* enc = NULL;
scc_encoder_create(640, 480, 12, SCC_PROFILE_LOSSY_HIGH, &enc);

uint8_t* out = NULL;
size_t   out_n = 0;
scc_encoder_encode(enc, depth_buffer, 640 * 480, &out, &out_n);

scc_buffer_free(out);
scc_encoder_destroy(enc);
```

The full ABI is in [`cabi/include/libscc.h`](cabi/include/libscc.h);
the contract is documented in [`docs/abi.md`](docs/abi.md).

---

## Repo layout

```
.
├── codec/        C++17 reference codec (rANS, disparity, frequency, quad-tree, SEI)
├── cabi/         libscc — stable C ABI used by every binding
├── sdk/
│   ├── wasm/     @djinn/scc-wasm    — WebAssembly + TypeScript adapter
│   ├── python/   scc-py             — pybind11 + scikit-build-core
│   ├── unity/    com.djinn.scc      — Unity Package Manager package
│   └── unreal/   SCC                — UPlugin (Unreal Engine 5.3+)
├── studio/
│   ├── agent/    scc-studio-agent   — Rust capture agent
│   ├── api/      Studio control plane (Fastify + Postgres)
│   └── frontend/ Studio UI (React 18 + WebGPU)
├── bench/        scc-bench          — comparator vs JPEG 2000 / PNG / TIFF / zlib
├── ci/           Conformance suite (executable Acceptance_Criteria.md)
└── docs/         SAD, ADRs, acceptance criteria, diagrams
```

Each subdirectory carries its own README with module-specific detail.

---

## Building from source

A complete build of every component requires several toolchains; you
only need the ones for the components you intend to build.

| Component                | Toolchain                                                    |
| ------------------------ | ------------------------------------------------------------ |
| `codec/`, `cabi/`, `bench/` | CMake **3.27+**, a C++17 compiler (gcc 9+ / clang 10+ / MSVC VS2019 16.10+) |
| `bench/` (extra deps)    | OpenJPEG 2.3+, libpng, libtiff, zlib                         |
| `sdk/wasm/`              | Emscripten **3.1.50+**, Node 20+                             |
| `sdk/python/`            | Python 3.10+, `scikit-build-core`, `pybind11`                |
| `sdk/unity/`             | Unity 2022.3 LTS or newer                                    |
| `sdk/unreal/`            | Unreal Engine 5.3+, MSVC VS2022                              |
| `studio/agent/`          | Rust **1.75+** (stable), `cargo`, `protoc`                   |
| `studio/api/`            | Node **20.10+**, Docker                                      |
| `studio/frontend/`       | Node **20.10+**, a WebGPU-capable browser                    |
| `ci/conformance/`        | Python **3.11+** (stdlib only)                               |

### Build the codec library + run unit tests

```bash
cmake -S . -B build -DSCC_ENABLE_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Catch2 v3 + rapidcheck are pulled via CMake `FetchContent` on the first
configure. For air-gapped builds, set
[`SCC_FETCHCONTENT_BASE_DIR`](docs/SAD.md#dependencies) to a pre-populated
cache, or build under vcpkg and supply
`-DCMAKE_TOOLCHAIN_FILE=<vcpkg>.cmake`.

### Sanitiser presets

```bash
cmake --preset asan          # Linux / macOS — ASan + UBSan
cmake --build --preset asan
ctest --preset asan
```

```powershell
cmake --preset msvc-asan     # Windows MSVC — ASan only
```

> Don't combine `Debug` build type with sanitisers on Windows — `/RTC1`
> conflicts with `/fsanitize=address`. The `msvc-asan` preset uses
> `RelWithDebInfo` for that reason.

---

## Verifying a release

Two automated gates ship in the repo:

- **`scc-bench`** ([`bench/`](bench/)) — runs each release against the
  committed performance + compression baseline at
  [`bench/reports/baseline.json`](bench/reports/baseline.json). Fails
  on > 5 % throughput regression or > 2 pp compression-ratio regression.
- **Conformance suite** ([`ci/conformance/`](ci/conformance/)) — every
  `REQ-NNN` in [`docs/Acceptance_Criteria.md`](docs/Acceptance_Criteria.md)
  is a YAML case the runner executes. Blocking failures fail the
  release.

```bash
# Bench gate
ctest --test-dir build -L bench

# Conformance gate
python ci/conformance/run.py --out reports/local
```

GitHub Actions wires both into CI; see [`.github/workflows/`](.github/workflows/).

---

## Documentation

- [**Solutions architecture document**](docs/SAD.md) — start here for
  the full design.
- [**Acceptance criteria**](docs/Acceptance_Criteria.md) — the source
  of truth for "done".
- [**Architecture decision records**](docs/adr/) — every locked
  decision with rationale (ADR-001 through ADR-015).
- [**C4 + sequence + state diagrams**](docs/) — `*.png` files render
  the system at every level (context, container, component, data flow).
- Component-level READMEs:
  - [`bench/README.md`](bench/README.md)
  - [`ci/conformance/README.md`](ci/conformance/README.md)
  - [`studio/agent/README.md`](studio/agent/README.md)
  - [`studio/api/README.md`](studio/api/README.md)
  - [`studio/frontend/README.md`](studio/frontend/README.md)
  - [`sdk/wasm/README.md`](sdk/wasm/README.md)
  - [`sdk/python/README.md`](sdk/python/README.md)
  - [`sdk/unity/com.djinn.scc/README.md`](sdk/unity/com.djinn.scc/README.md)
  - [`sdk/unreal/SCC/README.md`](sdk/unreal/SCC/README.md)

---

## Patent and licensing

SCC implements **US Patent 10,827,161 B2**, "Method for Real-Time
Compression of 3D Video Streaming Frames", owned by Djinn Technologies
Ltd. The patent is cited inline at every relevant code site (search
for `[US10827161B2 col. N]`). Use of SCC in jurisdictions where this
patent is in force may require a separate patent licence — contact
Djinn Technologies for terms.

Source code is distributed under the project's top-level
[`LICENSE`](LICENSE). Vendored third-party code carries its own
attribution; see [`codec/tests/vectors/`](codec/tests/vectors/) for
`ryg_rans` (public domain, used as a reference oracle for byte-equality
tests only — not linked into the runtime).

---

## Contributing

Contributions are welcome. Before opening a PR:

1. Run the codec tests (`ctest --test-dir build`) and ensure they pass
   with `cmake --preset asan`.
2. If you touch a SAD-traced requirement, update or add a YAML case
   under [`ci/conformance/cases/`](ci/conformance/cases/) and verify
   `python ci/conformance/run.py --no-execute` lints clean.
3. Run `bench/run-quick.sh` if you've changed any code under
   `codec/src/` to confirm there's no performance regression.
4. New architectural decisions land as a new ADR under
   [`docs/adr/`](docs/adr/).

The full contributor workflow lives in
[`docs/SAD.md` §16](docs/SAD.md#16-development-workflow).

---

*Maintained by [Djinn Technologies Ltd.](https://djinn.tech) — a
subsidiary of Akuma Engineering Ltd.*

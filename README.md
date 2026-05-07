# spatial-compression-codec

The **Spatial Compression Codec (SCC)** — a depth-first compression
codec plus the SDKs and Studio tooling that wrap it. The full solutions
architecture lives in [`docs/SAD.md`](docs/SAD.md); the executable
acceptance criteria live in [`docs/Acceptance_Criteria.md`](docs/Acceptance_Criteria.md).

## What's in this repo

```
.
├── codec/        C++17 reference codec (rANS, disparity, frequency, quad-tree, SEI)
├── cabi/         libscc — stable C ABI used by every binding
├── sdk/
│   ├── wasm/     @djinn/scc-wasm    — WebAssembly + TS adapter
│   ├── python/   scc-py             — pybind11 + scikit-build-core
│   ├── unity/    com.djinn.scc      — UPM package
│   └── unreal/   SCC                — UPlugin (UE 5.3+)
├── studio/
│   ├── agent/    scc-studio-agent   — Rust capture agent
│   ├── api/      Studio control-plane API (Fastify + Postgres RLS)
│   └── frontend/ Studio UI (React 18 + WebGPU)
├── bench/        scc-bench          — comparator harness vs jpeg2000/png/tiff/zlib
├── ci/           Conformance suite (executable Acceptance_Criteria.md)
└── docs/         SAD, ADRs, acceptance criteria
```

Each component carries its own `README.md` with subsystem-specific
detail. This top-level README is the roadmap and the host for the
quick-start commands.

---

## Toolchain setup (one-time)

You only need the toolchains for the components you intend to build.

| Component                | Toolchain                                                 |
| ------------------------ | --------------------------------------------------------- |
| `codec/`, `cabi/`, `bench/` | CMake **3.27+**, a C++17 compiler (gcc 9+ / clang 10+ / MSVC VS2019 16.10+) |
| `bench/` (extra)         | OpenJPEG 2.3+, libpng, libtiff, zlib                      |
| `sdk/wasm/`              | Emscripten **3.1.50+**, Node 20+                          |
| `sdk/python/`            | Python 3.10+, `pip install scikit-build-core pybind11`    |
| `sdk/unity/`             | Unity 2022.3 LTS or newer                                 |
| `sdk/unreal/`            | Unreal Engine 5.3+, MSVC VS2022, .NET 6 SDK               |
| `studio/agent/`          | Rust **1.75+** (stable), `cargo`, `protoc`                |
| `studio/api/`            | Node **20.10+**, Docker (Postgres testcontainer)          |
| `studio/frontend/`       | Node **20.10+**, a WebGPU-capable browser for e2e         |
| `ci/conformance/`        | Python **3.11+** (stdlib only)                            |

### Apt one-liners (Ubuntu 22.04+)

```bash
sudo apt-get update
sudo apt-get install -y \
  cmake ninja-build build-essential \
  libpng-dev libtiff-dev libopenjp2-7-dev zlib1g-dev \
  python3.11 python3-pip nodejs npm protobuf-compiler
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

### Brew one-liner (macOS)

```bash
brew install cmake ninja libpng libtiff openjpeg zlib python@3.11 node rustup protobuf
rustup-init -y
```

### winget one-liner (Windows 11)

```powershell
winget install Kitware.CMake Microsoft.VisualStudio.2022.BuildTools `
               OpenJS.NodeJS.LTS Python.Python.3.11 Rustlang.Rustup `
               Google.Protobuf
```

OpenJPEG / libpng / libtiff on Windows are most easily installed via
[vcpkg](https://vcpkg.io); pass `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>.cmake`
to the CMake configure step.

---

## Step-by-step: build the codec + run its tests

This is the smallest useful loop — build the C++ codec, run unit tests,
run the rANS micro-bench. ~2 minutes on a modern machine.

```bash
# 1. Configure
cmake -S . -B build -DSCC_ENABLE_TESTS=ON

# 2. Build
cmake --build build -j

# 3. Run unit tests
ctest --test-dir build --output-on-failure
```

The first configure pulls Catch2 v3 + rapidcheck via FetchContent; on
an air-gapped machine pre-populate
[`SCC_FETCHCONTENT_BASE_DIR`](docs/SAD.md#dependencies) or use a vcpkg
cache.

### Sanitiser presets

```bash
cmake --preset asan          # Linux / macOS — ASan + UBSan
cmake --build --preset asan
ctest --preset asan
```

```powershell
cmake --preset msvc-asan     # Windows MSVC — ASan only
cmake --build --preset msvc-asan
ctest --preset msvc-asan
```

> **MSVC ASan note.** Don't combine `Debug` build type with sanitisers
> on Windows. `/RTC1` conflicts with `/fsanitize=address`. The
> `msvc-asan` preset uses `RelWithDebInfo` for that reason.

### Module micro-benches

Per-module benches are gated by `SCC_ENABLE_BENCH=ON` (on by default at
top-level). Each is its own ctest entry under the `perf` label:

```bash
cmake --build build --target bench_rans bench_disparity bench_quadtree
build/bench/bench_rans
build/bench/bench_disparity
build/bench/bench_quadtree
ctest --test-dir build -L perf
```

Acceptance gates (see ADR-001 / -002 / -004):
rANS-8 encode > 200 MB/s · disparity > 1 GB/s · quadtree partition > 60 fps @ 1080p.

---

## Step-by-step: run the comparator harness (`scc-bench`)

`scc-bench` benchmarks SCC against jpeg2000, png, tiff, and zlib over a
deterministic synthetic corpus and emits CSV / JSON / HTML reports.
See [`bench/README.md`](bench/README.md) and ADR-014 for the full
methodology.

```bash
# 1. Install codec deps if you haven't already (Ubuntu)
sudo apt-get install -y libpng-dev libtiff-dev libopenjp2-7-dev zlib1g-dev

# 2. Configure with the harness target enabled
cmake -S . -B build \
      -DSCC_ENABLE_BENCH_HARNESS=ON \
      -DCMAKE_BUILD_TYPE=Release

# 3. Build the binary
cmake --build build --target scc_bench -j

# 4. Run the 5-minute developer bench
bench/run-quick.sh

# 5. Open the HTML report
xdg-open bench/reports/quick/report.html   # or `open` on macOS
```

The CI regression gate is wired through ctest:

```bash
ctest --test-dir build -L bench
```

---

## Step-by-step: run the conformance suite

Mechanical execution of every REQ in `docs/Acceptance_Criteria.md`. See
[`ci/conformance/README.md`](ci/conformance/README.md) and ADR-015.

```bash
# 1. Self-tests (no codec build required)
python -m unittest ci.conformance.tests.test_runner -v

# 2. Lint cases without spawning subprocesses
python ci/conformance/run.py --no-execute

# 3. Run only the cases whose evidence has changed since merge-base
python ci/conformance/run.py --bisect

# 4. Full local run (assumes `build/` exists with codec compiled)
python ci/conformance/run.py --out reports/local

# 5. View the report
xdg-open reports/local/conformance.html
```

The runner exits non-zero iff any **blocking** case failed. CI uses
`--gh-annotations` to surface failures inline on the PR.

---

## Step-by-step: bring up the full Studio stack

The Studio is three services running together: the **agent** captures
from a depth sensor, the **API** is the control plane (with Postgres),
and the **frontend** is the React UI. Local dev runs all three on one
machine.

### 1. Start Postgres (one terminal)

The Studio API uses [Postgres testcontainers](https://node.testcontainers.org/)
for tests, but local dev expects a long-lived Postgres. Easiest path:

```bash
docker run --rm -d \
  --name scc-studio-pg \
  -e POSTGRES_PASSWORD=scc \
  -e POSTGRES_USER=scc_app \
  -e POSTGRES_DB=scc_studio \
  -p 5432:5432 \
  postgres:16
```

### 2. Build + migrate + start the API (second terminal)

```bash
cd studio/api
npm install
cp .env.example .env             # edit DB_URL + JWT keys if needed
npm run db:migrate                # apply Drizzle migrations
npm run dev                       # listens on :8080
```

The API exposes `/api/v1/*`. Authentication is dual-track — API key for
service-to-service, RS256 JWT for the frontend (see ADR-012).

### 3. Build the codec libraries the agent links against (third terminal)

The agent's Rust crate links against `libscc` (the C ABI from
`cabi/`). Build it once:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
cmake --build build --target scc_cabi -j
```

This produces `build/cabi/libscc.{so,dylib,dll}`. The agent's
`build.rs` resolves it via `SCC_LIB_DIR`:

```bash
export SCC_LIB_DIR="$PWD/build/cabi"   # or set per-shell
```

### 4. Start the agent

```bash
cd studio/agent
cargo run --release -- --api-url http://localhost:8080 \
                       --api-key dev_key_replace_me
```

The agent registers with the API on startup, advertises its sensors
(`/v1/sensors`), and waits for capture-session commands.

### 5. Start the frontend (fourth terminal)

```bash
cd studio/frontend
npm install
npm run dev                       # Vite dev server on :5173
```

The dev server proxies `/api → http://localhost:8080` (HTTP +
WebSocket) so CORS is a non-issue. Open
[http://localhost:5173](http://localhost:5173) in a WebGPU-capable
browser (Chrome 113+, Edge 113+, or Firefox Nightly with the WebGPU
flag).

### Smoke test the stack

1. Sign in with the dev API key (or use the JWT issued by `npm run dev:token`).
2. The Capture page should populate with the agent's sensors.
3. Click **Start capture** — you should see live depth frames in the
   volumetric viewer and bitrate / FPS in the analytics panel.
4. Click **Stop**, then navigate to **Playback** — the session you
   just recorded should appear in the list.

If WebGPU is unavailable (older browser, headless Linux without
Vulkan), the viewer renders a fallback card explaining the
requirement; the rest of the UI is unaffected.

---

## Step-by-step: build a binding (SDK)

### WASM (`@djinn/scc-wasm`)

```bash
cd sdk/wasm
source /path/to/emsdk/emsdk_env.sh
npm install
npm run build           # writes dist/scc.{js,wasm,d.ts}
npm test
```

### Python (`scc-py`)

```bash
cd sdk/python
pip install -e ".[dev]"
pytest -q
```

`scikit-build-core` invokes CMake under the hood; the resulting wheel
embeds the same `libscc` C ABI used everywhere else.

### Unity

Open the Unity project (`sdk/unity/com.djinn.scc/`) in Unity 2022.3
LTS. The `Tests/EditMode` and `Tests/PlayMode` suites are runnable
from the **Test Runner** window. The package can be added to a host
project via *Window → Package Manager → Add package from disk*.

### Unreal

Copy `sdk/unreal/SCC/` into your project's `Plugins/` directory.
Generate project files (right-click the `.uproject` → *Generate Visual
Studio project files*), then build with VS 2022. Functional tests are
in `SCCRoundTripFunctionalTest.cpp`; run them from the Editor's
**Session Frontend → Automation** tab.

---

## Continuous integration

Every push and PR runs three workflows:

| Workflow                    | What it gates                                   |
| --------------------------- | ----------------------------------------------- |
| `codec.yml`                 | `cmake` build + `ctest` matrix (Linux / macOS / Windows) |
| `bench.yml`                 | `scc-bench` against `bench/reports/baseline.json` (ADR-014) |
| `conformance.yml`           | The `ci/conformance/` runner over every YAML case (ADR-015) |

Failed workflows surface inline on the PR; the conformance run also
emits `::error` annotations against the offending evidence files.

---

## Where to read next

- [`docs/SAD.md`](docs/SAD.md) — solutions architecture; start at §1.
- [`docs/Acceptance_Criteria.md`](docs/Acceptance_Criteria.md) — the
  source-of-truth REQ list.
- [`docs/adr/`](docs/adr/) — every architectural decision (ADR-001
  through ADR-015) with rationale.
- Component READMEs:
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

## Licensing

The codec implements US Patent 10,827,161 B2 (cited inline at every
relevant code site). Library code is licensed under the project's
top-level `LICENSE` file; vendored test fixtures (e.g. `ryg_rans` in
`codec/tests/vectors/`) carry their own attribution.

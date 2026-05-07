# ADR-011 — SCC Studio capture agent (`scc-studio-agent`)

| Field           | Value                                                              |
|-----------------|--------------------------------------------------------------------|
| Status          | Accepted                                                           |
| Date            | 2026-05-07                                                         |
| Supersedes      | —                                                                  |
| Superseded by   | —                                                                  |
| Evidence tags   | `[REQ-029]`, `[ADR-006]`                                           |

## Context

The SCC Studio is the developer-facing capture / preview / export
tool. It needs to talk to depth sensors (RealSense, Azure Kinect, ZED,
generic UVC) on the host. Two architectural choices were on the table:

1. **In-process** — embed the sensor + encoder directly in the
   Studio Electron app.
2. **Out-of-process daemon** — a separate native binary that owns the
   sensor handle and serves the Studio over an IPC socket.

Decisions to lock:

1. In-process vs out-of-process.
2. Implementation language and runtime model.
3. Sensor backend abstraction + feature gating.
4. IPC framing + forward-compat strategy.
5. Distribution model.
6. Privacy / trust boundary semantics.

## Decision

### 1. Out-of-process daemon (`scc-studio-agent`).

The agent runs as a separate process that owns the depth-sensor
permission. The Studio frontend (Electron, web, or a CLI) connects to
the agent over an IPC socket and drives sessions via protobuf. **The
frontend never sees raw depth pixels** — it sees only the post-encode
SEI byte payloads that the agent emits.

This buys us:

- **Trust boundary.** The agent is the single component with raw
  sensor access. A compromised Studio frontend cannot exfiltrate raw
  depth data; the worst it can do is start sessions and read post-
  encode bytes (which are already irreversibly the codec's output).
  The README documents this as the privacy guarantee.
- **Crash isolation.** Sensor SDK crashes (which happen) take down
  the agent, not the user-facing Studio.
- **Headless / CI use.** The agent runs without a GUI, exposes a
  scriptable IPC, and can be the only component installed on a
  headless capture rig.

### 2. Rust + Tokio for IPC + std::thread for compute.

Rust gives us:

- **Memory safety** at the FFI boundary, with explicit `unsafe`
  blocks each accompanied by a `// SAFETY:` comment justifying the
  invariant. [Ultrathink #1]
- **Static binaries** via `x86_64-unknown-linux-musl` and the
  per-target Cargo target.
- **First-class async** for the IPC server side (Tokio multi-thread
  runtime) without forcing async into the compute hot path.

The hot path (sensor capture + libscc encode) runs on `std::thread`s
with `crossbeam-channel` plus a lock-free `crossbeam::queue::ArrayQueue`
SPSC ring between sensor and encoder. The async runtime never sees a
raw frame; it only handles control RPCs.

A `criterion` bench (`benches/ring_buffer.rs`) measures the SPSC
ring's push+pop throughput at the agent's default capacity (8) and at
larger capacities. CI publishes the numbers; regressions fail the
gate. [Ultrathink #3]

### 3. `Sensor` trait with feature-gated backend modules.

A single `Sensor` trait abstracts every backend. Implementations live
under `src/sensors/{realsense,k4a,zed,uvc}.rs`, each gated by a Cargo
feature of the same name. The vendor SDK crates (librealsense2-sys,
k4a-sys, zed-sys, uvc-sys) are intentionally NOT pulled in by Cargo —
they would force every CI runner to install the corresponding vendor
SDK. Each backend ships as a v1 stub returning `SensorError::Unavailable`;
production builds add the real vendor crate behind the same feature
gate. [Ultrathink #2]

The `mock` backend is always compiled. It generates a deterministic
LCG-driven depth pattern so byte-equality round-trip tests work
without any hardware. CI exercises only the mock backend.

### 4. Length-prefixed protobuf on a Unix domain socket / Windows named pipe.

Wire format:

    [u32 BE length] [length bytes of prost-encoded protobuf]

Top-level envelopes are `AgentRequest` and `AgentResponse`, each with
a `oneof kind { ... }` that names the request/response variants. New
variants land in **strict ascending tag-number order**; old clients
ignore unknown fields and unknown oneof variants. proto3 forward
compat semantics hold. [Ultrathink #4]

The Unix-side listener (`tokio::net::UnixListener`) is the v1 path.
Windows named-pipe support (`tokio::net::windows::NamedPipeServer`) is
gated under `cfg(windows)` with a v2 stub that returns Unsupported;
the contract is identical, only the listener type changes. Documented
as an explicit follow-up.

### 5. `tracing` + OpenTelemetry OTLP for telemetry.

Structured logs via `tracing`. Trace export via OpenTelemetry's OTLP
gRPC exporter when `--otlp-endpoint <url>` is configured. The OTLP
exporter runs on the Tokio runtime; the sensor + encoder threads
emit spans without contending for the runtime.

When OTLP isn't configured, the OpenTelemetry layer is omitted and
only the formatter layer prints to stderr. The CLI flag is the only
toggle; no recompile needed.

### 6. Distribution: per-platform native binary inside an npm package.

`@djinn/scc-studio-agent` is the npm package. It ships:

- `bin/scc-studio-agent.js` — Node shim that detects
  `process.platform` + `process.arch` and execs the right binary.
- `binaries/<os>-<arch>/scc-studio-agent[.exe]` — the actual native
  binary, signed and notarised in CI.

Five target triples cover the matrix: linux-x64 (musl static),
linux-arm64 (musl static), darwin-x64 (notarised), darwin-arm64
(notarised), win32-x64 (Authenticode-signed). The `build-binaries.sh`
script is the per-target build recipe; CI orchestrates one platform
runner per triple, then merges the binaries into the npm package
and runs `npm publish --provenance`.

`.gitignore` excludes `npm/binaries/*/` so unsigned local builds
don't accidentally land in source control. The signing keys never
enter the repo.

## Consequences

### Positive

- Privacy guarantee is mechanically enforced: raw depth never crosses
  the agent boundary outside libscc.
- Sensor SDK crashes don't bring down the Studio frontend.
- Single-binary distribution per platform; `npm install -g
  @djinn/scc-studio-agent` is the install story for both developers
  and end users.
- Rust's borrow checker + the `// SAFETY:` discipline make the FFI
  surface auditable without a separate fuzz infrastructure.
- All four real sensor backends are feature-gated so a build without
  the K4A SDK installed succeeds.

### Negative

- Two-process architecture means an extra IPC hop on the control
  path. Not on the hot path: capture frames flow agent →
  libscc → file/network entirely in-process.
- `bindgen` requires `libclang` at build time; `prost-build`
  requires `protoc`. Both are CI-friendly tools but add build-host
  setup steps for new contributors.
- Real vendor sensor backends are v1 stubs; production deployments
  need the actual SDK integration which is non-trivial per backend
  (each vendor's SDK has its own threading model, lifetime
  semantics, and ergonomics).

### Deferred

- **Windows named-pipe IPC** (`tokio::net::windows::NamedPipeServer`).
  Stub today; v2 with the same protobuf contract.
- **libavformat-backed muxer.** v1 emits length-prefixed SEI bytes
  to a file/socket. Wrapping in a real H.264 NAL stream is the
  natural next step but adds a libav-sys dependency.
- **Real sensor SDK integration.** RealSense, K4A, ZED, UVC each
  need a dedicated PR with the vendor crate, a real
  `Sensor::next_frame` impl, and platform-specific build wiring.
- **Authenticode + Apple notarisation in CI.** The signing pipeline
  is sketched in `build-binaries.sh` (final step is "sign and
  notarise"); the CI workflow that actually does it is gated on
  the secrets being provisioned.

## Verification

- `cargo test` runs the unit + integration tests on the mock backend.
  - `tests/ipc_integration.rs` spawns the IPC server in-process on
    a tempfile-backed socket and drives a full session: ListSensors
    → StartSession → GetMetrics → StopSession → repeat-stop-errors
    → garbage-request-errors.
  - Each backend stub's `enumerate()` is callable; the trait's
    contract is the same whether the backend is the mock or
    a future real impl.
- `cargo clippy -- -D warnings` is the lint gate.
- `cargo audit` is the vulnerability gate.
- `cargo bench --bench ring_buffer` reports SPSC ring throughput
  (Ultrathink #3).
- `scc_bridge.rs` has a `// SAFETY:` comment immediately above
  every `unsafe { ... }` block; clippy's `missing_safety_doc`
  lint catches regressions (Ultrathink #1).
- The Cargo features (`realsense`, `k4a`, `zed`, `uvc`) gate
  module compilation only — no transitive vendor crate; building
  without `--features k4a` succeeds (Ultrathink #2).
- `protoc` / `prost-build` is the codegen toolchain;
  `cargo build` from a clean checkout regenerates the Rust types
  on every protocol edit (Ultrathink #4 — forward compat is
  baked into the proto3 semantics).
- The CI matrix builds the five target triples and signs each
  (Ultrathink #5; signing pipeline is the deferred CI follow-up).

The full build + test cycle requires Rust 1.75+, `protoc`, `libclang`,
and a built copy of `libscc.{so,dll,dylib}` reachable via
`SCC_LIBSCC_DIR`. The on-disk source is committed and ready for any
CI runner with those tools to exercise.

## References

- AI Build Prompt #11 (`docs/AI_Build_Prompts.md` §11).
- ADR-006 (C ABI) — the surface this binding wraps.
- ADR-007/008/009/010 (WASM, Python, Unity, Unreal) — sibling
  bindings sharing the same memory-ownership philosophy.
- Internal: `docs/SAD.md` §6.1.7 (SDK bindings); REQ-029 in
  `docs/Acceptance_Criteria.md`.

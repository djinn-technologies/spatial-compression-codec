# scc-studio-agent

Single-binary native daemon that owns the depth-sensor connection and
bridges to `libscc` over FFI. The SCC Studio frontend (an Electron app
or a CLI) connects to the agent's IPC socket and drives capture
sessions; the agent never exposes raw depth data over IPC, only the
post-encode SEI byte payloads.

[REQ-029, ADR-011]

## Architecture at a glance

```
+---------------------+        +---------------------+
| Studio frontend     | <----> | scc-studio-agent     | <---- USB / PCI
| (Electron / CLI)    |  IPC   | (this binary)        |
+---------------------+        +---------------------+
         ^                              |
         |                              v
   protobuf framing               libscc.so (FFI)
   on Unix socket /                     |
   Windows named pipe                   v
                              SCC SEI bytes -> muxer
                                         |
                                         v
                                file / network
```

- The **Studio frontend** is the only component that initiates capture.
  It sends `StartSession` requests and receives `SessionInfo` /
  `Metrics` responses. It NEVER sees raw depth pixels.
- The **agent** owns the sensor handle. It captures depth frames into
  a lock-free SPSC ring (`crossbeam::queue::ArrayQueue`) and drains
  the ring on a dedicated encoder thread that calls `libscc` via FFI.
- **libscc** does the actual compression; agent → libscc is the only
  raw-data crossing in the entire pipeline.

## Privacy model

The agent is the trust boundary between the OS-level sensor permission
and every other component. A compromised Studio frontend cannot
exfiltrate raw depth data; the worst it can do is start sessions and
read post-encode SEI bytes.

## Building

Requirements: Rust 1.75+, `protoc` (for `prost-build`), `libclang`
(for `bindgen`), and a built copy of the parent repo's `libscc`
shared library.

```bash
# 1. Build libscc (from the repo root):
cmake -S . -B build && cmake --build build --target scc_libscc

# 2. Build the agent:
cd studio/agent
SCC_LIBSCC_DIR="$(realpath ../../build/cabi)" cargo build --release
```

The `SCC_LIBSCC_DIR` env var tells `build.rs` where `libscc.{so,dll,dylib}`
lives; if unset, it defaults to `<repo>/build/cabi`.

### Static binary on Linux (musl)

```bash
rustup target add x86_64-unknown-linux-musl
cargo build --release --target x86_64-unknown-linux-musl
```

The release profile already strips symbols and uses fat LTO.

## Sensor backends

| Backend     | Cargo feature | Status      | Vendor SDK required at build |
|-------------|---------------|-------------|------------------------------|
| `mock`      | (built-in)    | Production  | No                            |
| `realsense` | `realsense`   | v1 stub     | librealsense2                 |
| `k4a`       | `k4a`         | v1 stub     | Azure Kinect SDK              |
| `zed`       | `zed`         | v1 stub     | Stereolabs ZED SDK            |
| `uvc`       | `uvc`         | v1 stub     | libuvc                        |

The four real backends are gated by Cargo features; default-build
includes only the `mock` sensor (always available, used by tests +
CI). [Ultrathink #2]

```bash
# Build without K4A: works without the K4A SDK installed.
cargo build --release

# Build with the RealSense stub (no SDK needed for the stub):
cargo build --release --features realsense
```

The v1 stub returns `SensorError::Unavailable`; production builds will
swap in the vendor crate behind the same feature gate.

## IPC contract

See `src/proto/control.proto`. Wire framing: each message on the
socket is preceded by a big-endian `u32` length, followed by exactly
that many bytes of `prost`-encoded protobuf. New fields land in
ascending tag-number order so v1 clients can ignore them safely.
[Ultrathink #4]

## Tracing

`tracing` for structured logs; OpenTelemetry OTLP for trace export
when `--otlp-endpoint <url>` (or `SCC_AGENT_OTLP_ENDPOINT`) is set.
The OTLP exporter runs on the Tokio runtime; the sensor + encoder
threads emit spans without contending for an async runtime.

## Tests

```bash
cargo test                           # unit + integration
cargo clippy -- -D warnings         # lint gate
cargo audit                         # vulnerability gate (cargo-audit pre-installed)
cargo bench --bench ring_buffer     # SPSC ring throughput  [Ultrathink #3]
```

## Distribution

The agent is published as the npm package `@djinn/scc-studio-agent`
which bundles per-platform pre-built binaries:

| Triple                         | Path inside the npm package           |
|--------------------------------|---------------------------------------|
| `x86_64-unknown-linux-musl`    | `binaries/linux-x64/`                 |
| `aarch64-unknown-linux-musl`   | `binaries/linux-arm64/`               |
| `x86_64-apple-darwin`          | `binaries/darwin-x64/`                |
| `aarch64-apple-darwin`         | `binaries/darwin-arm64/`              |
| `x86_64-pc-windows-msvc`       | `binaries/win32-x64/`                 |

Code signing (Authenticode for Windows, Apple notarisation for macOS)
is handled in CI; signing keys never enter this repo. [Ultrathink #5]

See `npm/README.md` for the consumer-facing install instructions.

## License

Apache-2.0.

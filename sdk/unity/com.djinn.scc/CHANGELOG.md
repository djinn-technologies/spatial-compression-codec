# Changelog

All notable changes to `com.djinn.scc` are documented here.

## [1.0.0] - 2026-05-07

### Added

- Initial release.
- `Djinn.SCC.Encoder` — managed wrapper around `scc_encode_frame`.
- `Djinn.SCC.Decoder` — managed wrapper around `scc_decode_sei`.
- `Djinn.SCC.SCCProfile` — profile enum (Lossless / LossyHigh / LossyStreaming).
- `Djinn.SCC.SCCImporter` — `ScriptedImporter` for `.scc` test fixtures.
- Burst-compatible `DepthConvert` helper for staging RT pixels into
  uint16 NativeArray buffers.
- EditMode tests: round-trip + 100 K-cycle disposal soak.
- Sample: BasicCapture (encode → decode → point-cloud Mesh).

### Platform support

- Unity 2022.3 LTS, Unity 6.0+ LTS.
- Standalone: Windows AMD64, Linux x86_64, macOS Intel + Apple Silicon.
- Mobile: Android arm64-v8a, iOS arm64.
- Both Mono and IL2CPP scripting backends.

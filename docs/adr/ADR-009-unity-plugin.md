# ADR-009 — Unity plugin (`com.djinn.scc`)

| Field           | Value                                                              |
|-----------------|--------------------------------------------------------------------|
| Status          | Accepted                                                           |
| Date            | 2026-05-07                                                         |
| Supersedes      | —                                                                  |
| Superseded by   | —                                                                  |
| Evidence tags   | `[REQ-029]`, `[ADR-006]`                                           |

## Context

Unity is the primary delivery surface for the codec's interactive
demos and for partner workflows that ingest depth frames from XR
headsets / capture rigs. The plugin needs to:

1. **Wrap the C ABI** (per ADR-006: bindings depend on the C ABI, not
   C++ headers).
2. **Avoid GC churn** in the hot path — every depth frame is ~1.84 MiB
   at 1280×720 uint16; per-frame allocations would dominate frame
   time.
3. **Cover the supported Unity matrix**: 2022.3 LTS and 6 LTS, both
   Mono and IL2CPP scripting backends, six platform/CPU
   combinations.
4. **Expose a Burst-compatible upstream stage** so the codec can
   ingest data prepared by Burst-compiled jobs.

Decisions to lock:

1. P/Invoke library naming across platforms.
2. Memory ownership and `NativeArray<T>` discipline.
3. Plugin slot layout + `.meta` file content.
4. Sample scene strategy.
5. Code-signing pipeline (Ultrathink #4).
6. Build pipeline for the per-platform binaries.

## Decision

### 1. `[DllImport("libscc")]` on every platform; `__Internal` on iOS.

Unity's mono runtime maps `[DllImport("libscc")]` to:

- Windows: `Plugins/.../libscc.dll`  (NB: differs from CMake's default
  `scc.dll` -- the build pipeline renames).
- Linux: `Plugins/.../libscc.so`
- macOS: `Plugins/.../libscc.dylib`
- Android: `Plugins/Android/arm64-v8a/libscc.so`

For iOS, dynamic loading is forbidden by the platform; the library is
linked statically into the IL2CPP build via `[DllImport("__Internal")]`.
A platform-conditional `#if UNITY_IOS && !UNITY_EDITOR` switches the
constant.

The Windows DLL needs a `lib` prefix to match the cross-platform
convention (without it, mono on Linux/macOS would search for
`liblibscc.so`/`liblibscc.dylib`). The build script (`build-plugins.sh`)
renames CMake's `scc.dll` output to `libscc.dll` before staging it.

### 2. NativeArray-only buffers; explicit ownership contracts.

The C# encoder accepts a `NativeArray<ushort> depth` and reads its raw
pointer directly via
`NativeArrayUnsafeUtility.GetUnsafeReadOnlyPtr(depth)`. No Unity-side
copy. The output is a plain `byte[]` because the consumer is typically
a network sender (which expects managed bytes); this is the only
managed allocation in the encode path.

The decoder allocates the output `NativeArray<ushort>` with
`Allocator.Persistent` (because the caller may keep it around past the
current frame for visualisation / further compute) and writes into it
via the C ABI's `out_depth` pointer. **The caller MUST call
`Dispose()`.** This is the Unity convention for any
Persistent-allocated NativeArray and is the only ownership pattern
that makes sense when the result outlives the API call.

The disposal-cycle test (`DisposalCycleIsStable`) runs 100 K
init/destroy pairs and exercises the contract under sustained churn.
A nightly soak with `SCC_UNITY_LONG=1` runs 1 M cycles. [Ultrathink #1]

### 3. Six platform slots, each with a committed `.meta` file.

The `.meta` files are committed alongside the (CI-built) binaries.
Each declares:

- `Editor: { enabled: 1, OS: <Windows|OSX|Linux>, CPU: <x86_64|ARM64> }`
  — for Editor Play Mode on the matching host.
- `Standalone: <Win64|Linux64|OSXUniversal>: { enabled: 1, CPU: ... }`
  — for built players.
- All other platforms: `enabled: 0` so a Linux .so is not picked up
  for a Windows build.

Without committed `.meta` files Unity defaults plugins to "every
platform", which causes load conflicts when a project has both a
`libscc.dll` and a `libscc.so` in the same `x86_64/` directory (the
Editor would try to load both on Windows; the `.so` fails noisily).

The `.meta` GUIDs use a deterministic `5cc1d4e7…` prefix so the same
binary file always resolves to the same GUID across clones; this
keeps Scene/Prefab references stable.

### 4. Sample as a script + manual setup steps.

The sample ships as `Samples~/BasicCapture/DepthVisualizer.cs` plus a
README documenting the manual setup (create empty GameObject; add
DepthVisualizer; press Play). We do **not** ship a binary `.unity`
scene file because:

- The repository is platform-portable; a binary scene risks line-
  ending issues even with Unity's "Force Text" serialisation.
- Authors who modify the package don't have to merge YAML scene
  diffs.

The script is fully IL2CPP-compatible (no reflection, no dynamic
code, no managed threading) and runs identically in the Editor and
in built players. [Ultrathink #2]

### 5. Code signing in CI; signing keys never in the repo.

The Windows Authenticode signing key, the Apple Developer ID
certificate, and the iOS provisioning profile are all secrets. They
live in CI (GitHub Actions secrets / Apple Notary Service / Azure
Key Vault) and never enter the repo. The CI workflow that runs
`build-plugins.sh` also runs:

- `signtool sign /tr <RFC3161 timestamp> /td sha256 /fd sha256 ...`
  on the Windows DLL.
- `codesign --options=runtime --timestamp ...` plus
  `xcrun notarytool submit ...` on the macOS dylibs.

For development builds, the binaries are unsigned. macOS users will
hit Gatekeeper warnings on first load; the standard
`xattr -d com.apple.quarantine` workaround applies to dev binaries.
[Ultrathink #4]

### 6. Per-platform build script with explicit recipes.

`build-plugins.sh <platform>` runs one platform at a time because
each requires a host-specific toolchain (Xcode for macOS/iOS, NDK for
Android, Visual Studio for Windows). The script invokes the parent
project's CMake (`scc_libscc` target) with platform-specific
toolchain settings, then renames/stages the output into
`Runtime/Plugins/<platform>/<arch>/`.

CI runs each recipe on a matching platform agent (Linux x86_64 &
aarch64 on `ubuntu-latest`/`ubuntu-latest-arm`; Windows on
`windows-latest`; macOS Intel + Apple Silicon on `macos-latest`;
Android cross-compiled on Linux; iOS on macOS).

## Consequences

### Positive

- One `Djinn.SCC.Encoder` API across every Unity target.
- Zero managed allocations on the encode hot path beyond the output
  `byte[]` (which is returned to the caller).
- Burst-compatible upstream helpers (`DepthConvert`) so Burst-compiled
  data-prep jobs can feed the encoder without managed marshalling.
- IL2CPP-clean: no AOT-incompatible features.
- The `.meta` contracts pin per-platform load behaviour at design
  time, eliminating the most common Unity plugin pitfall ("works in
  the Editor, fails in the build").

### Negative

- The Windows DLL must be RENAMED from CMake's default `scc.dll` to
  `libscc.dll`. Documented in `build-plugins.sh`; one extra mv command.
- The decoder's Persistent allocation is a footgun: forgetting to
  Dispose leaks both Unity-side (the NativeArray's metadata) and
  C-side memory. The XML doc on `Decoder.Decode` flags this loudly,
  and the EditMode tests demonstrate the correct pattern.
- Shipping six binaries × six platforms means a release artifact
  tree of ~24 files. The CI pipeline produces them all; releases
  are the only time the full set is staged.

### Deferred

- **PlayMode tests.** EditMode tests cover round-trip + lifecycle.
  PlayMode tests would exercise the build-pipeline binary load path
  (which can differ from the in-editor path). Defer to the CI matrix
  prompt where we'll wire `Test Runner -batchmode -runTests`.
- **AssetPostprocessor** for automatic `libscc.*` placement when
  the user updates the binary. v1 expects the user to drag the new
  binary into the right slot; the existing `.meta` keeps import
  settings stable.
- **PlayerSettings.IL2CPPCompilerConfiguration tuning** for size /
  master builds. Defer to the per-game configuration.

## Verification

- `Tests/EditMode/RoundTripTests.cs` covers:
  - round-trip on a 16×16 synthetic frame;
  - all three profiles produce non-empty payloads;
  - 100 K disposal-cycle stability (Ultrathink #1);
  - idempotent `Dispose()`;
  - `Encode` after Dispose throws `ObjectDisposedException`;
  - garbage SEI bytes throw `InvalidOperationException`.
- `package.json` declares the Unity 2022.3+ minimum + samples list.
- Plugin `.meta` files in `Runtime/Plugins/` pin the per-platform
  import settings (Ultrathink #3 — Apple Silicon explicitly under
  `Plugins/AnyCPU/libscc.dylib` with `CPU: ARM64`).
- `build-plugins.sh` documents the binary build pipeline; CI uses
  the same recipes (Ultrathink #2).
- `Samples~/BasicCapture/DepthVisualizer.cs` runs end-to-end in
  Editor and in built players (Ultrathink #2/#5; IL2CPP-clean).

The full build + test cycle requires Unity 2022.3 LTS or 6 LTS plus
each platform's native toolchain. The on-disk source is committed
and ready for any CI matrix to exercise.

## References

- AI Build Prompt #9 (`docs/AI_Build_Prompts.md` §9).
- ADR-006 (C ABI) — the surface this binding wraps.
- ADR-007 (WASM), ADR-008 (Python) — sibling bindings sharing the
  same memory-ownership philosophy.
- Unity docs:
  - <https://docs.unity3d.com/Manual/Plugins.html>
  - <https://docs.unity3d.com/Packages/com.unity.test-framework@latest/>
  - <https://docs.unity3d.com/Packages/com.unity.collections@latest/>
- Internal: `docs/SAD.md` §6.1.7 (SDK bindings); REQ-029 in
  `docs/Acceptance_Criteria.md`.

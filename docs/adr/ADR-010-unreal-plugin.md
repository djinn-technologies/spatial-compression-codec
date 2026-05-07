# ADR-010 — Unreal Engine plugin (`sdk/unreal/SCC`)

| Field           | Value                                                              |
|-----------------|--------------------------------------------------------------------|
| Status          | Accepted                                                           |
| Date            | 2026-05-07                                                         |
| Supersedes      | —                                                                  |
| Superseded by   | —                                                                  |
| Evidence tags   | `[REQ-029]`, `[ADR-006]`, `[ADR-009]`                              |

## Context

Unreal Engine is the second major real-time engine target alongside
Unity (ADR-009). The plugin needs to:

1. **Wrap the C ABI** (per ADR-006).
2. **Expose Blueprint-callable methods** so designers can drive
   encode/decode from visual scripts.
3. **Cook into shipping builds** without dragging in editor-only
   modules (Ultrathink #4).
4. **Build for the Fortnite-class platform matrix**: Windows AMD64,
   Linux x86_64, macOS Universal, Android arm64 (Quest/Pico), iOS
   arm64 (Ultrathink #1).
5. **Pass headless `RunUnreal` automation** in CI without a binary
   map asset (Ultrathink #2).
6. **Be Fab-marketplace-ready** with the necessary descriptor +
   icon (Ultrathink #4).
7. **Pin the libscc SDK version** so a runtime mismatch is detectable
   from Blueprint (Ultrathink #5).

## Decision

### 1. Single Runtime module + UActorComponent surface.

The plugin exposes one module (`SCC`) of `Type: "Runtime"` and one
public `UCLASS` (`USCCStreamComponent`) deriving from
`UActorComponent`. All public methods are `UFUNCTION(BlueprintCallable)`
and use Blueprint-friendly types (`TArray<uint8>`, `TArray<uint16>`,
`int32`, `FString`) — no raw pointers, no `std::` types.

The runtime module avoids editor dependencies: `UnrealEd`, `Slate`,
etc. are NOT in the dependency list. `FunctionalTesting` is added as
a *private* dependency only for non-Shipping builds, and the
`AFunctionalTest`-derived class is gated on `WITH_AUTOMATION_TESTS`
so it cooks out of shipping builds.

A single component holds **two** `scc_ctx*` handles: one for encoding
(created by `BeginEncode`, destroyed by `EndEncode`), one for
decoding (lazily created on first `DecodeSEI`, destroyed in
`BeginDestroy`). This matches the prompt's API and avoids a separate
`USCCDecoderComponent` while keeping Blueprint discoverability tight.

### 2. Per-platform Build.cs branches with delay-load on Windows.

`SCC.Build.cs` selects libscc binding by `Target.Platform`:

| Platform | Linkage                                        |
|----------|------------------------------------------------|
| Win64    | `PublicAdditionalLibraries` += `libscc.lib`    |
|          | `PublicDelayLoadDLLs` += `libscc.dll`          |
|          | `RuntimeDependencies` += `libscc.dll`          |
| Linux    | `PublicAdditionalLibraries` += `libscc.so`     |
|          | `PublicRuntimeLibraryPaths` += LinuxDir         |
| Mac      | `PublicAdditionalLibraries` += `libscc.dylib`  |
| Android  | arch-specific `libscc.so` for arm64-v8a         |
| iOS      | `PublicAdditionalLibraries` += `libscc.a` (static) |

Windows uses MSVC's delay-load mechanism — the DLL isn't touched until
the first symbol call, and `FSCCModule::StartupModule` explicitly
loads it via `FPlatformProcess::GetDllHandle` from the plugin's
installed `Source/ThirdParty/SCC/Win64/libscc.dll`. This avoids
LoadLibrary's default search order picking up an unrelated
`libscc.dll` from PATH. [Ultrathink #1]

iOS forbids dynamic loading, so `libscc.a` is statically linked into
the IL2CPP build; the same C ABI surface is reached through the
linker rather than the dynamic loader.

### 3. SDK version pinned in Build.cs and exposed at runtime.

```csharp
public const string SCCSDKVersion = "1.0.0";
PublicDefinitions.Add("SCC_SDK_VERSION=\"" + SCCSDKVersion + "\"");
```

The runtime has access to the version via the `SCC_SDK_VERSION` macro,
exposed through `USCCStreamComponent::GetSCCSDKVersion()` (Blueprint-
callable, BlueprintPure). A shipping app can compare this against an
expected value and refuse to load if the libscc binary it shipped is
out of sync with the plugin headers. [Ultrathink #5]

Bumping this version in the same PR that ships a new libscc binary is
part of the release checklist.

### 4. TArray buffers feed the C ABI by raw pointer.

`TArray<uint16>::GetData()` returns `uint16*` to a contiguous
allocation; `reinterpret_cast<const uint8*>(GetData())` is a
zero-copy view that we pass straight to `scc_encode_frame`. Output
allocation is a single `SetNumUninitialized(W * H)` call; the
decoder writes into the same buffer via
`reinterpret_cast<uint8*>(OutDepth.GetData())`.

No per-element copy on either side — the prompt's "Memcpy, not
per-element copy" requirement maps to "Unreal's TArray IS the
contiguous buffer". [Ultrathink #3]

### 5. Two-tier test coverage.

- **`AFunctionalTest`** (`ASCCRoundTripFunctionalTest`): an actor
  placed in `Content/SCC/Tests/SCCRoundTripMap.umap` (created
  in-editor). Runs in PIE and reports pass/fail through the Test
  Automation window. Suitable for developer-facing iteration.
- **Automation tests** (`IMPLEMENT_SIMPLE_AUTOMATION_TEST`): no
  map required; the test constructs a `USCCStreamComponent` in
  memory and exercises the round-trip. Runs in headless CI via
  `UnrealEditor-Cmd <Project>.uproject -ExecCmds="Automation RunTests
  SCC." -unattended -nopause -nullrhi -log`. [Ultrathink #2]

The binary `.umap` file is NOT committed — Unreal map assets are
binary and routinely create merge conflicts. The
`Content/SCC/Tests/README.md` documents the manual one-time editor
setup steps. The CI gate is the headless automation tests.

### 6. Fab marketplace readiness.

The `SCC.uplugin` includes a `Marketplace` block with a placeholder
`FabIdentifier` (Fab assigns the real UUID on submission). The
`Resources/Icon128.png` slot ships a placeholder text file documenting
the requirement; the real PNG is dropped into the plugin tree as part
of the release process and the absence of the placeholder is a
release-checklist item.

`prepare-distribution.sh` is the release-side script that:
- Copies `cabi/include/libscc.h` into
  `Source/ThirdParty/SCC/Include/` so the plugin no longer depends on
  the parent repo at build time.
- Verifies that all six platform binaries are staged.
- Verifies the icon is present.
- Zips the plugin tree into `dist/SCC-<version>.zip` ready for Fab
  upload.

Code signing (Authenticode + Apple notarisation) is delegated to the
parent repo's CI workflow; signing keys never enter the plugin source
tree.

## Consequences

### Positive

- Single Blueprint-callable surface (`USCCStreamComponent`) that's
  discoverable in the editor's Components menu and in the Place Actors
  panel.
- `TArray` semantics give zero-copy buffer interop without exposing
  raw pointers in Blueprint.
- Per-platform Build.cs branches keep linkage decisions in one place.
- `bUsePrecompiled = false` means projects rebuild from source against
  their engine version — no API drift across UE 5.3, 5.4, 5.5, 6.0+.
- Headless automation tests cover CI without committing binary map
  assets.

### Negative

- Six platform binaries × two configurations (Development +
  Shipping) = 12 artefacts per release. Build pipeline is in CI; not
  reproducible from a single host.
- `bUsePrecompiled = false` means consumers compile the plugin into
  their own project, lengthening initial build time. The trade is
  compatibility across UE versions; precompiled plugins would need
  a new build per engine release.
- The `void*` opaque-context fields on `USCCStreamComponent`
  intentionally avoid leaking `libscc.h` types into the public
  header; this means the type is `void*` in Blueprint reflection
  too, which is fine because users never see it (it's `private`).

### Deferred

- **PlayerController integration** for sample maps. Deferred to the
  developer; the FunctionalTest doc shows the minimum setup.
- **Niagara depth-particle visualiser**. The Unity sample renders a
  point cloud Mesh; a Niagara-based equivalent for Unreal is a
  natural next step but adds a dependency on the Niagara module.
- **Auto-update of `SCCSDKVersion` from CMake**. Today the constant
  is hand-bumped; a future build script could parse the parent
  CMake's project version and write the value into Build.cs.

## Verification

- `Source/SCC/Private/SCCAutomationTests.cpp` cases:
  - `SCC.RoundTrip.SyntheticFrame` — full encode/decode/byte-equality.
  - `SCC.RoundTrip.EncodeWithoutBegin` — `EncodeFrame` returns empty
    bytes if `BeginEncode` not called.
  - `SCC.RoundTrip.DecodeOnGarbage` — malformed bytes return false.
  - `SCC.RoundTrip.AllProfiles` — every profile produces a non-empty
    bytestream.
- `ASCCRoundTripFunctionalTest` provides the in-editor map-based
  variant.
- `SCC.uplugin` declares `Type: "Runtime"` so the plugin cooks into
  shipping builds (Ultrathink #4).
- `SCC.Build.cs` per-platform branches handle every supported target
  (Ultrathink #1).
- `SCCSDKVersion` constant pinned and exposed via
  `GetSCCSDKVersion()` (Ultrathink #5).
- `prepare-distribution.sh` packages a Fab-ready zip.

The full build + test cycle requires Unreal Engine 5.3 LTS or newer
plus each platform's native toolchain. The on-disk source is committed
and ready for any CI matrix to exercise.

## References

- AI Build Prompt #10 (`docs/AI_Build_Prompts.md` §10).
- ADR-006 (C ABI) — the surface this plugin wraps.
- ADR-007 (WASM), ADR-008 (Python), ADR-009 (Unity) — sibling
  bindings sharing the same memory-ownership philosophy.
- Unreal docs:
  - <https://dev.epicgames.com/documentation/en-us/unreal-engine/plugins-in-unreal-engine>
  - <https://dev.epicgames.com/documentation/en-us/unreal-engine/automation-system-overview>
  - <https://dev.epicgames.com/documentation/en-us/unreal-engine/integrating-third-party-libraries-into-unreal-engine>
- Internal: `docs/SAD.md` §6.1.7 (SDK bindings); REQ-029 in
  `docs/Acceptance_Criteria.md`.

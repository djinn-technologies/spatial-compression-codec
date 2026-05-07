# SCC — Spatial Compression Codec (Unreal Engine plugin)

Encode and decode H.264-compatible depth-frame SEI payloads from Unreal
Engine 5.3+ Blueprints or C++. Wraps the `libscc` C ABI under the hood.

## Install

The plugin lives at `sdk/unreal/SCC/`. To use it in your own project:

1. **Copy** the `SCC/` directory into `<YourProject>/Plugins/`.
2. **Generate project files** (right-click `<YourProject>.uproject`
   → "Generate Visual Studio project files" on Windows, or
   `<UE>/Engine/Build/BatchFiles/Linux/GenerateProjectFiles.sh` on
   Linux).
3. **Build** the project (Visual Studio, Xcode, or Rider).
4. **Open** the project in Unreal. The plugin appears in
   `Edit → Plugins → SCC`.

## Quick start (Blueprint)

1. Add an `SCCStreamComponent` to any actor.
2. Call `Begin Encode` with `Width=1280, Height=720, BitDepth=12,
   Profile="LossyHigh"`.
3. On every depth frame, call `Encode Frame(Depth)` where `Depth` is
   a `TArray<uint16>` of `Width * Height` samples (e.g. from a
   render-texture readback).
4. Send the returned `TArray<uint8>` over your network channel.

## Quick start (C++)

```cpp
#include "SCCStreamComponent.h"

USCCStreamComponent* Comp =
    NewObject<USCCStreamComponent>(GetWorld());
Comp->BeginEncode(/*Width*/1280, /*Height*/720,
                  /*BitDepth*/12, TEXT("LossyHigh"));

TArray<uint16> Depth;
Depth.SetNumUninitialized(1280 * 720);
// ... fill Depth from your sensor / RT readback ...

TArray<uint8> SEI = Comp->EncodeFrame(Depth);
// ... send SEI over the wire ...

Comp->EndEncode();
```

## Profiles

| Profile           | Use case                                          |
|-------------------|---------------------------------------------------|
| `Lossless`        | Capture / archival. Bit-exact round-trip.        |
| `LossyHigh`       | Visual fidelity prioritised over bandwidth.       |
| `LossyStreaming`  | Real-time streaming. Lower bitrate, motion-aware. |

## Platform matrix

| Platform        | Architecture | Plugin path                                              | Linkage              |
|-----------------|--------------|----------------------------------------------------------|----------------------|
| Windows         | x86_64       | `Source/ThirdParty/SCC/Win64/libscc.{dll,lib}`            | Delay-load + import lib |
| Linux           | x86_64       | `Source/ThirdParty/SCC/Linux/libscc.so`                  | Shared, RPATH=$ORIGIN |
| macOS           | universal    | `Source/ThirdParty/SCC/Mac/libscc.dylib`                 | Shared                |
| Android         | arm64-v8a    | `Source/ThirdParty/SCC/Android/arm64-v8a/libscc.so`      | Shared                |
| iOS / iPadOS    | arm64        | `Source/ThirdParty/SCC/IOS/libscc.a`                     | **Static** (iOS forbids dynamic loading) |

The plugin module is `Type: Runtime`, so it cooks into shipping
builds. Set `bUsePrecompiled = false` is the default; rebuild from
source on each engine version. [Ultrathink #4]

## SDK version pinning

The libscc SDK version is pinned in `Source/SCC/SCC.Build.cs`:

```csharp
public const string SCCSDKVersion = "1.0.0";
```

Bumping it in lockstep with a new libscc release is part of the
release checklist. The runtime exposes the pinned version via
`USCCStreamComponent::GetSCCSDKVersion()` (Blueprint-callable) so a
shipping app can sanity-check against its expected version.
[Ultrathink #5]

## Testing

The plugin ships two test surfaces:

- **Headless automation tests** in
  `Source/SCC/Private/SCCAutomationTests.cpp` (`SCC.RoundTrip.*`).
  Run via `UnrealEditor-Cmd <Project>.uproject
  -ExecCmds="Automation RunTests SCC."  -unattended -nopause -nullrhi
  -log` -- works in CI without a map. [Ultrathink #2]
- **In-map FunctionalTest** (`ASCCRoundTripFunctionalTest`) for
  developer-facing testing in PIE. The map asset itself
  (`Content/SCC/Tests/SCCRoundTripMap.umap`) is created in-editor;
  see `Content/SCC/Tests/README.md`.

## Distribution / Epic Fab marketplace

The plugin is structured for direct Fab submission:

- `SCC.uplugin`'s `Marketplace` block has a `FabIdentifier` slot
  filled in on submission.
- `Resources/Icon128.png` is required (currently a placeholder; see
  `Resources/Icon128.png.placeholder` for the requirements).
- `prepare-distribution.sh` stages the binaries, copies `libscc.h`
  into `Source/ThirdParty/SCC/Include/`, verifies the icon, and zips
  everything into `dist/SCC-<version>.zip`.

Code signing (Authenticode for `libscc.dll`, Apple notarisation for
`libscc.dylib`) is handled by the parent repository's CI workflow;
signing keys never enter the plugin source tree.

## Source layout

```
sdk/unreal/SCC/
├── SCC.uplugin
├── README.md  (this file)
├── prepare-distribution.sh
├── Resources/
│   └── Icon128.png  (binary; not committed -- see placeholder)
├── Content/
│   └── SCC/Tests/  (FunctionalTest .umap goes here, created in-editor)
└── Source/
    ├── SCC/
    │   ├── SCC.Build.cs
    │   ├── Public/
    │   │   ├── SCC.h
    │   │   ├── SCCStreamComponent.h
    │   │   └── SCCRoundTripFunctionalTest.h
    │   └── Private/
    │       ├── SCC.cpp
    │       ├── SCCStreamComponent.cpp
    │       ├── SCCRoundTripFunctionalTest.cpp
    │       └── SCCAutomationTests.cpp
    └── ThirdParty/
        └── SCC/
            ├── Include/   (libscc.h)
            ├── Win64/     (libscc.dll + libscc.lib)
            ├── Linux/     (libscc.so)
            ├── Mac/       (libscc.dylib)
            ├── Android/arm64-v8a/  (libscc.so)
            └── IOS/       (libscc.a)
```

## License

Apache-2.0. See the top-level `LICENSE` of the spatial-compression-codec
repository.

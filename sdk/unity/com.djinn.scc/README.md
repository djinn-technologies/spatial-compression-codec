# SCC — Spatial Compression Codec (Unity Package)

Encode and decode H.264-compatible depth-frame SEI payloads from Unity,
using the `libscc` C ABI under the hood. Compatible with Unity 2022.3
LTS and Unity 6 LTS.

## Install

The package is delivered as a **Unity Package Manager** package. Add it
to your project either by:

- **Git URL** — `Window → Package Manager → + → Add package from git URL`,
  paste the path to this folder.
- **Tarball** — `npm pack` (or zip) this directory and `Add package
  from tarball...`.
- **Local path** — for in-development use, link
  `"com.djinn.scc": "file:../path/to/com.djinn.scc"` into your project's
  `Packages/manifest.json`.

After installation you'll find the **Basic Capture** sample under
`Window → Package Manager → SCC → Samples`. Click "Import" to copy it
into your project, then follow `Samples~/BasicCapture/README.md`.

## Quick start

```csharp
using Unity.Collections;
using Djinn.SCC;

const int W = 1280, H = 720;
using var depth = new NativeArray<ushort>(W * H, Allocator.Temp);
// ... fill depth from your sensor / render-texture readback ...

using var enc = new Encoder(SCCProfile.LossyHigh, bitDepth: 12,
                            width: W, height: H);
using var dec = new Decoder();

byte[] sei = enc.Encode(depth);

using var seiNa = new NativeArray<byte>(sei, Allocator.Temp);
NativeArray<ushort> decoded = dec.Decode(seiNa,
    out int Wo, out int Ho, out int BDo);
try
{
    // ... use decoded ...
}
finally
{
    decoded.Dispose();   // Decoder.Decode allocates Persistent; you own it.
}
```

## API

```csharp
namespace Djinn.SCC
{
    public enum SCCProfile { Lossless, LossyHigh, LossyStreaming }

    public sealed class Encoder : IDisposable
    {
        public Encoder(SCCProfile profile, int bitDepth, int width, int height);
        public byte[] Encode(NativeArray<ushort> depth);
        public void Dispose();
    }

    public sealed class Decoder : IDisposable
    {
        public Decoder();
        public NativeArray<ushort> Decode(NativeArray<byte> sei,
                                          out int width, out int height,
                                          out int bitDepth);
        public void Dispose();
    }

    [Unity.Burst.BurstCompile]
    public static class DepthConvert
    {
        public static void FloatMetresToUInt16Millimetres(
            in NativeArray<float> srcMetres,
            ref NativeArray<ushort> dstMillimetres);
        public static void Copy(
            in NativeArray<ushort> src,
            ref NativeArray<ushort> dst);
    }
}
```

`Encoder.Encode` reads the input `NativeArray<ushort>` directly via its
unsafe pointer — **no Unity-side copy** of the depth pixels.

`Decoder.Decode` returns a freshly-allocated `NativeArray<ushort>`
allocated with `Allocator.Persistent`. **The caller is responsible for
calling `.Dispose()` on it.** [Ultrathink #1]

## Profiles

| Profile           | Use case                                          |
|-------------------|---------------------------------------------------|
| `Lossless`        | Capture / archival. Bit-exact round-trip.        |
| `LossyHigh`       | Visual fidelity prioritised over bandwidth.       |
| `LossyStreaming`  | Real-time streaming. Lower bitrate, motion-aware. |

## Platform matrix

| Platform               | Plugin path                               | Status |
|------------------------|-------------------------------------------|--------|
| Windows AMD64          | `Plugins/x86_64/libscc.dll`               | ✓      |
| Linux x86_64           | `Plugins/x86_64/libscc.so`                | ✓      |
| macOS Intel            | `Plugins/x86_64/libscc.dylib`             | ✓      |
| macOS Apple Silicon    | `Plugins/AnyCPU/libscc.dylib`             | ✓      |
| Android arm64-v8a      | `Plugins/Android/arm64-v8a/libscc.so`     | ✓      |
| iOS arm64              | `Plugins/iOS/libscc.a` (static)           | ✓      |

Both Mono and IL2CPP scripting backends are supported on all platforms.
The package contains no reflection-emit, dynamic code generation, or
managed threading.

The `.meta` files in each Plugins subdirectory describe the Unity
import settings; the binaries themselves are produced by
`build-plugins.sh` (run per-platform — see the script header for the
recipe).

## Code signing

Production releases of `libscc.dll` (Windows Authenticode) and the
macOS dylibs (Apple notarisation) are signed in CI. The signing
configuration lives in the parent repository's CI workflows and is
deferred from the package source tree (signing keys never enter the
repository). [Ultrathink #4]

## Testing

EditMode tests cover the round-trip, all three profiles, the
disposal-cycle stability soak (100 K iterations by default; 1 M with
`SCC_UNITY_LONG=1`), idempotent disposal, and error paths.

Run via `Window → General → Test Runner → EditMode → Run All`.

## License

Apache-2.0. See `LICENSE.md`.

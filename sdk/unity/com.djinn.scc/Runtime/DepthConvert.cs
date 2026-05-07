// sdk/unity/com.djinn.scc/Runtime/DepthConvert.cs
//
// Burst-compatible helper for staging depth pixels into the uint16
// NativeArray buffer that the SCC encoder consumes.
//
// The encoder itself cannot be Burst-compiled because it P/Invokes the
// C ABI -- Burst does not support FFI calls. This helper is the
// upstream stage that DOES run inside Burst, so the per-pixel
// conversion (scaling, clamping, byte-order) is auto-vectorised.
//
// Evidence: [REQ-029, ADR-009] (Burst integration pattern).

using Unity.Burst;
using Unity.Collections;
using Unity.Mathematics;

namespace Djinn.SCC
{
    /// <summary>
    /// Burst-compiled depth-conversion helpers. Use these to feed the
    /// <see cref="Encoder"/> from heterogeneous source formats.
    /// </summary>
    [BurstCompile]
    public static class DepthConvert
    {
        /// <summary>
        /// Convert a NativeArray of float depth values (e.g. metres,
        /// from a render-texture readback) into uint16 millimetres,
        /// clamped to <c>[0, 65535]</c>.
        /// </summary>
        [BurstCompile(CompileSynchronously = false)]
        public static void FloatMetresToUInt16Millimetres(
            in NativeArray<float> srcMetres,
            ref NativeArray<ushort> dstMillimetres)
        {
            int n = srcMetres.Length;
            for (int i = 0; i < n; ++i)
            {
                float mm = srcMetres[i] * 1000f;
                int clamped = (int)math.clamp(mm, 0f, 65535f);
                dstMillimetres[i] = (ushort)clamped;
            }
        }

        /// <summary>
        /// Copy a uint16 NativeArray into another uint16 NativeArray of
        /// the same length. Acts as a Burst-friendly equivalent of
        /// <c>memcpy</c> when the caller wants to keep the original
        /// buffer alive while the encoder reads a private copy.
        /// </summary>
        [BurstCompile(CompileSynchronously = false)]
        public static void Copy(
            in NativeArray<ushort> src,
            ref NativeArray<ushort> dst)
        {
            int n = src.Length;
            for (int i = 0; i < n; ++i) dst[i] = src[i];
        }
    }
}

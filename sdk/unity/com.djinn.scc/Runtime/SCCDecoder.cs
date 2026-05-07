// sdk/unity/com.djinn.scc/Runtime/SCCDecoder.cs
//
// Public Unity API for SCC frame decoding.
//
// Evidence: [REQ-029, ADR-009].

using System;
using Unity.Collections;
using Unity.Collections.LowLevel.Unsafe;

namespace Djinn.SCC
{
    /// <summary>
    /// SCC decoder. Wraps a single <c>scc_ctx*</c> from the C ABI.
    ///
    /// <para>
    /// The decoder is NOT thread-safe; allocate one per worker thread if
    /// parallel decoding is required.
    /// </para>
    ///
    /// <para>
    /// <b>Memory ownership:</b> <see cref="Decode"/> returns a fresh
    /// <see cref="NativeArray{T}"/> allocated with
    /// <see cref="Allocator.Persistent"/>. The caller MUST
    /// <see cref="NativeArray{T}.Dispose()"/> it. [Ultrathink #1]
    /// </para>
    /// </summary>
    public sealed class Decoder : IDisposable
    {
        private IntPtr m_Ctx;
        private bool m_Disposed;

        public Decoder()
        {
            m_Ctx = NativeMethods.scc_init();
            if (m_Ctx == IntPtr.Zero)
                throw new InvalidOperationException("scc_init returned null");
        }

        /// <summary>
        /// Decode SEI bytes back to a depth frame.
        /// </summary>
        /// <param name="sei">A NativeArray containing the SEI payload bytes.</param>
        /// <param name="width">Output: the recovered frame width in pixels.</param>
        /// <param name="height">Output: the recovered frame height in pixels.</param>
        /// <param name="bitDepth">Output: the recovered bit-depth flag.</param>
        /// <returns>
        /// A freshly-allocated <see cref="NativeArray{T}"/> of <c>ushort</c>
        /// holding <c>width * height</c> samples (Allocator.Persistent --
        /// caller MUST Dispose).
        /// </returns>
        public unsafe NativeArray<ushort> Decode(NativeArray<byte> sei,
                                                 out int width,
                                                 out int height,
                                                 out int bitDepth)
        {
            if (m_Disposed)
                throw new ObjectDisposedException(nameof(Decoder));
            if (!sei.IsCreated)
                throw new ArgumentException("sei NativeArray is not created", nameof(sei));
            if (sei.Length == 0)
                throw new ArgumentException("sei NativeArray is empty", nameof(sei));

            byte* seiPtr = (byte*)NativeArrayUnsafeUtility.GetUnsafeReadOnlyPtr(sei);
            UIntPtr seiLen = (UIntPtr)(uint)sei.Length;

            // Probe dimensions: pass null for out_depth -> SCC_INVALID_ARG
            // is expected, but the dimensions are populated.
            int W = 0, H = 0, BD = 0;
            NativeMethods.scc_decode_sei(
                m_Ctx,
                seiPtr,
                seiLen,
                /*out_depth=*/ null,
                /*out_stride=*/ UIntPtr.Zero,
                out W, out H, out BD);
            if (W <= 0 || H <= 0)
                throw new InvalidOperationException(
                    "scc_decode_sei probe failed: " + NativeMethods.ReadLastError(m_Ctx));

            // Allocate the output as a NativeArray<ushort> (Persistent --
            // caller owns and must Dispose).
            var depth = new NativeArray<ushort>(W * H, Allocator.Persistent,
                NativeArrayOptions.UninitializedMemory);
            try
            {
                byte* depthPtr = (byte*)NativeArrayUnsafeUtility.GetUnsafePtr(depth);
                UIntPtr stride = (UIntPtr)((ulong)W * sizeof(ushort));

                int rc = NativeMethods.scc_decode_sei(
                    m_Ctx,
                    seiPtr,
                    seiLen,
                    depthPtr,
                    stride,
                    out W, out H, out BD);
                if (rc != 0)
                {
                    string err = NativeMethods.ReadLastError(m_Ctx);
                    depth.Dispose();
                    throw new InvalidOperationException("scc_decode_sei: " + err);
                }
                width = W;
                height = H;
                bitDepth = BD;
                return depth;
            }
            catch
            {
                if (depth.IsCreated) depth.Dispose();
                throw;
            }
        }

        /// <inheritdoc />
        public void Dispose()
        {
            if (m_Disposed) return;
            m_Disposed = true;
            if (m_Ctx != IntPtr.Zero)
            {
                NativeMethods.scc_destroy(m_Ctx);
                m_Ctx = IntPtr.Zero;
            }
        }

        ~Decoder()
        {
            if (m_Ctx != IntPtr.Zero)
            {
                NativeMethods.scc_destroy(m_Ctx);
                m_Ctx = IntPtr.Zero;
            }
        }
    }
}

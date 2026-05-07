// sdk/unity/com.djinn.scc/Runtime/SCCEncoder.cs
//
// Public Unity API for SCC frame encoding.
//
// Evidence: [REQ-029, ADR-009].

using System;
using Unity.Collections;
using Unity.Collections.LowLevel.Unsafe;

namespace Djinn.SCC
{
    /// <summary>
    /// Compression profile selector. Maps to the same three profiles
    /// exposed by the WASM and Python bindings.
    /// </summary>
    public enum SCCProfile
    {
        /// <summary>Bit-exact round-trip; mode_flags bit 0 set.</summary>
        Lossless,
        /// <summary>Default. Visual fidelity prioritised over bandwidth.</summary>
        LossyHigh,
        /// <summary>Real-time streaming. Lower bitrate, motion-aware.</summary>
        LossyStreaming,
    }

    /// <summary>
    /// SCC encoder. Wraps a single <c>scc_ctx*</c> from the C ABI.
    ///
    /// <para>
    /// Lifecycle: construct once with the frame dimensions; call
    /// <see cref="Encode"/> per frame; call <see cref="Dispose"/> (or use
    /// <c>using</c>) to release the underlying native context.
    /// </para>
    ///
    /// <para>
    /// The encoder is NOT thread-safe; allocate one per worker thread if
    /// parallel encoding is required.
    /// </para>
    /// </summary>
    public sealed class Encoder : IDisposable
    {
        private IntPtr m_Ctx;
        private readonly int m_Width;
        private readonly int m_Height;
        private readonly int m_BitDepth;
        private bool m_Disposed;

        /// <summary>Construct an encoder for a fixed (width, height) frame.</summary>
        /// <param name="profile">One of <see cref="SCCProfile"/>.</param>
        /// <param name="bitDepth">Wire bit-depth flag: 8, 12, or 16.</param>
        /// <param name="width">Frame width in pixels.</param>
        /// <param name="height">Frame height in pixels.</param>
        public Encoder(SCCProfile profile, int bitDepth, int width, int height)
        {
            if (bitDepth != 8 && bitDepth != 12 && bitDepth != 16)
                throw new ArgumentException(
                    $"bitDepth must be 8, 12, or 16; got {bitDepth}",
                    nameof(bitDepth));
            if (width <= 0 || width > 65535)
                throw new ArgumentOutOfRangeException(nameof(width));
            if (height <= 0 || height > 65535)
                throw new ArgumentOutOfRangeException(nameof(height));

            m_Ctx = NativeMethods.scc_init();
            if (m_Ctx == IntPtr.Zero)
                throw new InvalidOperationException("scc_init returned null");

            m_Width = width;
            m_Height = height;
            m_BitDepth = bitDepth;
            try
            {
                ApplyProfile(profile);
            }
            catch
            {
                NativeMethods.scc_destroy(m_Ctx);
                m_Ctx = IntPtr.Zero;
                throw;
            }
        }

        private void ApplyProfile(SCCProfile profile)
        {
            string modeFlags, topCount, tauStatic, tauLow;
            switch (profile)
            {
                case SCCProfile.Lossless:
                    modeFlags = "1"; topCount = "4"; tauStatic = "1";  tauLow = "5";
                    break;
                case SCCProfile.LossyHigh:
                    modeFlags = "0"; topCount = "4"; tauStatic = "5";  tauLow = "30";
                    break;
                case SCCProfile.LossyStreaming:
                    modeFlags = "2"; topCount = "4"; tauStatic = "10"; tauLow = "50";
                    break;
                default:
                    throw new ArgumentOutOfRangeException(nameof(profile),
                        $"unknown profile {(int)profile}");
            }
            SetParam("mode_flags", modeFlags);
            SetParam("top_count",  topCount);
            SetParam("tau_static", tauStatic);
            SetParam("tau_low",    tauLow);
        }

        private void SetParam(string key, string value)
        {
            int rc = NativeMethods.scc_set_param(m_Ctx, key, value);
            if (rc != 0)
                throw new InvalidOperationException(
                    $"scc_set_param {key}={value}: {NativeMethods.ReadLastError(m_Ctx)}");
        }

        /// <summary>
        /// Encode one depth frame to an SEI payload.
        ///
        /// <para>
        /// The input <paramref name="depth"/> is read directly via its
        /// unsafe pointer -- <b>no copy</b> into a managed/intermediate
        /// buffer. The buffer must hold <c>width * height</c> uint16
        /// samples in row-major order.
        /// </para>
        /// </summary>
        /// <param name="depth">A NativeArray of <c>width * height</c> uint16 samples.</param>
        /// <returns>A new managed <see cref="byte"/>[] containing the SEI bytes.</returns>
        public unsafe byte[] Encode(NativeArray<ushort> depth)
        {
            if (m_Disposed)
                throw new ObjectDisposedException(nameof(Encoder));
            if (!depth.IsCreated)
                throw new ArgumentException("depth NativeArray is not created", nameof(depth));
            if (depth.Length != m_Width * m_Height)
                throw new ArgumentException(
                    $"depth length {depth.Length} != width*height {m_Width * m_Height}",
                    nameof(depth));

            byte* depthPtr = (byte*)NativeArrayUnsafeUtility.GetUnsafeReadOnlyPtr(depth);
            UIntPtr stride = (UIntPtr)((ulong)m_Width * sizeof(ushort));

            // Probe required output size.
            UIntPtr required;
            NativeMethods.scc_encode_frame(
                m_Ctx,
                depthPtr,
                stride,
                m_Width, m_Height, m_BitDepth,
                /*out_sei=*/ null,
                /*out_cap=*/ UIntPtr.Zero,
                out required);
            ulong needed = (ulong)required;
            if (needed == 0)
                throw new InvalidOperationException(
                    "scc_encode_frame probe failed: " + NativeMethods.ReadLastError(m_Ctx));
            if (needed > int.MaxValue)
                throw new InvalidOperationException(
                    $"scc_encode_frame requested {needed} bytes; exceeds int.MaxValue");

            byte[] result = new byte[(int)needed];
            UIntPtr written;
            fixed (byte* outPtr = result)
            {
                int rc = NativeMethods.scc_encode_frame(
                    m_Ctx,
                    depthPtr,
                    stride,
                    m_Width, m_Height, m_BitDepth,
                    outPtr,
                    required,
                    out written);
                if (rc != 0)
                    throw new InvalidOperationException(
                        "scc_encode_frame: " + NativeMethods.ReadLastError(m_Ctx));
            }

            ulong actual = (ulong)written;
            if (actual != needed)
            {
                if (actual > int.MaxValue)
                    throw new InvalidOperationException("encoder returned implausible out_len");
                Array.Resize(ref result, (int)actual);
            }
            return result;
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

        ~Encoder()
        {
            // Defensive: fire if user forgot Dispose. Native ctx leaks are
            // far worse than a finalizer log line.
            if (m_Ctx != IntPtr.Zero)
            {
                NativeMethods.scc_destroy(m_Ctx);
                m_Ctx = IntPtr.Zero;
            }
        }
    }
}

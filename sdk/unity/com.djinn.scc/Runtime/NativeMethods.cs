// sdk/unity/com.djinn.scc/Runtime/NativeMethods.cs
//
// P/Invoke shim for the libscc C ABI.
//
// Library naming:
//   - Windows : Plugins/x86_64/libscc.dll
//   - Linux   : Plugins/x86_64/libscc.so
//   - macOS   : Plugins/x86_64/libscc.dylib  (Intel)
//               Plugins/AnyCPU/libscc.dylib  (Apple Silicon)
//   - Android : Plugins/Android/arm64-v8a/libscc.so
//   - iOS     : Plugins/iOS/libscc.a (linked statically -> "__Internal")
//
// The CMake build of cabi/ produces `scc.dll` / `libscc.so` / `libscc.dylib`.
// The Windows DLL must be RENAMED to `libscc.dll` before being copied into
// Plugins/x86_64/ -- see build-plugins.sh.
//
// Calling convention: cdecl (matches C standard ABI).
//
// Evidence: [REQ-029, ADR-006 (C ABI), ADR-009].

using System;
using System.Runtime.InteropServices;

namespace Djinn.SCC
{
    internal static class NativeMethods
    {
#if UNITY_IOS && !UNITY_EDITOR
        // iOS forbids dynamic loading; libscc.a is linked statically into
        // the IL2CPP build.
        internal const string LibName = "__Internal";
#else
        // All other platforms: matches `libscc.{dll,so,dylib}` per the
        // package's Plugins/ layout.
        internal const string LibName = "libscc";
#endif

        // ---- Lifecycle ------------------------------------------------------

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr scc_init();

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void scc_destroy(IntPtr ctx);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr scc_version();

        // ---- Parameters -----------------------------------------------------

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int scc_set_param(
            IntPtr ctx,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string key,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string value);

        // ---- Frame encode / decode -----------------------------------------
        //
        // size_t is platform-dependent (8 bytes on 64-bit, 4 on 32-bit). We
        // use UIntPtr to match. Unity targets are all 64-bit nowadays, so
        // ulong values fit, but UIntPtr keeps us safe on hypothetical 32-bit
        // future targets.

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern unsafe int scc_encode_frame(
            IntPtr ctx,
            byte* depth,
            UIntPtr depth_stride,
            int width,
            int height,
            int bit_depth,
            byte* out_sei,
            UIntPtr out_cap,
            out UIntPtr out_len);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern unsafe int scc_decode_sei(
            IntPtr ctx,
            byte* sei,
            UIntPtr sei_len,
            byte* out_depth,
            UIntPtr out_stride,
            out int out_width,
            out int out_height,
            out int out_bit_depth);

        // ---- Errors ---------------------------------------------------------

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr scc_get_last_error(IntPtr ctx);

        // ---- Convenience: read C string into managed string -----------------

        internal static string ReadLastError(IntPtr ctx)
        {
            if (ctx == IntPtr.Zero) return string.Empty;
            IntPtr p = scc_get_last_error(ctx);
            return p == IntPtr.Zero ? string.Empty : Marshal.PtrToStringUTF8(p) ?? string.Empty;
        }
    }
}

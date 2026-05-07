#!/usr/bin/env bash
# sdk/unity/com.djinn.scc/build-plugins.sh
#
# Build libscc for every Unity-supported platform and stage the
# binaries into Runtime/Plugins/<platform>/<arch>/. Requires:
#
#   - Linux x86_64 / aarch64:  cmake + ninja + g++
#   - macOS Intel + ARM64   :  cmake + Xcode CLT (run on a Mac)
#   - Windows x86_64        :  cmake + MSBuild  (run inside the VS Developer shell)
#   - Android arm64-v8a     :  cmake + the Android NDK (NDK_ROOT env var)
#   - iOS arm64             :  cmake + Xcode    (run on a Mac)
#
# Each platform is run independently in CI; this script provides the
# canonical recipe for a developer to reproduce one platform locally.
#
# Naming: the CMake target produces `scc.dll` on Windows and
# `libscc.{so,dylib}` on Unix-likes. We rename the Windows DLL to
# `libscc.dll` so the C# `[DllImport("libscc")]` resolution is
# uniform across platforms.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
PLUGINS_DIR="${SCRIPT_DIR}/Runtime/Plugins"

# CMake invocation for the parent project's libscc target. Builds in
# isolation so we can rebuild per-platform without polluting the dev
# tree.
build_libscc() {
    local target_arch="$1"     # "x86_64" / "arm64" / "android-arm64" / "ios-arm64"
    local build_dir="$2"
    local cmake_args=("$@")
    cmake_args=("${cmake_args[@]:2}")  # drop the first two args

    cmake -S "${REPO_ROOT}" -B "${build_dir}" \
          -G Ninja \
          -DCMAKE_BUILD_TYPE=Release \
          -DSCC_ENABLE_TESTS=OFF \
          -DSCC_ENABLE_BENCH=OFF \
          "${cmake_args[@]}"
    cmake --build "${build_dir}" --target scc_libscc -j
}

# ---------------------------------------------------------------------------
# Per-platform recipes (uncomment / dispatch to whichever you need).
# ---------------------------------------------------------------------------

case "${1:-}" in
    windows-x86_64)
        # Run inside the VS Developer shell.
        build_libscc x86_64 "${REPO_ROOT}/build-unity-win64"
        cp "${REPO_ROOT}/build-unity-win64/cabi/scc.dll" \
           "${PLUGINS_DIR}/x86_64/libscc.dll"
        ;;
    linux-x86_64)
        build_libscc x86_64 "${REPO_ROOT}/build-unity-linux64"
        cp "${REPO_ROOT}/build-unity-linux64/cabi/libscc.so" \
           "${PLUGINS_DIR}/x86_64/libscc.so"
        ;;
    macos-x86_64)
        build_libscc x86_64 "${REPO_ROOT}/build-unity-macos-intel" \
            -DCMAKE_OSX_ARCHITECTURES=x86_64
        cp "${REPO_ROOT}/build-unity-macos-intel/cabi/libscc.dylib" \
           "${PLUGINS_DIR}/x86_64/libscc.dylib"
        ;;
    macos-arm64)
        build_libscc arm64 "${REPO_ROOT}/build-unity-macos-arm64" \
            -DCMAKE_OSX_ARCHITECTURES=arm64
        cp "${REPO_ROOT}/build-unity-macos-arm64/cabi/libscc.dylib" \
           "${PLUGINS_DIR}/AnyCPU/libscc.dylib"
        ;;
    android-arm64)
        : "${NDK_ROOT:?Set NDK_ROOT to the Android NDK path}"
        build_libscc android-arm64 "${REPO_ROOT}/build-unity-android-arm64" \
            -DCMAKE_TOOLCHAIN_FILE="${NDK_ROOT}/build/cmake/android.toolchain.cmake" \
            -DANDROID_ABI=arm64-v8a \
            -DANDROID_PLATFORM=android-26
        cp "${REPO_ROOT}/build-unity-android-arm64/cabi/libscc.so" \
           "${PLUGINS_DIR}/Android/arm64-v8a/libscc.so"
        ;;
    ios-arm64)
        # iOS forbids dynamic loading; switch the cabi target to STATIC
        # for this build via -DSCC_LIBSCC_STATIC=ON (a flag the cabi
        # CMakeLists honours when set).
        build_libscc ios-arm64 "${REPO_ROOT}/build-unity-ios-arm64" \
            -DCMAKE_SYSTEM_NAME=iOS \
            -DCMAKE_OSX_ARCHITECTURES=arm64 \
            -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
            -DSCC_LIBSCC_STATIC=ON
        cp "${REPO_ROOT}/build-unity-ios-arm64/cabi/libscc.a" \
           "${PLUGINS_DIR}/iOS/libscc.a"
        ;;
    all)
        # Sequentially run every recipe that the host can satisfy.
        # Skip platforms that need cross-compile toolchains we don't have.
        echo "Build all is platform-dependent; please run individual recipes." >&2
        exit 1
        ;;
    *)
        cat <<EOF
Usage: $0 <platform>

Platforms:
  windows-x86_64    -- run inside VS Developer shell
  linux-x86_64
  macos-x86_64      -- run on a Mac
  macos-arm64       -- run on an Apple-Silicon Mac
  android-arm64     -- requires NDK_ROOT
  ios-arm64         -- run on a Mac with Xcode
EOF
        exit 2
        ;;
esac

echo "[scc-unity] staged libscc into ${PLUGINS_DIR}"

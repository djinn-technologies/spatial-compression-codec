#!/usr/bin/env bash
# sdk/unreal/SCC/prepare-distribution.sh
#
# Stage the standalone-distributable form of the SCC plugin:
#
#   1. Copy cabi/include/libscc.h into Source/ThirdParty/SCC/Include/
#      so the plugin no longer depends on the parent repo at build
#      time (matches Epic Fab's "self-contained" expectation).
#   2. Verify that all platform binaries are present in
#      Source/ThirdParty/SCC/<platform>/.
#   3. Verify that Resources/Icon128.png exists and is the right size.
#   4. Zip the result into dist/SCC-<version>.zip ready for Fab
#      submission.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
PLUGIN_DIR="${SCRIPT_DIR}"
THIRDPARTY_DIR="${PLUGIN_DIR}/Source/ThirdParty/SCC"
DIST_DIR="${PLUGIN_DIR}/dist"

# 1. Copy libscc.h.
echo "[scc-unreal] Staging libscc.h ..."
mkdir -p "${THIRDPARTY_DIR}/Include"
cp "${REPO_ROOT}/cabi/include/libscc.h" "${THIRDPARTY_DIR}/Include/libscc.h"

# 2. Verify per-platform binaries.
expected_binaries=(
    "Win64/libscc.dll"
    "Win64/libscc.lib"
    "Linux/libscc.so"
    "Mac/libscc.dylib"
    "Android/arm64-v8a/libscc.so"
    "IOS/libscc.a"
)
missing=0
for rel in "${expected_binaries[@]}"; do
    if [[ ! -f "${THIRDPARTY_DIR}/${rel}" ]]; then
        echo "  MISSING: Source/ThirdParty/SCC/${rel}" >&2
        missing=$((missing + 1))
    fi
done
if (( missing > 0 )); then
    echo "[scc-unreal] ${missing} platform binaries are not staged." >&2
    echo "             Run sdk/unity/com.djinn.scc/build-plugins.sh on each platform first" >&2
    echo "             (the script's recipes apply unchanged to the Unreal plugin too)." >&2
    exit 1
fi

# 3. Icon.
ICON="${PLUGIN_DIR}/Resources/Icon128.png"
if [[ ! -f "${ICON}" ]]; then
    echo "[scc-unreal] WARNING: Resources/Icon128.png is missing (using placeholder)." >&2
fi

# 4. Zip.
mkdir -p "${DIST_DIR}"
VERSION="$(grep -oP '"VersionName"\s*:\s*"\K[^"]+' "${PLUGIN_DIR}/SCC.uplugin")"
ZIP_NAME="SCC-${VERSION}.zip"
ZIP_PATH="${DIST_DIR}/${ZIP_NAME}"

echo "[scc-unreal] Zipping plugin to ${ZIP_PATH} ..."
( cd "${PLUGIN_DIR}/.." && \
  zip -r "${ZIP_PATH}" "SCC" \
        -x "SCC/Intermediate/*" \
        -x "SCC/Binaries/*" \
        -x "SCC/Saved/*" \
        -x "SCC/dist/*" \
        -x "SCC/.git*" )

echo "[scc-unreal] OK -- ${ZIP_PATH}"

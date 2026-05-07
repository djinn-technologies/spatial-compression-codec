#!/usr/bin/env bash
# sdk/wasm/emscripten/build.sh
#
# Compile the SCC C++ codec + C ABI shim to a single WASM module via
# Emscripten. Requires `emcc` on PATH.
#
# Output:
#   dist/scc.js   — Modularised JS loader (createSCC factory)
#   dist/scc.wasm — Binary WebAssembly module
#
# Bundle-size budget: 800 KiB gzip combined. Run check-size.sh after
# build to enforce the budget. [Ultrathink #3]

set -euo pipefail

if ! command -v emcc >/dev/null 2>&1; then
    echo "ERROR: emcc not on PATH. Install Emscripten:" >&2
    echo "  https://emscripten.org/docs/getting_started/downloads.html" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${PKG_DIR}/../.." && pwd)"

CABI_SRC="${REPO_ROOT}/cabi/src"
CODEC_SRC="${REPO_ROOT}/codec/src"
CABI_INC="${REPO_ROOT}/cabi/include"
CODEC_INC="${REPO_ROOT}/codec/include"

DIST_DIR="${PKG_DIR}/dist"
mkdir -p "${DIST_DIR}"

# Sources: C ABI shim + every C++ stage. SIMD intrinsics in
# disparity_avx2.cpp are guarded by SCC_HAS_AVX2_BUILD which we DO NOT
# set for WASM -- the fallback scalar path runs (with WASM SIMD via
# autovectorisation under -msimd128). NEON likewise excluded.
SOURCES=(
    "${CABI_SRC}/libscc.cpp"
    "${CODEC_SRC}/common/rans.cpp"
    "${CODEC_SRC}/common/disparity.cpp"
    "${CODEC_SRC}/common/frequency.cpp"
    "${CODEC_SRC}/common/quadtree.cpp"
    "${CODEC_SRC}/common/sei/sei.cpp"
)

# Public function exports. Every function here must appear in
# cabi/abi-baseline.txt. _malloc / _free are runtime-required for
# heap marshalling from JS.
EXPORTED_FUNCTIONS='_scc_init,_scc_destroy,_scc_encode_frame,_scc_decode_sei,_scc_set_param,_scc_get_param,_scc_load_profile,_scc_save_profile,_scc_version,_scc_get_last_error,_malloc,_free'

# Runtime methods on the JS side. ccall / cwrap let the TS facade call
# arbitrary functions if needed; HEAPU8/HEAPU16/HEAP32 are the heap
# views used by the marshaller.
EXPORTED_RUNTIME_METHODS='ccall,cwrap,HEAPU8,HEAPU16,HEAP32'

CXXFLAGS=(
    -std=c++17
    -O3
    -flto
    -msimd128                # WebAssembly SIMD (Chrome 91+, Node 16.4+).
    -fexceptions             # codec uses std::invalid_argument throws.
    "-I${CABI_INC}"
    "-I${CODEC_INC}"
    -DLIBSCC_BUILD=1
)

LDFLAGS=(
    -O3
    -flto
    -fexceptions
    -sWASM=1
    "-sEXPORTED_FUNCTIONS=${EXPORTED_FUNCTIONS}"
    "-sEXPORTED_RUNTIME_METHODS=${EXPORTED_RUNTIME_METHODS}"
    -sMODULARIZE=1
    -sEXPORT_NAME=createSCC
    -sEXPORT_ES6=1
    -sUSE_ES6_IMPORT_META=1
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=64MB
    -sMAXIMUM_MEMORY=2147483648
    -sFILESYSTEM=0
    -sASSERTIONS=0
    -sENVIRONMENT=web,node,worker
    -sNO_EXIT_RUNTIME=1
    -sSTRICT=1
    -sMINIFY_HTML=0
)

echo "[scc-wasm] Compiling ${#SOURCES[@]} sources -> ${DIST_DIR}/scc.js"
emcc \
    "${CXXFLAGS[@]}" \
    "${SOURCES[@]}" \
    "${LDFLAGS[@]}" \
    -o "${DIST_DIR}/scc.js"

echo "[scc-wasm] Build complete:"
ls -la "${DIST_DIR}/scc.js" "${DIST_DIR}/scc.wasm"

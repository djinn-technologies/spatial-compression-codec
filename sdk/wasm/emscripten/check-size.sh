#!/usr/bin/env bash
# sdk/wasm/emscripten/check-size.sh
#
# Bundle-size budget gate. Asserts that gzip(scc.js) + gzip(scc.wasm)
# fits under 800 KiB. CI fails on regression.
# [Ultrathink #3, REQ-027 acceptance]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
DIST_DIR="${PKG_DIR}/dist"

if [[ ! -f "${DIST_DIR}/scc.js" || ! -f "${DIST_DIR}/scc.wasm" ]]; then
    echo "ERROR: dist/scc.js or dist/scc.wasm missing. Run build:wasm first." >&2
    exit 1
fi

BUDGET_KIB="${SCC_WASM_BUDGET_KIB:-800}"
BUDGET_BYTES=$((BUDGET_KIB * 1024))

js_gz=$(gzip -9 -c "${DIST_DIR}/scc.js"   | wc -c)
wasm_gz=$(gzip -9 -c "${DIST_DIR}/scc.wasm" | wc -c)
total=$((js_gz + wasm_gz))

human() {
    awk -v n="$1" 'BEGIN { printf "%.1f KiB", n/1024 }'
}

echo "scc.js   gzip: $(human $js_gz)"
echo "scc.wasm gzip: $(human $wasm_gz)"
echo "total    gzip: $(human $total)"
echo "budget       : $(human $BUDGET_BYTES) (override via SCC_WASM_BUDGET_KIB)"

if (( total > BUDGET_BYTES )); then
    echo "FAIL: bundle gzip $(human $total) exceeds budget $(human $BUDGET_BYTES)" >&2
    exit 1
fi

echo "OK: bundle within budget"

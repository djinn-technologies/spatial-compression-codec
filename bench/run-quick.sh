#!/usr/bin/env bash
#
# bench/run-quick.sh - the 5-minute developer-loop bench.
#
# Builds scc-bench (if not already built) and runs the tiny synthetic
# corpus across all five codecs at one resolution / one bit-depth.
# Prints the HTML report path on success.
#
# Acceptance budget: under 5 minutes on a Ryzen 5600X-class CPU (per
# AI Build Prompt #14). On lighter hardware the inner loops just take
# longer; the bench has no internal hard timeout beyond the per-test
# 300s ctest cap.
#
# Usage:
#   bench/run-quick.sh [build_dir]
#
# If build_dir is omitted, the script uses ./build.

set -euo pipefail

BUILD_DIR="${1:-build}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Configure with the bench harness target enabled. Subsequent runs are
# no-ops thanks to CMake's incremental configure.
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
    -DSCC_ENABLE_BENCH_HARNESS=ON \
    -DCMAKE_BUILD_TYPE=Release

cmake --build "$BUILD_DIR" --target scc_bench --config Release -j

OUT_DIR="$REPO_ROOT/bench/reports/quick"
mkdir -p "$OUT_DIR"

"$BUILD_DIR/bench/scc-bench" \
    --corpus "$REPO_ROOT/bench/fixtures" \
    --codecs scc,jpeg2000,png,tiff,zlib \
    --profiles lossless,lossy:high \
    --resolutions 320x240 \
    --bit-depths 12 \
    --runs 5 \
    --seed 0xC0FFEE \
    --out "$OUT_DIR" \
    --format csv,json,html

echo
echo "Quick bench complete. Open the HTML report:"
echo "  file://$OUT_DIR/report.html"

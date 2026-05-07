#!/usr/bin/env bash
# studio/agent/build-binaries.sh
#
# Build the agent binary for one platform target and stage it into the
# npm package's binaries/<platform>-<arch>/ directory.
#
# Run from a CI runner with the matching cross-compile toolchain.
# [Ultrathink #5 -- code signing happens in CI after this script.]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NPM_BINARIES_DIR="${SCRIPT_DIR}/npm/binaries"

usage() {
    cat <<EOF
Usage: $0 <triple>

Triples:
  x86_64-unknown-linux-musl     -> binaries/linux-x64/
  aarch64-unknown-linux-musl    -> binaries/linux-arm64/
  x86_64-apple-darwin           -> binaries/darwin-x64/
  aarch64-apple-darwin          -> binaries/darwin-arm64/
  x86_64-pc-windows-msvc        -> binaries/win32-x64/
EOF
    exit 2
}

if [[ $# -ne 1 ]]; then usage; fi
TRIPLE="$1"

case "${TRIPLE}" in
    x86_64-unknown-linux-musl)  out_dir="linux-x64";    exe="scc-studio-agent" ;;
    aarch64-unknown-linux-musl) out_dir="linux-arm64";  exe="scc-studio-agent" ;;
    x86_64-apple-darwin)        out_dir="darwin-x64";   exe="scc-studio-agent" ;;
    aarch64-apple-darwin)       out_dir="darwin-arm64"; exe="scc-studio-agent" ;;
    x86_64-pc-windows-msvc)     out_dir="win32-x64";    exe="scc-studio-agent.exe" ;;
    *) usage ;;
esac

echo "[scc-agent] building for ${TRIPLE} ..."
( cd "${SCRIPT_DIR}" && \
  cargo build --release --target "${TRIPLE}" \
        --features "" )

mkdir -p "${NPM_BINARIES_DIR}/${out_dir}"
cp "${SCRIPT_DIR}/target/${TRIPLE}/release/${exe}" \
   "${NPM_BINARIES_DIR}/${out_dir}/${exe}"

# Strip on Unix-likes (Cargo's strip = "symbols" should already cover
# this, but we double-tap for size).
case "${TRIPLE}" in
    *-linux-*) strip "${NPM_BINARIES_DIR}/${out_dir}/${exe}" || true ;;
    *-apple-darwin) strip -x "${NPM_BINARIES_DIR}/${out_dir}/${exe}" || true ;;
esac

echo "[scc-agent] staged ${NPM_BINARIES_DIR}/${out_dir}/${exe}"
echo "[scc-agent] next: code-sign and notarise (CI workflow)."

#!/usr/bin/env bash
# Shared configure+build — used by CI (build-linux job) and the local
# pre-push hook (.githooks/pre-push), so a build that passes locally passes
# in CI. Edit compiler flags / CMake args here, never in the workflow.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO_ROOT"

if [ ! -f vendor/httplib.h ]; then
    echo "error: vendor/ deps missing — run scripts/fetch-vendor-deps.sh first" >&2
    exit 1
fi

CMAKE_ARGS=(-B build -DCMAKE_BUILD_TYPE=Release)

# Transparent speedup, local or CI: if ccache is on PATH, use it. CI's runner
# image has it baked in; locally it's opt-in (apt/brew install ccache).
if command -v ccache >/dev/null 2>&1; then
    CMAKE_ARGS+=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi

cmake "${CMAKE_ARGS[@]}"
cmake --build build --config Release -j"$(nproc)"

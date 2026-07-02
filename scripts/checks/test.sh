#!/usr/bin/env bash
# Shared test runner — used by CI (build-linux job) and the local pre-push
# hook (.githooks/pre-push). Requires scripts/checks/build.sh to have run
# first (build/ must exist).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO_ROOT"

if [ ! -d build ]; then
    echo "error: build/ missing — run scripts/checks/build.sh first" >&2
    exit 1
fi

ctest --test-dir build -C Release --output-on-failure

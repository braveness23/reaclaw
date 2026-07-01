#!/usr/bin/env bash
# Shared clang-format check — the single source of truth for "is this repo
# formatted correctly". Called by both CI (.github/workflows/ci.yml
# format-check job) and the local pre-commit hook (.githooks/pre-commit).
# Edit the clang-format invocation here, never in the workflow or the hook.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO_ROOT"

MODE="${1:---check}"

# CI's runner image pins clang-format-18. Local machines may only have the
# unversioned binary — fall back to it but warn, since a version mismatch
# can disagree with CI on edge cases.
if command -v clang-format-18 >/dev/null 2>&1; then
    CLANG_FORMAT=clang-format-18
elif command -v clang-format >/dev/null 2>&1; then
    CLANG_FORMAT=clang-format
    echo "warning: clang-format-18 not found, using $(clang-format --version). CI pins v18." >&2
else
    echo "error: no clang-format binary found (need clang-format-18)" >&2
    exit 1
fi

mapfile -t FILES < <(find src -name "*.cpp" -o -name "*.h")

case "$MODE" in
    --check)
        "$CLANG_FORMAT" --dry-run --Werror "${FILES[@]}"
        ;;
    --fix)
        "$CLANG_FORMAT" -i "${FILES[@]}"
        ;;
    *)
        echo "usage: $0 [--check|--fix]" >&2
        exit 1
        ;;
esac

#!/usr/bin/env bash
# One-time local setup: point git at the repo's shared hooks.
# pre-commit: clang-format check (fast, every commit).
# pre-push:   full configure+build+ctest (slower, before code leaves your machine).
# Both call the same scripts CI uses (scripts/checks/*.sh), so passing locally
# means passing in CI. Hooks are a local convenience, not the enforcement
# boundary — CI is still the merge gate and can't be skipped with --no-verify.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
chmod +x "$REPO_ROOT"/.githooks/*
git -C "$REPO_ROOT" config core.hooksPath .githooks

echo "Git hooks installed (core.hooksPath -> .githooks)."
echo "  pre-commit: clang-format check"
echo "  pre-push:   build + ctest"
echo "One-off bypass: git commit/push --no-verify"

#!/usr/bin/env bash
# Fetch all vendored dependencies into vendor/.
# This script is the single source of truth for these versions — CI caches
# its output keyed on this file's hash (see .github/workflows/ci.yml), so a
# version bump here is what invalidates that cache. Don't duplicate these
# pins anywhere else.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR="$REPO_ROOT/vendor"

SQLITE_ZIP_URL="https://www.sqlite.org/2024/sqlite-amalgamation-3470200.zip"
JSON_HPP_URL="https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp"
HTTPLIB_URL="https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.41.0/httplib.h"
REAPER_SDK_URL="https://github.com/justinfrankel/reaper-sdk.git"
# Pinned commit, not the floating default branch — vendor/README.md previously
# documented this as "current" (always-latest), but once CI started caching
# vendor/ keyed only on this script's hash, that "always fresh" intent broke
# silently: the cache just freezes reaper-sdk at whatever commit was present
# the first time this file's hash last changed. Pinning makes the cache key
# honestly represent what it produces. Bump this deliberately, like WDL_COMMIT.
REAPER_SDK_COMMIT="ec60fb4c38e1f575e29e28bd01fcf50dbf1c0bc7"
WDL_COMMIT="6aaf30c"

echo "==> Fetching cpp-httplib v0.41.0"
curl -fsSL "$HTTPLIB_URL" -o "$VENDOR/httplib.h"

echo "==> Fetching nlohmann/json v3.11.3"
curl -fsSL "$JSON_HPP_URL" -o "$VENDOR/json.hpp"

echo "==> Fetching SQLite 3.47.2"
curl -fsSL "$SQLITE_ZIP_URL" -o /tmp/sqlite.zip
unzip -j /tmp/sqlite.zip "*/sqlite3.c" "*/sqlite3.h" -d "$VENDOR/"
rm /tmp/sqlite.zip

echo "==> Fetching REAPER SDK @ $REAPER_SDK_COMMIT"
rm -rf "$VENDOR/reaper-sdk"
mkdir -p "$VENDOR/reaper-sdk"
git -C "$VENDOR/reaper-sdk" init --quiet
git -C "$VENDOR/reaper-sdk" remote add origin "$REAPER_SDK_URL"
git -C "$VENDOR/reaper-sdk" fetch --quiet --depth=1 origin "$REAPER_SDK_COMMIT"
git -C "$VENDOR/reaper-sdk" checkout --quiet FETCH_HEAD
cp "$VENDOR/reaper-sdk/sdk/"*.h "$VENDOR/reaper-sdk/"

echo "==> Fetching WDL/swell @ $WDL_COMMIT (Linux/macOS panel support)"
rm -rf /tmp/wdl-dl && mkdir -p /tmp/wdl-dl
curl -fsSL "https://github.com/justinfrankel/WDL/archive/$WDL_COMMIT.tar.gz" | tar xz -C /tmp/wdl-dl
WDL_DIR=$(ls /tmp/wdl-dl | head -1)
mkdir -p "$VENDOR/WDL"
cp -r "/tmp/wdl-dl/$WDL_DIR/WDL/swell" "$VENDOR/WDL/"
rm -rf /tmp/wdl-dl

echo "==> Done. Vendor contents:"
ls "$VENDOR/"

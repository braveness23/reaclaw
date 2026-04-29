#!/usr/bin/env bash
# Fetch all vendored dependencies into vendor/.
# Versions here must stay in sync with the env vars in .github/workflows/ci.yml.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR="$REPO_ROOT/vendor"

SQLITE_ZIP_URL="https://www.sqlite.org/2024/sqlite-amalgamation-3470200.zip"
JSON_HPP_URL="https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp"
HTTPLIB_URL="https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.41.0/httplib.h"
REAPER_SDK_URL="https://github.com/justinfrankel/reaper-sdk.git"
WDL_COMMIT="6aaf30c"

echo "==> Fetching cpp-httplib v0.41.0"
curl -fsSL "$HTTPLIB_URL" -o "$VENDOR/httplib.h"

echo "==> Fetching nlohmann/json v3.11.3"
curl -fsSL "$JSON_HPP_URL" -o "$VENDOR/json.hpp"

echo "==> Fetching SQLite 3.47.2"
curl -fsSL "$SQLITE_ZIP_URL" -o /tmp/sqlite.zip
unzip -j /tmp/sqlite.zip "*/sqlite3.c" "*/sqlite3.h" -d "$VENDOR/"
rm /tmp/sqlite.zip

echo "==> Fetching REAPER SDK"
if [ -d "$VENDOR/reaper-sdk" ]; then
    git -C "$VENDOR/reaper-sdk" pull --quiet
else
    git clone --depth=1 "$REAPER_SDK_URL" "$VENDOR/reaper-sdk"
fi
cp "$VENDOR/reaper-sdk/sdk/"*.h "$VENDOR/reaper-sdk/"

echo "==> Fetching WDL/swell @ $WDL_COMMIT (Linux/macOS panel support)"
rm -rf /tmp/wdl-dl && mkdir -p /tmp/wdl-dl
curl -fsSL "https://github.com/justinfrankel/WDL/archive/$WDL_COMMIT.tar.gz" | tar xz -C /tmp/wdl-dl
mkdir -p "$VENDOR/WDL"
cp -r "/tmp/wdl-dl/WDL-$WDL_COMMIT/WDL/swell" "$VENDOR/WDL/"
rm -rf /tmp/wdl-dl

echo "==> Done. Vendor contents:"
ls "$VENDOR/"

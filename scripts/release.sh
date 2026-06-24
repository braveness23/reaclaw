#!/usr/bin/env bash
# release.sh — mechanical half of cutting a ReaClaw release.
#
# Bumps the version in the one place that matters (CMakeLists), rolls the
# CHANGELOG [Unreleased] section into a dated version section (the release
# workflow reads `## [x.y.z]` off the tag), updates the README "Latest release"
# line, runs the same local bar as a normal commit (clang-format + build +
# tests), and commits "release: vX.Y.Z" on the CURRENT branch.
#
# It deliberately does NOT push, open a PR, merge, or tag — those are driven by
# the /release skill so CI can be watched and the tag lands on the merge commit.
#
# Usage:
#   scripts/release.sh 1.6.0        # explicit version
#   scripts/release.sh --minor      # bump minor from current CMakeLists version
#   scripts/release.sh --patch      # bump patch
#   scripts/release.sh --major      # bump major
#   scripts/release.sh 1.6.0 --no-verify   # skip the build/test gate
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

VERIFY=1
ARG=""
for a in "$@"; do
  case "$a" in
    --no-verify) VERIFY=0 ;;
    *) ARG="$a" ;;
  esac
done
[ -n "$ARG" ] || { echo "usage: release.sh <X.Y.Z|--major|--minor|--patch> [--no-verify]" >&2; exit 2; }

CUR=$(grep -m1 -oE 'VERSION [0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt | awk '{print $2}')
IFS=. read -r MA MI PA <<<"$CUR"
case "$ARG" in
  --major) VER="$((MA+1)).0.0" ;;
  --minor) VER="${MA}.$((MI+1)).0" ;;
  --patch) VER="${MA}.${MI}.$((PA+1))" ;;
  [0-9]*.[0-9]*.[0-9]*) VER="$ARG" ;;
  *) echo "bad version arg: $ARG" >&2; exit 2 ;;
esac

# Refuse a dirty tree so the release commit is exactly the bump.
if [ -n "$(git status --porcelain)" ]; then
  echo "working tree not clean — commit or stash first so the release commit is just the bump" >&2
  git status --short >&2
  exit 1
fi

# Refuse if [Unreleased] has no content to release.
if ! awk '/^## \[Unreleased\]/{f=1;next} f&&/^## \[/{exit} f&&NF{print;exit}' CHANGELOG.md | grep -q .; then
  echo "CHANGELOG [Unreleased] is empty — nothing to release. Add entries first." >&2
  exit 1
fi

DATE=$(date +%F)
echo "Releasing $CUR -> $VER  ($DATE), on branch $(git branch --show-current)"

# 1) CMakeLists version (the source of truth; REACLAW_VERSION derives from it).
sed -i -E "s/(project\(reaper_reaclaw VERSION )[0-9]+\.[0-9]+\.[0-9]+/\1${VER}/" CMakeLists.txt

# 2) CHANGELOG: rename [Unreleased] -> [VER] - DATE and seed a fresh [Unreleased].
awk -v ver="$VER" -v date="$DATE" '
  !done && /^## \[Unreleased\]/ {
    print "## [Unreleased]"; print ""; print "## [" ver "] - " date; done=1; next
  } { print }
' CHANGELOG.md > CHANGELOG.md.tmp && mv CHANGELOG.md.tmp CHANGELOG.md

# 3) README "Latest release" line (cosmetic; phase table still eyeballed by hand).
sed -i -E "s/(Latest release: \*\*)v[0-9]+\.[0-9]+\.[0-9]+(\*\*)/\1v${VER}\2/" README.md || true

# 4) Same bar as any commit: format check (src only, as CI does), build, tests.
if [ "$VERIFY" = 1 ]; then
  FMT=$(command -v clang-format-18 || command -v clang-format || true)
  if [ -n "$FMT" ]; then
    echo "→ clang-format check (src/)"
    find src -name '*.cpp' -o -name '*.h' | xargs "$FMT" --dry-run --Werror
  fi
  echo "→ build"
  cmake --build build -j"$(nproc)" >/dev/null
  echo "→ tests"
  ( cd build && ctest --output-on-failure >/dev/null )
  echo "✓ verify passed"
fi

git add CMakeLists.txt CHANGELOG.md README.md
git commit -q -m "release: v${VER}

Bump version to ${VER} and roll CHANGELOG [Unreleased] -> [${VER}].
The release workflow extracts ## [${VER}] on the v${VER} tag.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"

echo
echo "✓ committed: release: v${VER}"
echo "Next (driven by /release): push branch → PR → CI green → squash-merge → tag v${VER} on main → release."

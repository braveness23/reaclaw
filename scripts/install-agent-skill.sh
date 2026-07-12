#!/usr/bin/env bash
# One-time local setup: expose the repo's `reaper` agent skill to Claude Code
# sessions running *outside* this repo by symlinking it into ~/.claude/skills.
# A symlink (not a copy) keeps git the single source of truth — LEARNED.md
# updates made from any session land in the repo, ready to commit.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$REPO_ROOT/.claude/skills/reaper"
DEST="$HOME/.claude/skills/reaper"

mkdir -p "$HOME/.claude/skills"
if [[ -e $DEST && ! -L $DEST ]]; then
    echo "install-agent-skill: $DEST exists and is not a symlink — refusing to clobber." >&2
    exit 1
fi
ln -sfn "$SRC" "$DEST"
echo "Agent skill installed: $DEST -> $SRC"

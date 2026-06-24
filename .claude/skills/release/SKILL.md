---
name: release
description: Cut a ReaClaw release end-to-end with minimal friction — version bump, PR, CI gate, merge, tag, and verify the published GitHub Release. Use when the user asks to "release", "cut a release", "ship it", "tag vX.Y.Z", or "do a release".
---

# ReaClaw release

Drive a release from "feature is done and verified" to "GitHub Release published
with the Linux binary attached" with as few stops for the user as possible. The
user has standing authorization for this whole flow — do not ask permission at
each step; just run it and report. Only stop if something genuinely needs a
human decision (a CI failure, a merge conflict, an ambiguous version).

## The one rule that prevents the usual mess

**The version bump must be in the PR before merge**, so the squash-merge carries
it and you tag the merge commit. Do **not** bump after opening the PR — that's
what forced a cherry-pick onto main last time. If work is already merged to main
without a bump, see "Recovery" below.

## Versioning

Each feature epic is a **minor** bump (v1.3 → v1.4 → v1.5 → v1.6, one epic each).
Bug-fix-only releases are **patch**. If the user names a version, use it; else
default to `--minor` and say so. `REACLAW_VERSION` derives from the CMakeLists
version, so CMakeLists is the only source of truth.

## Steps

1. **Pre-flight.** Confirm the work is committed and the CHANGELOG `[Unreleased]`
   section actually describes what's shipping. Tree should be clean. Know whether
   you're on a feature branch (normal path) or main (recovery path).

2. **Bump + verify (scripted).** On the feature branch:
   ```
   scripts/release.sh <X.Y.Z | --minor | --patch>
   ```
   It bumps CMakeLists, rolls CHANGELOG `[Unreleased]` → `[X.Y.Z] - <today>` (and
   seeds a fresh `[Unreleased]`), updates the README "Latest release" line, runs
   the clang-format/build/test bar, and commits `release: vX.Y.Z`. Then eyeball
   the README phase table for the version (the script leaves that to you).

3. **Push + PR.** Push the branch. If no PR exists, `gh pr create --base main`
   with a body summarizing the epic (lead with the headline feature). If the PR
   already exists, the push updates it.

4. **Watch CI.** `gh pr checks <n> --watch`. Expect `clang-format` and
   `Build (Linux, GCC)` to pass; `Publish GitHub Release` shows **skipping** on a
   PR (it only runs on a `v*` tag) — that's correct, not a failure. If a check
   fails, stop and fix; don't merge red.

5. **Merge.** Squash-merge: `gh pr merge <n> --squash`. (If the user prefers to
   click merge themselves, let them — then continue once it's merged.)

6. **Tag the merge commit.** `git checkout main && git pull --ff-only`, then:
   ```
   git tag -a vX.Y.Z -m "ReaClaw vX.Y.Z — <epic title>" && git push origin vX.Y.Z
   ```
   The tag push triggers the release workflow (build-linux → extract `## [X.Y.Z]`
   from CHANGELOG → publish Release with `reaper_reaclaw-linux-x86_64.so`).

7. **Watch the release run + verify.** `gh run watch <id> --exit-status`, then
   `gh release view vX.Y.Z --json assets` to confirm it's published with the
   `.so` attached. Report the release URL.

## Recovery — work already merged to main without the bump

This is the off-happy-path that needed a cherry-pick last time. If the PR is
already merged and main is at the old version: bump directly on main
(`scripts/release.sh X.Y.Z` from main), push main, then tag and continue from
step 6. A `release: vX.Y.Z` commit on main is acceptable (it's how v1.3.0 was
cut) — just don't make a habit of it; prefer bump-in-PR.

## Don't

- Don't bump the version in more than the script touches by hand (drift).
- Don't tag before the merge is on main (you'd tag the wrong tree).
- Don't treat the release job "skipping" on a PR as an error.
- Don't forget the docs reflex: per the standing rule, a feature release refreshes
  README + EXAMPLES, not just the CHANGELOG — the script + your eyeball cover the
  version strings, but make sure new endpoints are actually documented.

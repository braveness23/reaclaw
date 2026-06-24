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

## Versioning & cadence

ReaClaw is past 1.0, so the REST API is a **contract** agents and the MCP server
consume. `REACLAW_VERSION` derives from the CMakeLists version — that's the only
source of truth.

**Choosing the bump** — keep `CHANGELOG [Unreleased]` honest on every PR, then the
bump picks itself:

| Bump | When | Examples |
|------|------|----------|
| **patch** `1.6.x` | behavior fix, **no API surface change**, fully backward-compatible | wrong dB readback, a hint misfiring, a crash on an empty item, a perf fix, a fix that rebuilds the binary |
| **minor** `1.x.0` | new, **additive** surface — old calls still work | a new endpoint/verb/field/measure; one cohesive feature (can be smaller than a whole epic) |
| **major** `2.0.0` | **breaking** the contract | removing/renaming an endpoint, changing a response shape agents rely on, changing auth/bind/config defaults |

Rule of thumb: anything breaking in `[Unreleased]` → **major**; else any new feature
→ **minor**; else (only fixes) → **patch**. If the user names a version, use it.

**Cadence — release when there's something worth installing; don't batch.** A
release is a tag → ~6 min automated build+publish, so sitting on a fix costs more
than it saves.
- **Patches: cut eagerly** — a real fix to an already-*released* version lands on
  main → tag a patch that day. (A fix to a feature still sitting in `[Unreleased]`
  isn't a patch; it folds into that pending minor.)
- **Minors: per cohesive feature**, which may be *smaller than a full epic* — ship
  an independently-useful sub-feature rather than waiting to bundle a big one.
- **Majors: rare, deliberate, batched.**

**One bar that does not relax for patches:** if a patch changes behavior, it still
gets the live-REAPER verification. "Patch" means small scope, not "skip the check."

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

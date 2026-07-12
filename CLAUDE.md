# CLAUDE.md — ReaClaw Project

## Before Touching Any Code

**Read these files first. Every time. No exceptions.**

1. `ReaClaw_TECH_DECISIONS.md` — architectural decisions and rationale; many things that look like bugs are intentional choices
2. `ReaClaw_IMPLEMENTATION_CHECKLIST.md` — phase-by-phase task list; shows what's done, what's in scope, and what's deferred
3. `ReaClaw_Design.md` — full design spec including API contracts, data models, and security model
4. `SECURITY.md` — security model, scope, and deployment guidance
5. `ReaClaw_ROADMAP.md` — the consolidated forward plan (control + perception); maps the open epics to GitHub issues

Do not propose changes that contradict settled decisions in `TECH_DECISIONS.md`. If a decision seems wrong, raise it with the user rather than silently overriding it.

---

## Operating the Live Rig

For any request to *do something in REAPER right now* (mute, play, add FX,
render, …) — as opposed to changing ReaClaw's source — invoke the **`reaper`
skill** (`.claude/skills/reaper/`). It carries the operating manual
(`docs/AGENT_GUIDE.md`), an auth-wrapped `rc` helper, a sub-second `warmup.sh`,
and the background event tail. Do not rediscover ports, keys, or endpoint
shapes by hand.

---

## Update Docs As You Go

After completing each checklist item, mark it done in `ReaClaw_IMPLEMENTATION_CHECKLIST.md`. After adding or changing any API endpoint, update `docs/API.md`. After any config change, update the config reference in `ReaClaw_Design.md` §7. This is how the project survives crashes, context compaction, and resumed sessions.

---

## Code Comments: Constraints, Not History

A comment must state something the code can't — a live constraint, a non-obvious
"why", a warning about what breaks if changed. Never write comments that narrate
what the code replaced or how it used to work ("replaces the former X",
"previously used Y", "(future work)" for things that shipped) — that's what
commit messages, PR descriptions, and `CHANGELOG.md` are for, and in code it
goes stale silently. If history *is* the constraint ("don't re-add X, it breaks
because Y"), keep the constraint and drop the archaeology.

---

## Developer Workflow & CI

Local checks and CI call the *same* scripts, on purpose — a commit that passes locally deterministically passes CI:

- `scripts/checks/format.sh` — clang-format check/fix. Used by CI's `format-check` job and the `pre-commit` git hook.
- `scripts/checks/build.sh` + `test.sh` — configure+build / ctest. Used by CI's `build-linux` job and the `pre-push` git hook.
- `scripts/fetch-vendor-deps.sh` — pins and fetches vendor headers/SDKs (SQLite, json.hpp, httplib, reaper-sdk, WDL). Single source of truth for those versions; CI caches its output keyed on this script's hash.

**Rule: when changing a lint rule, compiler flag, test invocation, or vendor version, edit the shared script — never `.github/workflows/ci.yml` or a `.githooks/*` hook directly.** Both sides call the same file by construction; editing only one reintroduces drift (this happened before: vendor versions were duplicated in both `ci.yml` and `fetch-vendor-deps.sh`, "kept in sync" only by a comment).

Git hooks (`scripts/install-git-hooks.sh`, one-time local setup) are the fast/local signal, not the enforcement boundary — CI is the merge gate and can't be skipped with `--no-verify`. `pre-commit` is format-only (fast, every commit); `pre-push` runs the full build+test (slower, before code leaves the machine).

CI itself: self-hosted k3s runner (`arc-runner-reaclaw`, config in `deploy/runner/values.yaml`), image built by `ci.yml`'s own `runner-image` job (not a separate workflow — that was retired) from `.github/runner-image/Dockerfile`. That job runs first, `needs:`-gated ahead of every job that uses the self-hosted pool, and only rebuilds when the Dockerfile's content actually changed (content-hash tag, checked via `docker manifest inspect`) — this is what stops "the image the workflow assumes exists" from racing "the image that's actually been pushed." The image bakes in everything static across runs (build toolchain, ccache, e2e's Xvfb/audio/X11 deps) so jobs don't pay setup cost every run — only genuinely per-run state (vendor deps, ccache objects, the pinned REAPER binary) is fetched/restored per job. `ci.yml` also cancels superseded runs on the same ref (`concurrency` block) so a quick follow-up push doesn't waste runner capacity finishing a stale commit.

---

## Project Overview

ReaClaw is a native C++ REAPER extension (`.dll`/`.dylib`/`.so`) that embeds an HTTPS server inside REAPER's process and exposes a REST/JSON API for AI agents to control REAPER.

**Key settled decisions (do not re-litigate without reading TECH_DECISIONS.md first):**
- Bind address `0.0.0.0` is intentional — loopback-only is a user config choice, not a default
- No rate limiting — single-user tool; network isolation is the right defense layer
- Auth: `none` or `api_key` only — no mTLS, no OAuth
- Lua validation: syntax only, no static analysis, no approval gate — agent is trusted
- No LLM client in the extension

---

## Phase Status

All planned phases and all six roadmap epics are **complete** (see
`ReaClaw_ROADMAP.md` §2 and `ReaClaw_IMPLEMENTATION_CHECKLIST.md` for the full
record). Current release line: v1.16.0.

| Phase | Tag | Status |
|-------|-----|--------|
| Phase 0 — Foundation | v0.1.0 | Complete |
| Phase 1 — Scripts & Sequences | v0.2.0 | Complete |
| Phase 2 — Integration & Hardening | v1.0.0 | Complete |
| Phase 3 — Extensions menu | v1.2.0 | Complete |
| Phase 4 — Perception, ergonomics & learning (Epics #16–#20) | v1.3.0–v1.6.0 | Complete |
| Epic #32 — Headless offline render engine | v1.8.0–v1.15.0 | Complete |
| Epic #45 — Full coverage (transport/MIDI/take-FX/chunk/lifecycle) | v1.8.0–v1.10.0 | Complete |

Forward work is tracked as GitHub issues (see `ReaClaw_IDEAS.md` for the backlog).

---

## Source Layout

```
src/
├── app.h                    # Global singletons (g_config, g_db, g_start_time)
├── main.cpp                 # Defines globals; ReaperPluginEntry
├── auth/auth.{h,cpp}        # Auth check + reject helpers
├── config/config.{h,cpp}    # Config struct, load(), save()
├── db/db.{h,cpp}            # SQLite wrapper
├── handlers/                # HTTP route handlers
│   ├── common.h             # json_ok, json_error, now_iso, agent_id, vol_to_db
│   ├── analysis.{h,cpp}     # GET /analysis/item/{i}, /analysis/file, /state/meters
│   ├── capabilities.{h,cpp} # GET /capabilities (coverage matrix, sdk stats, features)
│   ├── catalog.{h,cpp}      # GET /catalog, /catalog/search, /catalog/categories, /catalog/{id}
│   ├── chunk.{h,cpp}        # GET/POST /state/chunk (universal RPP backstop)
│   ├── events.{h,cpp}       # GET /events, /events/stream (SSE)
│   ├── execute.{h,cpp}      # POST /execute/action, /execute/sequence, /execute/script
│   ├── fx.{h,cpp}           # Track/take-FX verbs: add/get/set/delete/copy, presets
│   ├── handler_util.h       # Shared handler plumbing (path/body parsing, with_undo, etc.)
│   ├── hints.{h,cpp}        # Consequence-aware hints[] on mutating responses
│   ├── history.{h,cpp}      # GET /history
│   ├── items.{h,cpp}        # GET/POST/DELETE /state/items[/{i}], split
│   ├── learning.{h,cpp}     # GET /suggestions, /learn/stats (opt-in mining)
│   ├── midi.{h,cpp}         # GET/POST /state/items/{i}/midi
│   ├── probe.{h,cpp}        # GET /analysis/*/probe (pitch/key/tempo)
│   ├── project.{h,cpp}      # /project*, /undo, /redo, /state/markers, /state/tempo, /time
│   ├── recipes.{h,cpp}      # GET /recipes[/{id}]
│   ├── render.{h,cpp}       # POST /render, /render/jobs (async job model)
│   ├── restart.{h,cpp}      # POST /reaper/restart (Linux)
│   ├── screenshot.{h,cpp}   # GET /screenshot (named surfaces, X11)
│   ├── scripts.{h,cpp}      # POST /scripts/register, GET/DELETE /scripts/{id}, GET /scripts/cache
│   ├── snapshot.{h,cpp}     # POST/GET/DELETE /snapshot, /snapshot/diff[/visualize]
│   ├── state.{h,cpp}        # GET /state*, track/send verbs, selection (FX verbs live in fx.{h,cpp})
│   ├── transport.{h,cpp}    # GET/POST /transport[/*]
│   └── visualize.{h,cpp}    # GET /analysis/*/visualize (PNG + digest)
├── panel/                   # Extensions › ReaClaw menu + SWELL dialogs
├── reaper/                  # REAPER SDK integration
│   ├── api.{h,cpp}          # init(), shutdown(), timer_callback(); REAPERAPI_IMPLEMENT
│   ├── catalog.{h,cpp}      # Action catalog indexer (bundled table + live enumeration)
│   ├── csurf.{h,cpp}        # IReaperControlSurface event feed (issue #31)
│   ├── executor.{h,cpp}     # Command queue + main-thread dispatch + EditingGuard
│   ├── native_actions.gen.h # Generated native action ID→name table
│   └── scripts.{h,cpp}      # register_script(), unregister_script()
├── server/
│   ├── router.{h,cpp}       # Route registration + auth_wrap
│   └── server.{h,cpp}       # SSLServer lifecycle
└── util/
    ├── dsp.h                # Header-only FFT (analysis/visualization/probes)
    ├── image.{h,cpp}        # Dependency-free PNG encoder + RGB canvas
    ├── jsondiff.h           # Recursive JSON differ (snapshot diff)
    ├── logging.{h,cpp}      # Log::info/warn/error, level filter
    ├── midi_util.h          # MIDI helpers (note names, PPQ)
    ├── music.h              # Pitch/key math (probes)
    └── tls.{h,cpp}          # Self-signed cert generation
```

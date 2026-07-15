# Changelog

All notable changes to ReaClaw are documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

## [1.18.1] - 2026-07-14

### Fixed
- **Windows: extension corrupted REAPER's mouse/keyboard handling** (#111) —
  plain clicks executed their Ctrl-variants ("mute/solo exclusively"), wheel
  zoom died, FX windows wouldn't open. Root cause: `csurf_inst` handed a
  MinGW-ABI C++ vtable to MSVC-built REAPER; the host's virtual calls
  (including `IsKeyDown`) landed one slot off. The control surface is no
  longer registered on Windows, so `GET /events` reads empty there (poll
  `GET /state/changes` instead) until an MSVC-layout vtable shim restores the
  feed.

## [1.18.0] - 2026-07-12

### Added
- **Agent onboarding manual served by the API** — new `docs/AGENT_GUIDE.md`
  (vendor-neutral operating manual for AI agents: connection discovery, the
  latency contract, the external-change sync protocol, a verified cheat sheet
  of the common verbs, and the trap list), embedded into the binary at build
  time (`cmake/embed_file.cmake`) and served verbatim at **`GET /agent/guide`**
  (`?format=json` wraps it with the server version and entry-point links).
  Any AI harness can now bootstrap itself from the running server with one
  call — and the served copy can never drift from the committed guide.
  `GET /` gained an `agent_guide` pointer.
- **Committed Claude Code skill** at `.claude/skills/reaper/` — instant-ready
  live-REAPER operation for agent sessions in this repo: `rc` (auth/TLS/
  discovery-wrapped curl), `warmup.sh` (sub-second project digest + change
  cursors), `events-tail.sh` (background SSE mirror of `/events/stream` so
  "what did the user change?" is a local file read), and a `LEARNED.md`
  capture loop. `scripts/install-agent-skill.sh` symlinks it into
  `~/.claude/skills` for sessions outside the repo.

## [1.17.0] - 2026-07-08

### Added
- **Stable FX addressing via GUID** (#102) — every FX read/write response
  (`GET`/`POST`/`DELETE .../fx/{slot}`, `.../copy`, `.../preset`, the new
  `.../pins`) now carries a `guid` field (`TrackFX_GetFXGUID`/
  `TakeFX_GetFXGUID`), and `{slot}` in every one of those routes accepts
  either the numeric chain index (unchanged) or that GUID string — so an
  agent that read a plugin's identity once can keep addressing it correctly
  even after other FX are inserted/deleted/reordered in the chain. Backed by
  a shared `resolve_fx_slot()` helper; router regexes for FX slot segments
  changed from `(\d+)` to `([^/]+)` to accept both forms.
- **FX channel routing / pin-mapping visibility** (#101) —
  `GET`/`POST /state/tracks/{index}/fx/{slot}/pins` and the take-FX
  equivalent expose a plugin's I/O pin count (`TrackFX_GetIOSize`) and how
  each pin maps onto track/take channels (`TrackFX_GetPinMappings`/
  `SetPinMappings`, decoded from the 64-bit channel bitmask into a
  `channels[]` list per pin). Distinct control surface from the existing
  `sends` verbs — this is routing *within* a plugin's own pins.
- **FX parameter modulation / MIDI-CC-link visibility** (#100) — every
  param in an FX read/write response now carries a `modulation` object
  (`lfo_active`, `acs_active`, `plink_active`, and — only when a link is
  active — a `plink` detail object) via `TrackFX_GetNamedConfigParm`'s
  `param.X.{lfo,acs,plink}.*` keys, so an agent can tell whether a knob is
  already wired to a hardware MIDI CC (or an LFO, or audio-rate modulation)
  before setting it via the API. Write side: a per-param `plink` object in
  the existing `params` write array sets or clears the binding via
  `TrackFX_SetNamedConfigParm`.

### Fixed
- **`GET /state` time signature denominator** — was hardcoded to `/4`
  (a 6/8 project reported `"6/4"`); now read from the tempo map via
  `TimeMap_GetTimeSigAtTime`, matching what `GET /state/tempo` reports.
- **`GET /capabilities` internal drift** (#103) — `via_script_or_action` still
  listed "MIDI notes/events" even though MIDI graduated to structured verbs in
  v1.8.0 (#51), contradicting the `coverage.midi: structured` entry in the same
  response. Also added the missing `GET /transport`, `POST /transport/play|stop|
  pause|record`, `POST /execute/script`, and render-jobs entries to `direct`.
- **First-run `config.json` omitted the `learning` block** — `default_config()`
  now writes it (matching `save()`, `config.example.json`, and the documented
  reference in `ReaClaw_Design.md` §7).

### Documentation (issue #103 — code-vs-docs drift audit)
- `docs/API.md`: documented the take-FX verbs (#50, shipped v1.10.0 but never
  added here), the unauthenticated `GET /` landing page, and the `409`/`503`
  status codes; refreshed the stale `/capabilities` example (`sdk` numbers,
  `coverage.midi`).
- `docs/MCP.md`: rewrote the stdio-server section to match the real
  `reaclaw_mcp` package — invocation (`python -m reaclaw_mcp.server`), env vars
  (`REACLAW_URL`, not `REACLAW_BASE_URL`), and the actual `reaper_*` tool
  names (the old table listed 18 `reaclaw_*` tools that never existed).
- `docs/DEPLOYMENT.md`: first-run config block gained the `learning` and
  `semantic_search` sections.
- `ReaClaw_Design.md`: §4 marked as the historical Phase-0/1 spec (docs/API.md
  is authoritative); §8's "Windows must use MSVC" corrected per the revised
  ABI note in `ReaClaw_TECH_DECISIONS.md` §2.
- `SECURITY.md`: documented the deliberate `GET /` no-auth exception.
- `ReaClaw_IMPLEMENTATION_CHECKLIST.md`: Epic #32 heading corrected to
  complete; `/state/items` cache claim corrected (uncached since item CRUD).
- `ReaClaw_ROADMAP.md`: Epic 6 version span corrected (v1.8.0–v1.15.0).
- `ReaClaw_TECH_DECISIONS.md`: MCP tool count 17 → 18.
- `README.md`: fixed the `GET /state` jq example (`.project.bpm`,
  `.track_count`) and the phase-table tags.
- `CLAUDE.md`: removed contradictory stale Phase-2 task sections, refreshed the
  phase-status table and the source-layout tree to the current handler set.

### Removed
- Dead code found during the #103 sweep: unused `not_implemented()` helper in
  `handlers/common.h`, stale "not implemented in Phase 0" comment in
  `handlers/scripts.h`, and `tests/placeholder_test.cpp` (never referenced by
  the test build).

## [1.16.0] - 2026-07-01

### Added
- **External-change event feed** (#31) — `GET /events?since=&limit=` (poll) and
  `GET /events/stream` (Server-Sent Events) push granular, attributed change
  events via a new `IReaperControlSurface` hook: track list/volume/pan/mute/
  selected/solo/recarm/title and play/repeat state, from any source — GUI,
  control surface, another API client, or ReaClaw's own edits. Each event
  carries `source: "reaclaw"|"external"` via a new `Executor::EditingGuard`
  RAII attribution mechanism covering both direct mutations and action
  dispatch (sync and async). Verified live including SSE over the existing
  TLS server. Complements the existing `GET /state/changes` poll token and
  `GET /snapshot/diff`. See `ReaClaw_TECH_DECISIONS.md` §26 for the honest
  account of attribution's confirmed-live limits (best-effort, not airtight)
  and the deliberate v1 scope boundary (FX/marker changes not yet covered).
- **Server-side semantic catalog search + `/recipes`** (#10) —
  `GET /catalog/search?semantic=true` ranks by embedding similarity via a
  local Ollama instance instead of keywords. Opt-in twice over
  (`config.semantic_search.enabled` AND the request's `semantic=true`),
  loopback-only (a non-loopback `ollama_url` is rejected instantly, no
  network attempt), and silently falls back to keyword search on any
  failure. Catalog results also gain `mutates_state` and
  `requires_selection` heuristic flags (alongside the existing
  `interactive`). New `GET /recipes[/{id}]` exposes the Skill's vetted
  snippets as structured JSON for callers without the Skill loaded. See
  `ReaClaw_TECH_DECISIONS.md` §25 for why this is a narrow, deliberate
  exception to the "no network egress" stance (§11) — an embedding model,
  not a generative one, and the same call the MCP client already makes.
- **Windows release binary** (#84) — `reaper_reaclaw-windows-x86_64.dll`,
  cross-compiled via MinGW-w64 on the same self-hosted CI runner, published
  alongside the Linux `.so` on every tagged release. Re-added from git
  history (the job existed pre-Linux-only-CI) and fully verified with a local
  mingw cross-compile before landing — the source itself needed no changes,
  only the CI job and vendor-dep fetch (WDL/swell, which the source now
  includes directly). Not yet load-tested on a physical Windows REAPER
  install — flagged in the docs.

## [1.15.0] - 2026-07-01

### Added
- **`GET /snapshot/diff/visualize`** (#53) — the A/B visual diff. Paired
  waveform/spectrum/loudness visualizations of a snapshot's audio target vs.
  current (or another snapshot), plus a digest delta. `POST /snapshot` gains
  an optional `audio: {item|file}` field to freeze an audio reference at
  capture time. Reuses the same decode+FFT pipeline as
  `GET /analysis/file/visualize` and the same `jsondiff` as `/snapshot/diff`.
- **CI: headless offline-render E2E smoke test** (#36) — installs a throwaway,
  unlicensed REAPER under Xvfb + a dummy JACK backend, loads the just-built
  extension, builds a tiny composition via the API, renders it, and asserts
  the output is non-silent via `GET /analysis/file` (no ffmpeg dependency).
  Runs on every PR; closes out Epic #32 (programmatic audio production).

## [1.14.0] - 2026-07-01

### Added
- **`async: true` on `POST /render`** — returns `{job_id, status: "queued"}`
  immediately instead of blocking the HTTP connection for the render's
  duration. `GET /render/jobs/{id}` polls status/result, `GET /render/jobs`
  lists tracked jobs, `DELETE /render/jobs/{id}` cancels a not-yet-started
  job (409 if already running — no safe SDK abort exists). Confirmed live
  that this does not make other endpoints stay responsive during an active
  render (REAPER's main thread pumps no message loop during an offline
  render) — documented as a known v1 limitation rather than silently
  overstating what async fixes (#35).

## [1.13.0] - 2026-07-01

### Added
- **`POST /reaper/restart`** — full main-thread-wedge recovery: kills and
  relaunches the REAPER process ReaClaw is embedded in, replaying its own
  current argv/environment (`/proc/self/cmdline`/`environ`) byte-for-byte so
  DISPLAY/XAUTHORITY are exactly what's already working. Best-effort
  in-place project save (default on, short timeout) before restarting.
  Linux only. Goes beyond `POST /queue/flush` (#64), which only drains the
  pending backlog and can't unstick a call already stuck mid-execute (#77).

### Fixed
- **In-place project save silently no-op'd.** `POST /project/save` (no
  `path`) and `POST /reaper/restart`'s pre-restart save both relied on
  `GetSetProjectInfo_String(nullptr, "PROJECT_FILENAME", ...)` to detect the
  current filename — not a real REAPER parmname, so it always read back
  empty — and on `Main_SaveProjectEx(nullptr, nullptr, 0)` to save in place,
  which also silently no-ops instead of saving to the current filename.
  Found live while testing #77; both now use `EnumProjects(-1, ...)` and
  pass the filename explicitly.

## [1.12.0] - 2026-06-30

### Added
- **`GET /transport`** — live transport position, bypassing the 1s `/state`
  cache so playhead polling during playback isn't up to a second stale (#67).
- **`POST /transport/play|stop|pause|record`** — agent-friendly aliases for
  `POST /transport {"action": ...}` so a guessed sub-resource route no longer
  404s (#71).
- **`POST /execute/script`** — one-shot Lua execution: register + run + (by
  default) deregister in a single call, for throw-away scripts that don't
  need a place in the script library (#69).
- **`POST /queue/flush`** — drains the pending command backlog so callers
  blocked behind a stuck main thread return immediately instead of waiting
  out their timeout (#64).
- **`instrument` field on `POST /state/tracks` create specs** — add a
  VSTi/CLAP by name in the same call that creates the track, instead of a
  separate follow-up `POST .../fx` (#79). (Bulk creation was already covered
  by the existing `create: [...]` array — no separate `/tracks/bulk` route.)
- **`is_inline_eq` / `agent_slot` on track `fx[]` entries** — flags REAPER's
  auto-added disabled inline ReaEQ (always at raw slot 0 on new tracks) so an
  agent counting FX by `agent_slot` never hits the off-by-one (#66).
- **Pagination/search on `GET /state/tracks/{n}/fx/{slot}`** — `?limit=&offset=&q=`
  for plugins with hundreds/thousands of params (e.g. Surge XT: 2147) (#74).
- **Pagination on `GET /state/items/{n}/midi`** — `?limit=&offset=` (default
  200/0) for items with large note counts; `note_count`/`cc_count` always
  report true totals (#75).
- **`context.schema` on the most commonly-guessed-wrong 400 responses** —
  `/render`, `/execute/action`, `/execute/script`, `/scripts/register`,
  `/project/open`, `/project/save` now list every accepted field on the
  first missing-field error (#72).
- **`docs/SCRIPTING.md`** — Lua call-signature gotchas, the `SetExtState`
  return pattern, the `SetTempoTimeSigMarker` idiom, headless-safety rules,
  and a worked MIDI-build example (#73).

### Changed
- **`POST /state/tempo`** — `time` is now optional (defaults to `0.0`), and
  `time_signature` accepts a `"4/4"` string as sugar for
  `timesig_num`/`timesig_denom` — setting the project's starting tempo is now
  a one-field call: `{"bpm": 95}` (#70).

## [1.11.2] - 2026-06-30

### Fixed
- **`ShowConsoleMsg` now rejected at script registration** — scripts containing
  `ShowConsoleMsg` were silently passed to REAPER where the call blocks the main thread
  indefinitely in headless sessions (waiting for a GUI dialog that never appears), permanently
  wedging the command queue. Registration now returns `registered: false` with a clear error
  pointing to `reaper.SetExtState()` as the correct data-return pattern.
- **`/health` reports `"status": "degraded"` when the main thread is stuck** — previously
  the health endpoint always returned `"status": "ok"` even when the command queue had been
  non-empty for seconds without draining (indicating a stuck main thread). Now reports
  `"degraded"` with a `degraded_reason` field after 10 s of queue stall.
- **Default command queue timeout raised from 5 s to 15 s** — 5 s was too tight for
  aarch64 ARM (Raspberry Pi); Lua scripts doing meaningful work (MIDI builds, FX probe
  loops) routinely exceeded it, leaving phantom items in the queue. 15 s is the new default
  across all platforms.
- **Per-call `timeout_ms` field on `POST /execute/action`** — agents running on slow
  hardware can now override the timeout per call (`"timeout_ms": 30000`), clamped to
  [1 000, 120 000] ms. Resolves #65.

## [1.11.1] - 2026-06-30

### Fixed
- **`POST /render` WAV format** — `wav_render_format()` was building a blob with
  `0x00000000` as the codec discriminator; REAPER's PCM sink expects the `"evaw"` FourCC
  prefix and rejected the blob with a blocking "Invalid render format!" dialog that froze
  the main thread. Fix: return `""` and let REAPER use its own built-in WAV default.
- **`POST /render` bounds mapping** — `bounds_flag()` had the entire REAPER API mapping
  wrong (`"project"` → 0, which is actually custom time range, not entire project). Correct
  mapping: `0`=custom, `1`=project, `2`=time_selection, `3`=all_regions. Default render with
  `bounds:"project"` was silently triggering "Nothing to render!" dialog and blocking the
  main thread for the full 300 s timeout. Both bugs found by a naive-agent benchmark run.

## [1.11.0] - 2026-06-29

### Added
- **`GET /` discovery landing page** — no auth required; returns `what_i_am`, 9-step
  `quick_start` recipe, `key_gotchas` list, and pointers to `/capabilities` and `/health`.
  A fresh agent with only a URL and token can orient in one call without any prior knowledge
  of ReaClaw.
- **`midi: true` in `POST /state/items` create spec** — creates a proper MIDI item via
  `CreateNewMIDIItemInProj`. Previously the only path (`AddMediaItemToTrack +
  PCM_Source_CreateFromType("MIDI")`) produced a take that silently rejected
  `MIDI_InsertNote`. The new flag is the correct and only reliable way to create an empty
  MIDI item for note insertion via `POST /state/items/{i}/midi`.
- **Lua runtime error capture** — scripts registered via `POST /scripts/register` are
  now wrapped in a `pcall` at write time. Runtime errors appear in the `POST /execute/action`
  response as `{status: "lua_error", lua_error: "<message with line number>"}` instead of
  silent `{status: "success"}`. Errors inside `reaper.defer()` callbacks are not captured
  (deferred execution is outside the pcall scope).
- **`GET /capabilities` updates** — `items.create` documents the `midi` field; `scripts`
  section expanded with `list`, `delete`, `execute`, `error_capture`, and `render_note`
  entries.

## [1.10.0] - 2026-06-29

### Added
- **Async action dispatch — `async` flag on `POST /execute/action` (partial #35)** — Pass
  `"async": true` to schedule the action via SWELL `SetTimer`/`WM_TIMER` instead of blocking
  the executor. Returns `{status:"queued", cmd_id, action_id}` immediately. The timer fires in
  REAPER's main message loop — a context where `Main_OnCommand` can safely drive a modal
  sub-event-loop (e.g. the render progress dialog) without deadlocking.
- **Transport verbs — play/stop/pause/record/cursor/loop (closes #49)** — First-class transport
  endpoints backed by `CSurf_On*` rather than opaque action IDs:
  - `POST /transport {action: play|stop|pause|record}` — drives `CSurf_OnPlay/Stop/Pause/Record`;
    returns transport state (playing, recording, paused, position) after dispatch.
  - `POST /transport/cursor {position, moveview?:false, seekplay?:false}` — moves the edit cursor
    to a project-time position in seconds via `SetEditCurPos`; returns actual cursor position.
  - `POST /transport/loop {start?, end?, enabled?}` — sets loop range (`GetSet_LoopTimeRange2`)
    and/or toggles repeat (`GetSetRepeatEx`); all fields optional so you can update range-only or
    enabled-only. Returns `{start, end, enabled}`.
  `GET /capabilities`: transport domain moves from `action` to `structured`.
- **Take-FX verbs — full `TakeFX_*` surface (closes #50)** — Mirrors the track-FX endpoints for
  item takes. New routes under `/state/items/{i}/takes/{t}/fx/...`:
  - `POST .../fx {name, enabled?, params?}` — add FX (`TakeFX_AddByName`)
  - `GET/POST/DELETE .../fx/{slot}` — read params / set enabled+params / remove
  - `POST .../fx/{slot}/copy {to_item, to_take, to_slot?:-1, move?:false}` — copy or move via
    `TakeFX_CopyToTake`
  - `GET/POST .../fx/{slot}/preset {name|navigate:-1|1}` — load/navigate presets
  Full TakeFX param snapshots (index, name, normalized value, formatted string, real-unit
  min/max/mid/raw). All mutations undo-wrapped. Coverage matrix updated.
- **Change-token polling endpoint (partial #31)** — `GET /state/changes` returns
  `{change_count}` from `GetProjectStateChangeCount()` — a cheap monotonic counter that
  increments on any project edit (GUI, control surface, another API client). Cache the count;
  re-read state only when it advances. Pair with `GET /snapshot/diff` to find what changed.
  Full event feed (IReaperControlSurface hook) remains out of scope.

## [1.9.0] - 2026-06-29

### Added
- **Project lifecycle endpoints (issue #34)** — four new `POST /project/*` verbs that let an
  agent manage the full project lifecycle without any GUI modal:
  - `POST /project/new` — open a blank project from REAPER's default template; returns 409 if
    the project has unsaved changes unless `discard_changes:true`.
  - `POST /project/open {path, discard_changes?}` — replace the current project with a `.rpp`
    file; same 409 guard; 400 if the file does not exist. Tab mode not supported (file replaces
    active project).
  - `POST /project/save {path?}` — save in-place (400 if project has never been saved and no
    path is given) or save-as to `path` (sets the new project filename).
  - `POST /project/reset {discard_changes?}` — blank the current project in-place: deletes all
    tracks/items/envelopes, all markers/regions, all extra tempo markers (resets to 120 BPM
    4/4), clears time selection/loop, parks cursor at 0, clears project notes. Deterministic on
    a headless/virtual display — no GUI modal. Proven recipe used live in the render-engine
    demos. Closes #34.
- `GET /capabilities` updated: `project` domain now shows `new/open/save/reset` verbs; removed
  "project open/save/new" from the `via_script_or_action` list.

## [1.8.0] - 2026-06-28

### Added
- **MIDI verbs — notes and CC read/write (issue #51)** — `GET /state/items/{index}/midi`
  returns all notes (pitch, note name, channel, velocity, start/end in both PPQ and project
  seconds, selected, muted) and CC events from the active MIDI take. `POST
  /state/items/{index}/midi` inserts or replaces notes and CC: accepts PPQ positions or
  project-time seconds, with sensible defaults (channel=0, velocity=100, duration=quarter
  note). `replace:true` clears existing content first; without it, events are appended. All
  mutations are undo-wrapped. Non-MIDI items return 400. `GET /capabilities` marks the `midi`
  domain `structured`. `GET /state/items/{index}/midi` is the first structured MIDI surface in
  ReaClaw (previously Lua-only). Closes #51.


- **State-chunk endpoint — universal reachability backstop (issue #48)** — `GET/POST
  /state/chunk` reads and writes the full RPP state chunk of any `track`, `item`, or
  `envelope` (`GetSetObjectState`/`Get*StateChunk`). Any property REAPER serializes is now
  reachable even without a dedicated verb, so combined with the action-runner and Lua escape
  hatch the automation surface is **provably 100% reachable**. Writes are undo-wrapped and bust
  the state cache; the read buffer grows up to 64 MB. `GET /capabilities` marks the
  `object_state_chunk` domain `structured`. Closes #48.
- **Capabilities coverage matrix + feature detection (issue #46)** — `GET /capabilities` now
  carries a `coverage` map of every REST-relevant REAPER domain → `{status, note}`
  (`structured`/`chunk`/`action`/`lua`/`out_of_scope`), an honest `sdk` summary
  (`functions_total:865, functions_called:131, raw_pct, reachable`), and a `features` object
  detecting optional dependencies (`sws`, `sws_r128_loudness`, `ffmpeg`, `xdotool`,
  `key_tempo_detector`) so agents branch instead of probe-and-fail. Closes #46.
- **Catalog search synonyms (issue #41)** — `GET /catalog/search` widens to a curated synonym
  map on a miss (strict-first, so precise queries keep precision): "folder depth" → "indent",
  "bounce" → "render", "colour" → "color", etc. Response adds `matched` + `expanded`. (The
  `interactive`/modal flag this issue also called for already shipped on every catalog result.)
  Closes #41.
- **Governance policies (issue #37)** — `ReaClaw_TECH_DECISIONS.md` now documents a deliberate
  **dependency policy** (Tier 0–3: vendored core required, SWS/external tools optional and
  feature-detected, network forbidden) and an explicit **API stability & versioning policy**
  (SemVer: additive→MINOR, breaking→MAJOR; documented + `/capabilities`-advertised endpoints are
  stable; no 2.0 for additive growth). Summarized in `docs/API.md`. Closes #37.
- **Track icons (issue #29)** — agents can now assign and read REAPER's built-in
  track icon set via the API. `GET /state/tracks` includes an `icon` field (`P_ICON`
  value, or `null` when unset). `icon` is writable on `POST /state/tracks/{index}`,
  `POST /state/tracks` (create + batch update): pass a relative name like `"bass.png"`
  (resolved against `Data/track_icons`), an absolute path, or `null`/`""` to clear.
  `GET /state/track-icons` discovers all available icon filenames so an agent can pick
  a valid name instead of guessing. If a relative name doesn't resolve to a file, the
  mutating response includes an `icon_not_found` hint in `hints[]`. Closes #29.
- **First-class offline render engine (Epic #32 / issue #33)** — `POST /render` triggers a
  synchronous offline render to a file from the agent, supporting WAV, FLAC, MP3, and OGG
  formats with configurable bit depth, sample rate, channel count, and render bounds
  (`project`, `time_selection`, `all_regions`, `custom`). RENDER_FORMAT blobs are built
  internally per format; callers never see the base64. Response includes `output_path`,
  `render_seconds`, `project_length`, and `offline_ratio` (speed multiple, e.g. 20×).
  Render settings are saved and restored after each call so agent renders do not
  permanently change the project's render configuration. Timeout: 300 s (covers ~100 min
  of project at 20× real-time). Closes #33.

### Fixed
- **Build break in `POST /render` (#33) — the project did not compile.** Every
  `GetSetProjectInfo_String` call in `src/handlers/render.cpp` passed five
  arguments, but the REAPER SDK signature takes four
  (`ReaProject*, const char* desc, char* valuestrNeedBig, bool is_set`). This
  broke `main` for both the render (#33) and track-icons (#29) merges; CI was red
  on every run since v1.7.0. Dropped the stray buffer-size argument from all nine
  calls (the buffers are already pre-sized, as the 4-arg form expects). Restores a
  green build.

## [1.7.0] - 2026-06-25

### Added
- **Snapshot / state-diff layer (Epic #20 prep)** — capture a canonical,
  diffable project-state snapshot, store it, and diff two snapshots (or a
  snapshot vs. live). `POST /snapshot {label?}`, `GET /snapshot`,
  `GET /snapshot/{id}`, `DELETE /snapshot/{id}`, and `GET /snapshot/diff?from=<id>
  &to=<id|current>` → a flat `[{path, op, from?, to?}]` change list. This is the
  shared cross-cutting layer the roadmap called for: it backs both the deferred
  #19 A/B visual diff and #20's correction mining. Pure recursive diff in
  header-only `util/jsondiff.h` (unit-tested). (#20)
- **Learned suggestions — the compounding moat (Epic #20 / Q8)** — local, opt-in
  mining of the agent's own edit history into *"after X, agents usually do Y"*
  associations, surfaced as suggestions tagged `method:"learned"` + `confidence`.
  `GET /suggestions?after=&agent=&limit=` and `GET /learn/stats`. Structured
  edits (track create/update fields, FX add, send add, item create) are recorded
  as events; antecedent→consequent transitions within a window accumulate in
  `learn_pairs`; confidence = P(consequent | antecedent). Distinct from the
  hand-authored hints of #18 though it shares the suggestion channel. (#20)
- **Local-first and opt-in.** Learning is **off by default** (`learning.enabled`);
  while disabled, nothing is recorded. All state lives in the local SQLite DB —
  there is no network egress, ever. New config block `learning`
  (`enabled`/`window_seconds`/`min_support`/`min_confidence`).

## [1.6.0] - 2026-06-23

Epic #19 — Visual perception & musical probes. The perception loop now produces
**pictures** (audio visualization) and reads **musical attributes** (key / pitch /
tempo), on top of Epic #18's audio analysis. On-demand screenshots gain named
surface targets. All built-in and dependency-free; every measure tagged
`method`+`confidence`. Verified live on REAPER 7.74 (aarch64): 440 Hz → A4,
261.6 Hz → C4, framed `arrange` capture, graceful external-tool absence.

### Added
- **Audio visualization (Epic #19 / Q4)** — `GET /analysis/item/{index}/visualize`
  and `GET /analysis/file/visualize` render a picture of an audio source as a
  base64 **PNG** *alongside* a machine-readable **digest** (the agent reads
  numbers, doesn't OCR pixels). Three `type`s: `spectrum` (32 log-spaced bands +
  centroid + low/mid/high), `waveform` (peak/RMS/clip + 32-pt envelope), and
  `loudness` (RMS contour over time). `width`/`height`/`start`/`end` params;
  `image=none` for digest-only. Long windows cap at 120 s decoded
  (`window.truncated`). Tagged `method`+`confidence` like the rest of perception.
  A/B snapshot diff deferred to the shared snapshot layer. (#19)
- Visualizations are **labelled charts**: spectrum renders as a log-frequency
  **EQ curve** (Hz ticks + dB scale), waveform/loudness carry a seconds axis, and
  the level plots a dB scale — via a small built-in 5×7 bitmap font on the canvas.
  Default size 640×200. (#19)
- Self-contained, dependency-free **PNG encoder + 8-bit RGB canvas** (`util/image`,
  incl. bitmap-font text + thick lines) and a shared header-only **FFT**
  (`util/dsp`, factored out of `analysis.cpp`). Unit-tested (12 new tests: PNG
  round-trip, base64 vectors, font/line drawing, FFT vs naive DFT; 50/50).
- **Musical-attribute probes (Epic #19 / Q7)** —
  `GET /analysis/item/{index}/probe` and `GET /analysis/file/probe` return
  **pitch**, **key**, and **tempo**, each tagged with its truth source. Pitch
  (dominant fundamental → note + cents) and key (chromagram + Krumhansl–Schmuckler
  correlation) are built-in `estimated_dsp`; tempo is exact `introspection` (the
  project tempo at the item's position) plus an optional external detector
  (`bpm-tools`' `bpm-tag`) for tempo-from-audio that **degrades gracefully**
  (`available:false`) when the tool is absent. `?probes=` filter; `start`/`end`
  windowing. Pure note/key math lives in header-only `util/music.h` (7 new unit
  tests; A440→A4, C-major profile→C major, etc.). Verified live on REAPER 7.74
  (aarch64): 440 Hz sine → A4 (conf 0.95), 261.6 Hz → C4. (#19)
- **Named screenshot surfaces (Epic #19 / Q5)** — `GET /screenshot` now accepts a
  named `target` (`arrange`/`reaper`, `mixer`, `fxchain`, `midi`, `routing`,
  `master`, `transport`, `explorer`) that auto-frames that REAPER surface, in
  addition to `screen`, `window=<title>`, and `region=x,y,w,h`. `width=` downscales
  to bound token cost. Capture rectangles are **clamped to the screen** so a
  maximized window's geometry no longer makes x11grab fail. (#19)
- **404 messages preserved** — the catch-all error handler now only supplies the
  generic "Not found" body for genuinely unmatched routes, so handler-authored 404
  messages ("Item index out of range", "No visible window matching '…'") survive
  instead of being flattened.

### Deferred (within #19)
- **A/B visual diff** against an earlier snapshot — depends on the cross-cutting
  snapshot / state-diff layer (also needed by Epic #20). Built once, shared; not
  bundled into #19. See `ReaClaw_ROADMAP.md` §4 and TECH_DECISIONS §17.

## [1.5.0] - 2026-06-22

Epic #18 — Audio perception ("the agent hears itself"). The headline
differentiator: the agent can now **measure its own output** and is told the
**consequence of its own edits**. Built-in, always-available, no external tool.
Every measure is tagged with a `method` + `confidence` so the agent trusts each
number correctly. Verified live on REAPER 7.74 (aarch64): 12/12 checks, incl. a
440 Hz sine resolving to a 439.997 Hz spectral centroid.

### Added
- **Loudness analysis** — `GET /analysis/item/{index}` and
  `GET /analysis/file?path=` return exact offline LUFS-I / RMS-I / peak /
  true-peak (via REAPER's `CalculateNormalization` full-decode) plus derived
  clipping (digital + inter-sample). `?measures=` filter and `start`/`end`
  windowing. (#18)
- **Spectral balance** — a rough low/mid/high band-energy digest + spectral
  centroid, computed by decoding samples (`PCM_source::GetSamples`) through a
  small in-tree FFT. Tagged `estimated_dsp`, confidence 0.6. (#18)
- **Live metering** — `GET /state/meters`: per-track + master `peak_db` /
  `peak_hold_db` (dBFS) from REAPER's own meters, plus `audio_running`. (#18)
- **Consequence-aware hints (Q3)** — mutating responses for track update, add
  FX, add send, and item create/update carry a `hints[]` array of hand-authored
  invariants surfaced inline (`muted_track`, `solo_elsewhere`,
  `near_silent_fader`, `routes_nowhere`, `recarm_no_input`, `fx_offline`,
  `fx_bypassed`, `send_dest_routes_nowhere`, `send_dest_muted`, `empty_item`,
  `midi_no_instrument`, `phase_inverted`). (#18)
- All analysis output tagged `method` (`offline_analysis` / `estimated_dsp` /
  `introspection` / `derived`) + `confidence`. `/capabilities` gains a
  `perception` section; `docs/API.md` and TECH_DECISIONS §17 updated.

### Deferred (within #18)
- Onset/density detection (transient analysis) — not in the epic's acceptance
  criteria; the Lua escape hatch + the loudness/spectral surface cover the
  near-term need. DSP locus (in-process vs. dedicated analyzer) recorded as an
  open question in TECH_DECISIONS §17.

## [1.4.0] - 2026-06-22

Epic #17 — Tier-B content manipulation. Media items, takes, and sources were
read-only before this release; this adds the write surface for the objects
*inside* a track, plus deeper track/FX properties and a per-project scratchpad.
All mutations are wrapped in undo blocks (one undoable step each). Verified live
on REAPER 7.74 (aarch64).

### Added
- **Media item CRUD** — `POST /state/items` (create/batch-update),
  `POST /state/items/{index}` (update), `POST /state/items/{index}/split`,
  `DELETE /state/items/{index}`, and `GET /state/items/{index}`. Create accepts a
  `file` to load an audio/MIDI source (length defaults to the source length);
  update can move an item to another track. (#17)
- **Enriched item reads** — `GET /state/items` now returns `selected`, `muted`,
  `volume_db`, `fade_in`, `fade_out`, a `take` object (name, volume, pan, pitch,
  playrate, preserve-pitch, polarity) and a `source` object (file, type, length,
  sample rate, channel count). `take`/`source` are `null` for empty items. (#17)
- **Take properties** — read/write take vol/pan/pitch/playrate/preserve-pitch/name
  via the item `take` object. (#17)
- **Track extras** — `phase`, `n_channels`, `pan_mode`, `dual_pan_l`/`dual_pan_r`,
  `rec_input`, `midi_hw_out`, `main_send` added to `GET`/`POST /state/tracks`. (#17)
- **FX copy** — `POST /state/tracks/{index}/fx/{slot}/copy {to_track,to_slot,move}`
  duplicates (or moves) a configured FX to another track without Lua. (#17)
- **FX online/offline** — FX reads expose `offline`; `POST .../fx[/{slot}]` accepts
  `offline`. (#17)
- **Item selection write** — `POST /state/selection` now also accepts
  `items: [j,...] | "all" | "none"` (returns `selected_items`). (#17)
- **Project ext state** — `GET/POST/DELETE /project/extstate {section,key,value}`,
  a persistent per-project scratchpad stored in the `.rpp`. (#17)
- `GET /capabilities` updated with the items/take/source surface, track extras,
  FX copy/offline, item selection, and project ext state.

### Changed
- `GET /state/items` moved to `src/handlers/items.cpp` and is no longer cached — it
  reads through the main thread so item edits are always reflected immediately.

## [1.3.0] - 2026-06-21

### Added
- **Action names in logs & history** — executing an action now logs and returns the
  resolved human-readable name, not just the numeric id. `POST /execute/action` (and
  each sequence step) returns `action_name`; `GET /history` rows carry `target_name`
  (new `execution_history.target_name` column, migrated on open). Names resolve from
  the bundled catalog, so they work for native actions even without SWS. (#8)
- **Readable track structure** — `GET /state/tracks` now includes `folder_depth`
  (`1`=folder parent, `0`=normal, negative=closes folders) and `color` (`"#RRGGBB"`
  or `null`), so agents can verify folders/colors without screenshotting the GUI.
  (Perception roadmap Q2 / #9)
- **Screenshot recipe in the agent Skill** — `skill/reaclaw/SKILL.md` now documents
  how to *see* REAPER when asked: capture the live window with `ffmpeg x11grab`
  (not `xwd`, which returns blank GDK/SWELL client areas), crop to a region, and
  `Read` the PNG. Structure-first stays the default; vision is the on-demand
  fallback. (Perception roadmap Q5)
- **Polished menu dialogs** — the Extensions › ReaClaw *Status*, *View log*, and
  *Copy API key* surfaces are now proper SWELL/Win32 dialogs (live status with LED +
  copy-address, scrollable log viewer with refresh, API-key field with copy +
  "Copied!" confirmation) instead of plain `MessageBox` popups. New
  `src/panel/dialogs.{h,cpp}`, `src/panel/resource.h`, `src/panel/dialogs.rc`. (#2)

- **High-level structured commands (tiered coverage)** — the "easy commands" that
  collapse the 146-call friction-log session into a handful of requests:
  - Tracks: `POST /state/tracks` (create + batch update), `DELETE /state/tracks/{i}`,
    and `POST /state/tracks/{i}` now also writes `name`, `color`, and `folder_depth`
    (on top of mute/solo/arm/volume/pan).
  - FX: `POST /state/tracks/{i}/fx` (add by name), `GET`/`POST`/`DELETE`
    `/state/tracks/{i}/fx/{slot}` — enable/bypass and parameter get/set (normalized
    0..1, by param index or name).
  - Routing: `POST`/`DELETE /state/tracks/{i}/sends[/{send}]`; track reads now
    include a `sends[]` array (`dest_track`, `volume_db`, `pan`).
  - Selection: `POST /state/selection` (`[i,...]`, `"all"`, or `"none"`).
  - `GET /capabilities` — manifest of what's supported directly vs. via an action
    or Lua script.

- **Agent-friendliness layer (Stage 3)** — sits outside the C++ extension:
  - **Agent Skill** (`skill/reaclaw/SKILL.md`) — loads ReaClaw know-how into an
    agent's context: structured-verb recipes, action cheat-sheet, a modal-action
    "don't" list, and a decision guide.
  - **MCP server** (`mcp/`) — Python (FastMCP) server exposing 18 typed tools over
    the REST API; the `ReaClawClient` core uses only the stdlib and is usable
    standalone.
  - **Semantic action search** — `search_actions` embeds the action catalog via a
    local Ollama model (`nomic-embed-text`), caches it, and ranks by cosine
    similarity so natural-language queries map to actions; falls back to keyword
    search when embeddings are unavailable.

- **Tier-A control verbs (Epic #16)** — the high-value structured verbs that previously
  required parameterless actions or hand-written Lua:
  - **Undo grouping** — every structured mutation now runs inside a REAPER undo block,
    so each lands as a single, user-undoable step (no-ops create no undo point).
    `GET /undo` (next undo/redo descriptions), `POST /undo`, `POST /redo`.
  - **Markers & regions** — `GET`/`POST /state/markers`, `DELETE /state/markers/{id}`
    (`?is_region=`); add by position with optional name/region-end/`#RRGGBB` color.
  - **Tempo / time-signature map** — `GET`/`POST /state/tempo` (full map read; add a
    tempo/time-sig marker). `GET /time?time=` / `?beats=[&measure=]` for beat↔seconds.
  - **FX presets** — `GET`/`POST /state/tracks/{i}/fx/{slot}/preset` (load by `name`
    or `navigate:-1|1`).
  - **FX param metadata** — FX param reads now also carry real-unit `raw`/`min`/`max`/
    `mid` (from `TrackFX_GetParamEx`), not just normalized 0..1.
  - **Envelope automation write** — `POST /state/tracks/{i}/automation`
    (`{envelope, points:[{time,value,shape,tension}], clear_range:[s,e]}`).
  - **Send extended props** — `POST /state/tracks/{i}/sends/{send}` and the create
    body now accept `muted`/`phase`/`mono`/`mode` (pre/post-fader); track `sends[]`
    reads expose them too.
  - **Project extras** — `GET /project` (dirty flag, length, notes), `POST /project/notes`.
  - **MIDI editor catalog** — `GET /catalog[/search]?section=midi_editor` indexes the
    MIDI editor action section (separate table to avoid main-section ID collisions).
  - **Catalog `interactive` flag** — every catalog row now reports whether the action
    opens a modal dialog (name heuristic + curated exceptions like `40696`), so agents
    can avoid hanging headless.
  - `GET /capabilities` updated to advertise all of the above.

### Changed
- `GET /state/tracks` track objects now include a `sends` array (with extended
  `muted`/`phase`/`mono`/`mode` props per send).
- Version bumped to **1.3.0**.

### Docs
- Added `ReaClaw_IDEAS.md` — perception/learning/discovery idea queue and the
  direction decisions taken for the Phase 4 build-out.
- Added `ReaClaw_ROADMAP.md` — consolidated forward plan (control + perception);
  maps the open epics (#16–#20) to GitHub issues.
- `ReaClaw_TECH_DECISIONS.md` §16 — API coverage is **tiered** (structured verbs +
  action-runner + Lua escape hatch); resolves the open question in #7. Updated to
  record markers/regions, tempo map, and envelope writes graduating to verbs (#16).

---

## [1.2.0] - 2026-06-18

### Added
- **Extensions menu** — ReaClaw is now controlled from a native `Extensions › ReaClaw`
  submenu (built via `hookcustommenu` + `AddExtensionsMainMenu()`):
  - **Start/stop server** — toggles the HTTPS server (checked while running)
  - **Status…** — message box with address, auth mode, uptime, and version
  - **Open config file** — opens `config.json` in the OS default editor
  - **View log** — opens the log file (or notes logging goes to the REAPER console)
  - **Copy API key** — copies `auth.key` to the clipboard
- Each item is also a registered action (`REACLAW_SERVER_TOGGLE`, `REACLAW_STATUS`,
  `REACLAW_OPEN_CONFIG`, `REACLAW_VIEW_LOG`, `REACLAW_COPY_API_KEY`) — visible in the
  Actions list and bindable to keys/toolbar buttons.

### Removed
- **Dockable control panel** (`src/panel/panel.cpp`, `panel.h`, `panel.rc`, `resource.h`)
  and its action `REACLAW_PANEL_TOGGLE`, superseded by the Extensions menu. The SWELL
  function-pointer table (`swell_stub.cpp`) is retained.

### Notes
- Menu actions dispatch through `hookcommand2` (the correct hook for `custom_action`s),
  not the older main-section-only `hookcommand`.

---

## [1.1.0] - 2026-04-11

### Added
- Dockable control panel (`src/panel/`) — toggle via REAPER Actions list ("ReaClaw: Control Panel") or toolbar button
  - Enable/disable server checkbox
  - Host and Port text fields
  - Bypass TLS cert validation checkbox
  - Log file viewer (caps at 4 KB) with Refresh button
  - Apply button — saves `config.json` and starts/stops/restarts the server
- Linux/macOS: SWELL-based dialog resource defined inline; SWELL function pointers populated at runtime from `SWELLAPI_GetFunc` exported by REAPER host
- Windows: native Win32 dialog resource (`src/panel/panel.rc`) via `CreateDialogParam`
- REAPER docker integration via `DockWindowAddEx` — panel dock state persists across sessions
- Registered as REAPER custom action `REACLAW_PANEL_TOGGLE` with `hookcommand` and `toggleaction` support

---

## [1.0.5] - 2026-04-10

### Fixed
- Windows DLL now statically links MinGW runtime (`libwinpthread-1.dll`, `libgcc_s_seh-1.dll`, `libstdc++-6.dll`); resolves "entry point not found: __gthr_win32_self" error on systems without MinGW installed

---

## [1.0.3] - 2026-04-08

### Changed
- CI: Linux build, format check, and release jobs now run on self-hosted ARC runner (`arc-runner-reaclaw`) on k3s cluster; Windows and macOS remain on GitHub-hosted runners

---

## [1.0.2] - 2026-04-08

### Added
- GitHub Actions release workflow: builds `.dll` / `.dylib` / `.so` on Windows, macOS, and Linux and publishes them as GitHub Release assets on every `v*` tag
- `docs/DEPLOYMENT.md`: headless Linux testing section covering all dependencies, virtual display (Xvfb), virtual audio device (`snd-dummy`), REAPER pre-configuration, launch sequence, and integration test script

---

## [1.0.1] - 2026-04-08

### Fixed
- `ReaperPluginEntry`: inverted `REAPERAPI_LoadAPI` return-value check caused the plugin to silently abort on every load (the SDK returns 0 on success, not non-zero)
- Script execution: registered ReaScripts now store the REAPER command ID (`reaper_cmd_id`) returned by `AddRemoveReaScript`; execute handler uses this directly instead of relying on `NamedCommandLookup`, which does not resolve scripts by our naming convention

### Changed
- DB schema: `reaper_cmd_id INTEGER DEFAULT 0` column added to `scripts` table via `ALTER TABLE` migration (backward-compatible with existing databases)

---

## [1.0.0] - 2026-04-08

### Added
- `Strict-Transport-Security: max-age=31536000` header on all responses
- Path traversal guard: script paths verified inside `scripts_dir` before write (`lexically_relative` check)
- Auth failure audit log: distinct `WARN` messages for missing header, malformed header, and wrong key — each includes client IP
- `GET /health` now returns `queue_depth`, `db_ok`, `server_running`
- 1-second TTL read cache for `GET /state`, `GET /state/tracks`, `GET /state/items`; invalidated on `POST /state/tracks/{index}` writes
- Structured log format: set `logging.format: "json"` for newline-delimited JSON log output
- `docs/MCP.md` — MCP tool definitions and OpenClaw/Sparky integration guide
- `docs/DEPLOYMENT.md` — platform build instructions, config reference, security checklist, troubleshooting

### Changed
- `Log::init()` accepts optional `format` parameter (`"text"` or `"json"`)
- `config.json` gains `logging.format` field (default `"text"`)

### Notes
- SQLite indexes (`idx_history_executed_at`, `idx_scripts_name`) were already present in schema from Phase 1
- Agent identification (`X-Agent-Id` → `execution_history.agent_id`) was already implemented in Phase 1

---

## [0.2.0] - 2026-04-07

### Added
- `POST /scripts/register` — Lua ReaScript registration with SHA-256–derived action IDs
- `GET /scripts/cache[?tags=]` — list registered scripts with optional tag filter
- `GET /scripts/{id}` — fetch script metadata and Lua source
- `DELETE /scripts/{id}` — unregister script from REAPER, disk, and DB
- `POST /execute/sequence` — multi-step execution with per-step feedback and `stop_on_failure`
- `GET /history[?agent_id=]` — execution audit log with agent filtering
- Lua syntax validation via `luac -p` subprocess; graceful fallback if luac absent
- Idempotent script registration: same name returns existing action ID unchanged
- Script `execution_count` and `last_executed` tracking on `POST /execute/action`
- `docs/API.md` — full Phase 0 + Phase 1 endpoint reference
- `docs/EXAMPLES.md` — curl examples for all Phase 1 flows

---

## [0.1.0] - 2026-04-07

### Added
- `GET /health` endpoint
- Action catalog indexing via `kbd_enumerateActions`
- `GET /catalog`, `GET /catalog/search`, `GET /catalog/{id}`, `GET /catalog/categories`
- `GET /state`, `GET /state/tracks`, `GET /state/items`, `GET /state/selection`, `GET /state/automation`
- `POST /state/tracks/{index}` — direct track property writes via `GetSetMediaTrackInfo`
- `POST /execute/action` — single action execution via main-thread command queue
- HTTPS server with self-signed cert generation (cpp-httplib + OpenSSL)
- API key authentication middleware
- SQLite persistence (action catalog, scripts, execution history)
- Cross-platform CMake build (Windows MSVC, macOS Clang, Linux GCC/Clang)

---

<!-- Releases will be added here as they are tagged -->
<!-- Example entry format:

## [0.1.0] - YYYY-MM-DD

### Added
- `GET /health` endpoint
- Action catalog indexing via `kbd_enumerateActions`
- `GET /catalog`, `GET /catalog/search`, `GET /catalog/{id}`, `GET /catalog/categories`
- `GET /state`, `GET /state/tracks`, `GET /state/items`, `GET /state/selection`, `GET /state/automation`
- `POST /state/tracks/{index}` — direct track property writes via `GetSetMediaTrackInfo`
- `POST /execute/action` — single action execution via main-thread command queue
- HTTPS server with self-signed cert generation (cpp-httplib + OpenSSL)
- API key authentication middleware
- SQLite persistence (action catalog, execution history)
- Cross-platform CMake build (Windows MSVC, macOS Clang, Linux GCC/Clang)

-->

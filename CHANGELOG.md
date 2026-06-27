# Changelog

All notable changes to ReaClaw are documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added
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

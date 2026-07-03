# ReaClaw: Implementation Checklist

Each phase is a shippable unit. Complete and test each phase before starting the next.

---

## Phase 0: Foundation

**Goal:** A working REAPER extension that starts an HTTPS server, indexes the action catalog, responds to state queries, and executes single actions.

### Project Setup

- [x] Create `CMakeLists.txt` (see Design doc §8 for full structure)
- [x] Create `vendor/` and populate:
  - [x] Download `httplib.h` from yhirose/cpp-httplib (latest release)
  - [x] Download `json.hpp` from nlohmann/json (3.x, single header)
  - [x] Download SQLite amalgamation (`sqlite3.c`, `sqlite3.h`) from sqlite.org
  - [x] Clone or copy REAPER SDK headers into `vendor/reaper-sdk/` (justinfrankel/reaper-sdk)
- [x] Add `.gitignore`: `build/`, `certs/`, `*.sqlite`, `.DS_Store`, `*.user`, `.vs/`
- [x] Verify CMake produces a `.dll`/`.dylib`/`.so` from a stub `main.cpp`
- [x] Copy to REAPER `UserPlugins`, restart REAPER — confirm it loads without crash

### Extension Entry Point (`src/main.cpp`)

- [x] Implement `ReaperPluginEntry(HINSTANCE, reaper_plugin_info_t*)`
  - [x] `rec == NULL` branch: call `ReaClaw::shutdown()`, return 0
  - [x] `rec->caller_version != REAPER_PLUGIN_VERSION`: return 0
  - [x] Call `REAPERAPI_LoadAPI(rec->GetFunc)` to bind all API function pointers
  - [x] Call `ReaClaw::init(rec)`, return 1
- [x] Print startup message: `ShowConsoleMsg("ReaClaw: starting...\n")`
- [x] Register main-thread timer callback: `plugin_register("timer", timer_callback_fn)`
- [x] After catalog index and DB are ready, reconcile registered scripts:
  - [x] For each row in `scripts` table, check if the `.lua` file still exists on disk
  - [x] Re-call `AddRemoveReaScript(true, 0, path, true)` for each script to ensure it is registered (handles REAPER reinstall or `reaper-kb.ini` reset)
  - [x] Log count: `"ReaClaw: re-registered N scripts"`
- [x] Unregister timer on unload

### Config (`src/config/`)

- [x] `Config::load()`:
  - [x] Call `GetResourcePath()` to find REAPER resource dir
  - [x] Create `{ResourcePath}/reaclaw/` if it doesn't exist
  - [x] If `config.json` missing, write defaults and log a notice
  - [x] Parse with nlohmann/json into a `Config` struct
- [x] Config struct: `server.{host, port, thread_pool_size}`, `tls.{enabled, generate_if_missing, cert_file, key_file}`, `auth.{type, key}`, `database.path`, `script_security.{validate_syntax, log_all_executions, max_script_size_kb}`, `logging.{level, file}`
- [x] Unit test: valid config loads; missing fields use defaults

### Database (`src/db/`)

- [x] Open SQLite at `{ResourcePath}/reaclaw/reaclawdb.sqlite` (or config path)
- [x] Run `schema.sql` on open (`CREATE TABLE IF NOT EXISTS` for all tables)
- [x] `schema.sql` tables:
  - `actions` (id, name, category, section, created_at)
  - `actions_fts` (FTS5 virtual table on actions)
  - `scripts` (id, name, body, script_path, tags, execution_count, created_at, last_executed)
  - `execution_history` (id, type, target_id, agent_id, status, error, executed_at)
- [x] `DB` class with `execute(sql)`, `query(sql, params) → rows`, `insert(...)` helpers
- [x] Unit test: create DB, insert and query a row

### REAPER API Layer (`src/reaper/api.cpp`)

- [x] `#define REAPERAPI_IMPLEMENT` before `#include "reaper_plugin_functions.h"`
- [x] After `REAPERAPI_LoadAPI`, verify all needed function pointers are non-null; log warning for any missing
- [x] Functions needed for Phase 0:
  - `kbd_enumerateActions`, `SectionFromUniqueID`
  - `CountTracks`, `GetTrack`, `GetTrackName`, `GetSetMediaTrackInfo`
  - `GetProjectTimeSignature2`, `GetCursorPosition`, `GetPlayState`
  - `Main_OnCommand`
  - `GetResourcePath`, `GetAppVersion`, `ShowConsoleMsg`

### Action Catalog Indexer (`src/reaper/catalog.cpp`)

- [x] On startup, check if `actions` table is empty, REAPER version changed, or catalog builder version changed
  - [x] If rebuild needed: truncate `actions`, then:
    - [x] Seed native built-in actions from the bundled static table (`src/reaper/native_actions.gen.h`) — REAPER does not expose these via any SDK enumeration call
    - [x] Live-overlay installed extension/script actions: probe command-ID range (`1..0xFFFF`) via `kbd_getTextFromCmd`, plus a `kbd_enumerateActions` pass
  - [x] Derive category from name prefix (e.g. `"Track: "` → `"Track"`)
  - [x] Bulk-insert into `actions`; rebuild `actions_fts`
  - [x] Store current `GetAppVersion()` result for next-run comparison
  - [x] Log: `"ReaClaw: indexed N actions"`
- [x] Unit test: mock enumeration; verify correct rows inserted

### Command Queue + Timer Callback (`src/reaper/executor.cpp`)

- [x] `Command` struct: `std::function<void()> execute`, `std::promise<nlohmann::json> result`
- [x] `std::queue<Command>` + `std::mutex`
- [x] `post_command(fn) → std::future<nlohmann::json>`: enqueue and return future
- [x] Timer callback drains queue on main thread (up to 10 commands per tick); sets promise for each
- [x] Handler thread waits on future with 5s timeout; return HTTP 408 on timeout

### TLS Utilities (`src/util/tls.cpp`)

- [x] `TLS::generate_self_signed(cert_path, key_path)`:
  - [x] Generate 4096-bit RSA key via `EVP_PKEY_keygen`
  - [x] Create self-signed X.509 cert (10 year validity)
  - [x] Write PEM-encoded cert and key to disk
  - [x] Log path on success

### HTTPS Server (`src/server/`)

- [x] `server.cpp`: Initialize `httplib::SSLServer` with cert/key paths
  - [x] If `tls.generate_if_missing` and cert/key missing, call `TLS::generate_self_signed()` first
  - [x] Set thread pool size from config
  - [x] Auth middleware: check `Authorization: Bearer {key}` on every request when `auth.type == "api_key"`; return 401 on mismatch
  - [x] Set `Content-Type: application/json` on all responses
- [x] `router.cpp`: Register all route handlers (Phase 0 routes; return 501 for unimplemented)
- [x] Start server on background thread; store thread handle for shutdown join

### Phase 0 API Handlers

- [x] `GET /health` → version, uptime, catalog size, REAPER version
- [x] `GET /catalog` → paginated action list from SQLite
- [x] `GET /catalog/search?q=` → FTS5 full-text search on `actions_fts`
- [x] `GET /catalog/{id}` → single action lookup by numeric ID; 404 if not found
- [x] `GET /catalog/categories` → `SELECT category, COUNT(*) FROM actions GROUP BY category`
- [x] `GET /state` → BPM, time signature, cursor, transport, track count (threadsafe REAPER calls)
- [x] `GET /state/tracks` → enumerate all tracks with name, mute, solo, armed, volume, FX
- [x] `POST /state/tracks/{index}` → set track properties directly via `GetSetMediaTrackInfo`; accepts any combination of `muted`, `soloed`, `armed`, `volume_db`, `pan`; returns updated track state; 404 if index out of range
- [x] `GET /state/items` → media items with position, length, track index
- [x] `GET /state/selection` → selected tracks and items
- [x] `GET /state/automation` → automation envelopes for selected track
- [x] `POST /execute/action` → post to command queue; await result; log to `execution_history`; return feedback if requested

### Logging (`src/util/logging.cpp`)

- [x] Level filter from config (`debug`, `info`, `warn`, `error`)
- [x] Write to REAPER console via `ShowConsoleMsg` if no log file set
- [x] Write to file if `logging.file` is configured
- [x] Format: `[LEVEL] [timestamp] message`

### Phase 0 Deliverable

- [x] Extension loads in REAPER on Windows, macOS, Linux without crash
- [x] `GET /health` returns 200 with correct data
- [x] `/catalog/search?q=mute` returns relevant actions
- [x] `GET /catalog/40285` returns the correct action
- [x] `/state` returns correct BPM and transport state
- [x] `/state/tracks` returns correct track list
- [x] `POST /state/tracks/0 {"muted": true}` mutes track 0; response confirms new state
- [x] `POST /execute/action {"id": 40285}` executes in REAPER
- [x] Auth middleware rejects requests with wrong key
- [x] All executions appear in `execution_history`
- [x] Push `main`; tag `v0.1.0`
- [x] Write `docs/API.md` for Phase 0 endpoints

---

## Phase 1: Scripts & Sequences

**Goal:** Agents register Lua ReaScripts as custom REAPER actions and run multi-step sequences with per-step feedback.

### Prerequisites

- [x] Phase 0 complete and tagged

### Script Registration (`src/reaper/scripts.cpp`)

- [x] `Scripts::register_script(name, body, tags) → {action_id, error}`:
  - [x] Generate unique ID: `_{name}_{sha256_prefix_8chars}`
  - [x] Check idempotency: if same name already in `scripts` table, return existing ID
  - [x] Validate Lua syntax:
    - [x] Write body to a temp file
    - [x] Shell out to `luac -p {tempfile}`; capture stdout/stderr
    - [x] If `luac` not on PATH, fall back to bracket/keyword check
    - [x] On failure: return `{ registered: false, syntax_error: { line, message } }`
  - [x] On success: write body to `{ResourcePath}/reaclaw/scripts/{action_id}.lua`
  - [x] Post to command queue: call `AddRemoveReaScript(true, 0, path, true)`
  - [x] Insert into `scripts` table
  - [x] Return `{ action_id, registered: true, script_path }`
- [x] `Scripts::unregister_script(action_id)`:
  - [x] Post to command queue: call `AddRemoveReaScript(false, 0, path, true)`
  - [x] Delete `.lua` file from disk
  - [x] Remove row from `scripts` table

### Phase 1 API Handlers

- [x] `POST /scripts/register` → call `Scripts::register_script`; return result
- [x] `GET /scripts/cache` → query all rows from `scripts` table; support `?tags=` filter
- [x] `GET /scripts/{id}` → fetch row + read file body from disk
- [x] `DELETE /scripts/{id}` → call `Scripts::unregister_script`

### Multi-Step Sequence (`src/handlers/execute.cpp`)

- [x] `POST /execute/sequence`:
  - [x] Iterate `steps` array
  - [x] For each step: post to command queue; await result
  - [x] If `feedback_between_steps: true`, call state read functions after each step; include in step result
  - [x] If `stop_on_failure: true`, abort on first failure; mark remaining steps as `"skipped"`
  - [x] Log each step to `execution_history` (type `"sequence"`, target = sequence label or first action ID)
  - [x] Return: overall status, `steps_completed`, per-step results array

### Execution History (`src/handlers/history.cpp`)

- [x] `GET /history`:
  - [x] Query `execution_history` ordered by `executed_at DESC`
  - [x] Support `?limit=`, `?offset=`, `?agent_id=` query params
  - [x] Read `X-Agent-Id` request header on all endpoints and store in `execution_history.agent_id`

### Phase 1 Deliverable

- [x] `POST /scripts/register` with valid Lua: script appears in REAPER Actions list
- [x] `POST /execute/action` with the registered ID runs the script in REAPER
- [x] Registering same name twice returns existing ID (idempotent)
- [x] `POST /scripts/register` with syntax error returns error + line number; nothing written to disk or REAPER
- [x] `DELETE /scripts/{id}` removes from REAPER Actions list and disk
- [x] `POST /execute/sequence` with 5 steps: all execute in order; per-step feedback returned
- [x] `stop_on_failure: true` stops at first failure; remaining steps marked skipped
- [x] `GET /history` returns accurate log
- [x] Push `main`; tag `v0.2.0`
- [x] Update `docs/API.md` with Phase 1 endpoints
- [x] Add `docs/EXAMPLES.md` with script registration and sequence examples

---

## Phase 2: Integration & Hardening

**Goal:** MCP wrapper for OpenClaw/Sparky; agent identification; performance; security audit.

### Prerequisites

- [x] Phase 1 complete and tagged

### Agent Identification

- [x] Read `X-Agent-Id` header on all requests; store in `execution_history.agent_id` — done in Phase 1
- [x] `GET /history?agent_id=sparky` filters by agent — done in Phase 1

### Performance

- [x] Add SQLite indexes: `execution_history(executed_at)`, `scripts(name)` — already in schema from Phase 1
- [x] Cache frequent state reads in memory with 1s TTL — `/state`, `/state/tracks` cached; invalidated on track writes. (`/state/items` was later moved to uncached main-thread reads when item CRUD landed, so item edits are always reflected — see `docs/API.md`.)
- [x] Profile all Phase 0–1 endpoints (requires live REAPER; targets: catalog search <50ms, state queries <100ms, action execution <200ms excluding queue wait) — verified: catalog search ~47ms sequential, state queries ~47ms; action execution well under 200ms

### Optional MCP Wrapper

- [x] Design MCP tool definitions: `reaclawExecuteAction`, `reaclawExecuteSequence`, `reaclawQueryState`, `reaclawRegisterScript`, `reaclawSearchCatalog`
- [x] Write `docs/MCP.md` — MCP tool definitions and OpenClaw/Sparky integration guide

### Security Hardening

- [x] Add `Strict-Transport-Security` header to all responses (`router.cpp` `auth_wrap`)
- [x] Validate all input sizes (script body ≤ max_script_size_kb, step arrays ≤ 100 items) — done in Phase 1
- [x] Ensure script files are written only inside `{ResourcePath}/reaclaw/scripts/`; reject path traversal in script names (`reaper/scripts.cpp` — `lexically_relative` check after path construction)
- [x] Audit log for auth failures (wrong API key → log IP and timestamp) (`auth/auth.cpp` — distinct warn messages for missing header, malformed header, wrong key)
- [x] Agent identification (`X-Agent-Id` → `execution_history.agent_id`) — already implemented in Phase 1 execute handlers

### Observability

- [x] `GET /health` enhancements: `queue_depth`, `db_ok`, `server_running` added
- [x] Structured log format option: `logging.format: "json"` — newline-delimited JSON output

### Phase 2 Deliverable

- [x] Security hardening checklist complete
- [x] MCP tool definitions documented (`docs/MCP.md`)
- [x] Write `docs/DEPLOYMENT.md` with platform-specific build and install instructions
- [x] Push `main`; tag `v1.0.0`
- [x] Load test: 10 concurrent agents making catalog searches — all <50ms (requires live REAPER) — verified: 10 concurrent requests 64–140ms (4-thread pool contention); sequential <50ms each; acceptable

---

## Phase 3: Extensions Menu (v1.2.0)

**Goal:** Control ReaClaw from a native `Extensions › ReaClaw` menu without editing config.json.

> **Superseded the dockable control panel (v1.1.0).** The original Phase 3 was a
> dockable SWELL/Win32 dialog (`src/panel/panel.cpp`, `panel.rc`, `resource.h`).
> It was replaced in v1.2.0 by a lightweight menu under REAPER's main "Extensions"
> menu — simpler, less GUI surface, and no docking edge cases. The panel sources
> were removed; `swell_stub.cpp` (SWELL function table) is retained.

- [x] `src/panel/menu.cpp` — registers actions + builds the submenu via `hookcustommenu`
- [x] `src/panel/menu.h` — Menu::init / destroy
- [x] `src/panel/swell_stub.cpp` — SWELL function pointer table (retained from v1.1.0)
- [x] `src/reaper/api.cpp` — call Menu::init after server start; Menu::destroy on shutdown
- [x] `CMakeLists.txt` — build `menu.cpp` in place of the panel sources/resource

### Menu items (Extensions › ReaClaw)
- **Start/stop server** — toggles the HTTPS server (checked while running); extension stays loaded
- **Status…** — live SWELL dialog: status LED, address (+Copy), auth mode, uptime, version
- **Open config file** — opens `config.json` in the OS default editor
- **View log** — scrollable SWELL log viewer with Refresh
- **Copy API key** — SWELL dialog: key field + Copy button with "Copied!" confirmation

> The Status / View log / Copy API key surfaces use SWELL dialog resources
> (`src/panel/dialogs.{h,cpp}`, `resource.h`, `dialogs.rc`), replacing the former
> plain `MessageBox` popups (Phase 4 Stage 1 / #2). Dialogs are modeless and
> appear top-centered on the main window. Verified live on REAPER 7.74 (aarch64).

### Implementation notes
- Each item is a `custom_action` (also shows in the Actions list / bindable to keys/toolbar).
- Menu structure added on `hookcustommenu` flag 0; the Start/stop check mark set on flag 1.
- Actions dispatch through **`hookcommand2`** (the correct hook for `custom_action`s) —
  not the older main-section-only `hookcommand`.
- `AddExtensionsMainMenu()` ensures the top-level "Extensions" menu exists.

### To use
The menu appears as **Extensions › ReaClaw**. Items are also in the Actions list
(search "ReaClaw:") and can be assigned to keys or toolbar buttons.

---

## Phase 4: Perception, Ergonomics & Learning (v1.3.0+)

**Goal:** the two halves of the "magic wand + hears itself" thesis — easy
high-level commands (control) and the perception/learning loop. Direction set
2026-06-20 (see `ReaClaw_IDEAS.md` → *Decisions taken*): tiered coverage,
audio analysis basic-built-in/advanced-optional, phased with check-ins. Built in
stages, smallest/lowest-risk first.

### Stage 1 — Quick wins
- [x] **#8** Log action names (id + name) in execution log + `GET /history`
  (`target_name` column) + `action_name` in `/execute/action` and sequence step
  responses. Helper `Catalog::action_name()`. Verified live (native + SWS).
- [x] **Q2/#9 (partial)** Readable structure: `folder_depth` + `color` added to
  `GET /state/tracks`. Verified live (folder parent/child/close + custom colors).
- [x] **#2** Replace plain `MessageBox` menu dialogs with polished SWELL dialogs
  (Status / API key / Log). New `panel/dialogs.{h,cpp}`, `resource.h`, `dialogs.rc`.
  Verified live (renders, copy + "Copied!" confirmation, scrollable log).

### Stage 2 — Easy commands (tiered coverage; #7/#9/#10) — **complete**
- [x] Track create/delete; writable name, color, folder_depth (superset of the
      former 5-field `POST /state/tracks/{index}`). `POST /state/tracks` (create),
      `DELETE /state/tracks/{index}`.
- [x] `add_fx` by name + param get/set; routing/sends. `POST/GET/DELETE`
      `/state/tracks/{i}/fx[/{slot}]` (params normalized, by index or name);
      `POST/DELETE /state/tracks/{i}/sends[/{send}]`; `sends[]` added to track reads.
- [x] Batch track writes (`POST /state/tracks {create,update}`); selection write
      (`POST /state/selection {tracks:[...]|"all"|"none"}`).
- [x] `GET /capabilities` manifest; `ReaClaw_TECH_DECISIONS.md` §16 coverage
      philosophy (tiered). All verified live on REAPER 7.74 (aarch64); 38/38 tests.

### Stage 3 — Agent friendliness (#10) — **complete**
- [x] ReaClaw Skill (`skill/reaclaw/SKILL.md`) — structured-verb recipes, action
      cheat-sheet, modal-action "don't" list, decision guide. Lives outside the
      extension (zero-dependency, highest ROI).
- [x] MCP wrapper (`mcp/`) — Python MCP server (FastMCP) exposing 18 typed tools
      over the REST API; importable `ReaClawClient` core (stdlib HTTP). Verified
      live: 18 tools registered; create/set/fx/send/selection round-trip.
- [x] Semantic catalog search — in the MCP layer (`search_actions`): embeds the
      ~6700-action catalog via local Ollama (`nomic-embed-text`), caches by
      catalog signature, cosine ranking; graceful keyword fallback.
- [x] **Server-side semantic search + `GET /recipes`** — the two pieces that were
      MCP-client-only or Skill-only now also exist server-side for plain
      REST/MCP-less callers: `GET /catalog/search?semantic=true` (opt-in twice
      over, loopback-only, graceful keyword fallback — `ReaClaw_TECH_DECISIONS.md`
      §25) and `GET /recipes[/{id}]` (the Skill's snippets as structured JSON).
      Catalog results also gained `mutates_state`/`requires_selection` flags.
      Verified live: semantic search against the 1411-action MIDI-editor catalog
      (real cosine scores, cache builds once then sub-second), loopback safety
      rail rejects a non-loopback `ollama_url` instantly and falls back cleanly.

### Epic #16 — Tier-A control verbs (v1.3.0) — **complete**
Extends tiered coverage with the high-value SDK gaps from
`ReaClaw_REAPER_API_ANALYSIS.md` (roadmap Epic 1). All verified live on REAPER
7.74 (aarch64); 38/38 unit tests pass.
- [x] **Undo grouping** — `with_undo()` wraps every structured mutation; lands as
      one undoable step (no-ops create no point). `GET /undo`, `POST /undo`,
      `POST /redo`. Verified: draining the undo stack rolls back a full session.
- [x] **Markers & regions** — `GET`/`POST /state/markers`, `DELETE /state/markers/{id}`.
- [x] **Tempo/time-sig map** — `GET`/`POST /state/tempo`; beat↔time via `GET /time`.
- [x] **FX presets** — `GET`/`POST /state/tracks/{i}/fx/{slot}/preset` (name/navigate).
- [x] **FX param metadata** — real-unit `raw`/`min`/`max`/`mid` in FX param reads.
- [x] **Envelope automation write** — `POST /state/tracks/{i}/automation`.
- [x] **Send extended props** — `muted`/`phase`/`mono`/`mode` (create + `POST .../sends/{send}` + reads).
- [x] **Project extras** — `GET /project` (dirty/length/notes), `POST /project/notes`.
- [x] **MIDI editor catalog** — `?section=midi_editor` (separate `actions_midi` table; 1411 live).
- [x] **Catalog `interactive` flag** — modal-dialog detection (name heuristic + curated IDs).
- [x] `GET /capabilities` + `ReaClaw_TECH_DECISIONS.md` §16 updated (graduation recorded).

### Epic #17 — Tier-B content manipulation (v1.4.0) — **complete**
Adds the write surface for the objects *inside* a track (roadmap Epic 2). All
mutations wrapped in undo blocks; verified live on REAPER 7.74 (aarch64), 19/19
live checks pass. New handler file `src/handlers/items.cpp`.
- [x] **Item CRUD** — `POST /state/items` (create + batch update),
      `POST /state/items/{index}` (update), `POST /state/items/{index}/split`,
      `DELETE /state/items/{index}`, `GET /state/items/{index}`. Create accepts a
      `file` (PCM source); update moves items across tracks
      (`MoveMediaItemToTrack`), writes position/length/fades/vol/mute/selected.
- [x] **Audio source metadata** — `source{file,type,length,length_is_beats,
      sample_rate,num_channels}` exposed on item reads (active take).
- [x] **Take properties** — `take{name,volume_db,pan,pitch,playrate,
      preserve_pitch,polarity_flipped}` readable + writable.
- [x] **Track extras** — `phase`, `n_channels`, `pan_mode`, `dual_pan_l/r`,
      `rec_input`, `midi_hw_out`, `main_send` in track reads/writes.
- [x] **FX copy** — `POST /state/tracks/{i}/fx/{slot}/copy {to_track,to_slot,move}`.
- [x] **FX online/offline** — `offline` in FX reads; accepted in FX set/add.
- [x] **Item selection write** — `POST /state/selection {items:[...]|"all"|"none"}`.
- [x] **Project ext state** — `GET/POST/DELETE /project/extstate {section,key,value}`.
- [x] `GET /capabilities`, `docs/API.md`, `CHANGELOG.md` updated.
- ~~Tier-C (take FX chains, MIDI note CRUD, multi-project) intentionally deferred —
  Lua escape hatch covers them; see `ReaClaw_TECH_DECISIONS.md` §16.~~
- **MIDI note/CC CRUD graduated to verbs (issue #51, v1.8.0)** — `GET/POST
  /state/items/{index}/midi`; notes/CC read+write, replace mode, PPQ+time duality,
  undo-wrapped. See `docs/API.md`. Take-FX and multi-project still deferred.

### Stage 4 / Epic #18 — Hear itself (Q1, Q3) (v1.5.0) — **complete**
Audio perception: the agent measures its own output and is told the consequence
of its own edits. Built-in, always-available; every measure tagged
method+confidence. New `src/handlers/analysis.cpp` (loudness/spectral/meters) and
`src/handlers/hints.cpp`. Verified live on REAPER 7.74 (aarch64), 12/12 checks
(440 Hz sine → 439.997 Hz centroid).
- [x] **Loudness** (built-in, exact) — LUFS-I/RMS-I/peak/true-peak via
      `CalculateNormalization`; clipping derived. `GET /analysis/item/{i}`,
      `GET /analysis/file?path=` with `measures`/`start`/`end`.
- [x] **Spectral balance** — low/mid/high band energy + centroid via sample
      decode + in-tree FFT (tagged `estimated_dsp`).
- [x] **Live metering** — `GET /state/meters` (per-track + master peak/hold).
- [x] **Tagging** — exact-introspection vs estimated-DSP via `method`+`confidence`.
- [x] **Consequence-aware hints** — ~12 hand-authored invariants inline on
      mutating track/FX/send/item responses (`hints[]`).
- [ ] Onset / density detection — deferred within the epic (not in acceptance
      criteria; transient analysis is higher-complexity). See TECH_DECISIONS §17.

### Stage 5 / Epic #19 — Pictures + advanced listening (Q4, Q5, Q7)
**Q4 audio visualization — done (Unreleased).** New `src/handlers/visualize.cpp`
plus dependency-free `src/util/image.{h,cpp}` (PNG encoder + RGB canvas) and a
shared header-only FFT `src/util/dsp.h` (factored out of `analysis.cpp`).
- [x] Pictures of audio — `GET /analysis/item/{i}/visualize` &
      `/analysis/file/visualize` with `type=spectrum|waveform|loudness`,
      `width`/`height`/`start`/`end`, `image=png|none`.
- [x] **Machine-readable digest** alongside every image (bands/envelope/contour),
      tagged `method`+`confidence`.
- [x] Labelled charts: spectrum = log-freq EQ **curve** (Hz/dB axes), waveform/
      loudness with seconds + dB axes, via a built-in 5×7 bitmap font on the canvas.
- [x] Unit tests: PNG round-trip, base64, font/line drawing, FFT vs naive DFT
      (12 new; 50/50).
- [x] Verified live on REAPER 7.74 (aarch64): 440 Hz sine → spectrum peak in the
      440 Hz log-band (dominant=mid, mid≈1.0), waveform peak −18.1/RMS −21.1 dB
      (sine −3 dB law holds); pink-noise demo renders flat EQ curve + arch loudness
      contour + enveloped waveform with correct axis labels; error paths 400/404.
- [x] **Q5 screenshot ergonomics** — built-in `GET /screenshot` with named surface
      targets (`arrange`/`reaper`, `mixer`, `fxchain`, `midi`, `routing`, `master`,
      `transport`, `explorer`), `window=`/`region=`, `width=` downscale, and
      screen-clamped capture rects. Verified live: framed `arrange` capture +
      graceful 404 for an unopened surface + 400 for an unknown target.
- [x] **Q7 musical probes** — `GET /analysis/item/{i}/probe` & `/analysis/file/probe`
      return pitch + key (built-in `estimated_dsp`) and tempo (exact `introspection`
      project tempo + optional external `bpm-tag`, graceful when absent), tagged by
      truth source. Pure math in header-only `util/music.h` (7 unit tests; 57/57).
      Verified live on REAPER 7.74: 440 Hz→A4 (0.95), 261.6 Hz→C4. Probe modelled as
      a flavour of the analysis surface, not a separate registry (TECH_DECISIONS §17).
- [x] **A/B diff** against an earlier snapshot — `GET /snapshot/diff/visualize`
      (issue #53, done). `POST /snapshot` gains an optional `audio:{item|file}`
      to freeze a file reference at capture time (no digest/PNG stored — decode
      is deferred to diff time, reusing `/analysis/file/visualize`'s pipeline
      via `build_file_visualization`). See `ReaClaw_TECH_DECISIONS.md` §24 for
      the design rationale and the documented source-vs-mix scope limitation.

### Stage 6 / Epic #20 — Learns over time (Q8)
**Snapshot / state-diff layer (cross-cutting prep).** New `src/handlers/snapshot.{h,cpp}`
+ header-only `src/util/jsondiff.h` (pure recursive diff, unit-tested).
- [x] Capture a canonical, diff-stable project-state snapshot; store in
      `state_snapshots`. `POST /snapshot`, `GET /snapshot[/{id}]`, `DELETE`.
- [x] `GET /snapshot/diff?from=<id>&to=<id|current>` → flat `[{path,op,from,to}]`.
      Also backs the #19 A/B visual diff (`GET /snapshot/diff/visualize`, #53, done).

**Learned suggestions (the moat).** New `src/handlers/learning.{h,cpp}` + `learn_events`
/ `learn_pairs` tables + `learning` config block.
- [x] Log structured edits as events (track create/update fields, FX add, send
      add, item create); mine antecedent→consequent transitions within a window.
- [x] `GET /suggestions` surfaces "after X, agents usually do Y" tagged
      `method:learned` + confidence (= P(consequent|antecedent), gated by
      min_support/min_confidence). `GET /learn/stats` for observability.
- [x] **Local-first and opt-in** — `learning.enabled` off by default; nothing
      recorded while disabled; all state local SQLite, no network egress ever.
- [ ] Heavier mining (multi-step sequences, value-aware "corrected to Y") —
      deferred; the pairwise association layer covers the acceptance criteria.

---

## Issue #29 — Track icons — **complete**

Adds `P_ICON` read/write to the track layer and a discovery endpoint.

- [x] **Read** — `icon` field in `GET /state/tracks` (`P_ICON` value; `null` when unset)
- [x] **Write** — `icon` accepted by `POST /state/tracks/{index}`, `POST /state/tracks`
      (create + batch update). Relative name → `Data/track_icons`; absolute path used as-is;
      `null`/`""` clears. Pass-through to `P_ICON` via `GetSetMediaTrackInfo_String`.
- [x] **Discovery** — `GET /state/track-icons` lists `.png/.jpg/.jpeg/.bmp` files in
      `{ResourcePath}/Data/track_icons`, sorted alphabetically.
- [x] **Capabilities** — `icon` added to `writable_fields`; discovery endpoint noted.
- [x] **Hint / validation** — if a relative name doesn't resolve under `Data/track_icons`,
      an `icon_not_found` warn hint is appended to `hints[]` on the mutating response.
- [x] **Docs** — `docs/API.md`, `docs/EXAMPLES.md`, `CHANGELOG.md` updated.

---

## Epic #32 — Programmatic production: headless offline render engine (Q9) — **complete**

> Closed by #36 (CI E2E render smoke test). The two unchecked items below —
> stem rendering / batch presets and the release-triggered showcase render —
> are deferred slivers, not blockers; region-bounds rendering shipped as
> `bounds: "all_regions"` on `POST /render`.

A new third half (production) beside control + perception. See
`ReaClaw_ROADMAP.md` → Epic 6 and `ReaClaw_TECH_DECISIONS.md` §19. **Offline-first,
local-first.** Proof of concept proven 2026-06-24 (`demos/`): a 7-track API-built
groove rendered offline to a 24-bit WAV in 0.36 s for 8 s of audio (~20×+), no
audio device — today via Lua `GetSetProjectInfo_String` RENDER_* + action 41824.
The epic makes it first-class.

**`/render` endpoint ([#33](https://github.com/braveness23/reaclaw/issues/33)).**
- [x] `POST /render` → offline render to file; params `{format, srate, bit_depth,
      channels, bounds, output, normalize?}`. Build/cache valid `RENDER_FORMAT`
      blobs per format so callers never see the base64.
- [x] Bounds: project / time selection / regions / custom range. Response reports
      output path + render seconds + offline-vs-realtime ratio.
- [ ] Stem + region rendering; batch/parametric presets (within the epic).

**Project save / load / open ([#34](https://github.com/braveness23/reaclaw/issues/34)). Done v1.9.0.**
- [x] `POST /project/new`, `POST /project/open {path}`, `POST /project/save {path?}`, `POST /project/reset`.

**Transport verbs ([#49](https://github.com/braveness23/reaclaw/issues/49)).**
- [x] `POST /transport {action: play|stop|pause|record}` — backed by `CSurf_On*`. **Done v1.10.0.**
- [x] `POST /transport/cursor {position}` — `SetEditCurPos`. **Done v1.10.0.**
- [x] `POST /transport/loop {start?, end?, enabled?}` — `GetSet_LoopTimeRange2` + `GetSetRepeatEx`. **Done v1.10.0.**

**Take-FX verbs ([#50](https://github.com/braveness23/reaclaw/issues/50)).**
- [x] Full `TakeFX_*` surface at `/state/items/{i}/takes/{t}/fx/...`: add, read, set, delete, copy, preset. **Done v1.10.0.**

**Change-token polling + event feed ([#31](https://github.com/braveness23/reaclaw/issues/31)). Complete.**
- [x] `GET /state/changes` → `{change_count}` via `GetProjectStateChangeCount()`. **Done v1.10.0.**
- [x] Event feed (`IReaperControlSurface` hook) — `GET /events?since=&limit=`,
      `GET /events/stream` (SSE), full attribution (`source: reaclaw|external`) via
      `Executor::EditingGuard`. Verified live: real events with correct GUIDs/kinds;
      API-driven edits tagged `reaclaw`; REAPER's own startup activity tagged
      `external`; SSE streams real-time over the existing `SSLServer`. Attribution
      confirmed best-effort (not airtight for every notification — see
      `ReaClaw_TECH_DECISIONS.md` §26 for what was actually observed). FX/marker
      changes not yet covered (`Extended()` tail) — a documented v1 boundary.

**Async render-job model ([#35](https://github.com/braveness23/reaclaw/issues/35)). Done — see Phase 2 below.**
- [x] `async: true` flag on `POST /execute/action` — schedules via SWELL `SetTimer`. **Done v1.10.0.**
- [x] `async: true` flag on `POST /render` returns `{job_id, status}`;
      `GET /render/jobs/{id}` polls status/output; `GET /render/jobs` lists;
      `DELETE /render/jobs/{id}` cancels a not-yet-started job. Does **not**
      solve main-thread starvation during an active render — confirmed live
      that REAPER pumps no message loop during an offline render, documented
      as an honest v1 limitation. See `ReaClaw_TECH_DECISIONS.md` §23.

**CI / pipeline integration ([#36](https://github.com/braveness23/reaclaw/issues/36)). Done.**
- [x] E2E smoke test: `demos/scripts/ci_smoke_test.py` builds a tiny composition →
      offline render → asserts non-silent via `GET /analysis/file` (no ffmpeg
      dependency). Wired into `.github/workflows/ci.yml` as `e2e-render-smoke`,
      running on every PR against a throwaway, unlicensed REAPER install under
      Xvfb + dummy JACK on the self-hosted runner. Confirmed REAPER's evaluation
      mode renders correctly with no blocking license/nag dialog — no license
      file needed in CI. Closes Epic #32.
- [ ] (stretch, not done) release-triggered showcase render / trailer as a build
      artifact.

---

## Friction-test fixes (2026-06-30 dogfooding session, issues #63-#80) — Phase 1 batch

Most items came pre-fixed or already covered by existing code (verified live
before writing anything new) — see `ReaClaw_ROADMAP.md` for full triage.
Code-changing items landed:

- [x] **#66** ReaEQ ghost FX — `is_inline_eq`/`agent_slot` flags on track `fx[]`
- [x] **#67** `GET /transport` — live position, bypasses the 1s state cache
- [x] **#69** `POST /execute/script` — one-shot register+run(+deregister)
- [x] **#70** `POST /state/tempo` — `time` optional (defaults 0.0), `time_signature` string sugar
- [x] **#71** `POST /transport/play|stop|pause|record` — agent-friendly aliases
- [x] **#72** `context.schema` on 5 named 400 responses
- [x] **#73** `docs/SCRIPTING.md` — Lua cheat sheet
- [x] **#74** FX param pagination/search (`?limit=&offset=&q=`)
- [x] **#75** MIDI readback pagination (`?limit=&offset=`)
- [x] **#64** `POST /queue/flush` — drains pending backlog (short-term health/timeout fix already shipped v1.11.2)
- [x] **#79** `instrument` field on `POST /state/tracks` create specs (bulk creation already existed via `create: [...]`)
- [x] **#80** confirmed already fixed (`Main_openProject`, not `40023`) — added doc warning only
- [x] **#68** confirmed already fixed (startup script reconciliation + `GET /scripts/cache` both predate this batch) — verify-only
- [x] **#76** confirmed already fixed (track color read/write both predate this batch) — verify-only

Phase 2 (real architecture/risk decisions, done one at a time with a check-in):

- [x] **#77** `POST /reaper/restart` — full main-thread-wedge recovery via
      `/proc/self/{cmdline,environ}` argv/environment replay (no `Executor::post`
      dependency, no REAPER SDK calls on the critical path). Linux only. See
      `ReaClaw_TECH_DECISIONS.md` §22.
- [x] **#35** `async: true` on `POST /render` + `GET/DELETE /render/jobs/{id}`,
      `GET /render/jobs`. Reuses the existing `Executor::post` path from a
      detached worker thread (no new SetTimer trigger needed); single-flight
      comes free from Executor's FIFO drain. See `ReaClaw_TECH_DECISIONS.md` §23.
- [x] **#36** CI E2E headless render smoke test — `e2e-render-smoke` job +
      `demos/scripts/ci_smoke_test.py`. Closes Epic #32.
- [x] **#53** `GET /snapshot/diff/visualize` — A/B visual diff. See
      `ReaClaw_TECH_DECISIONS.md` §24.
- [x] **#10** Server-side semantic catalog search + `GET /recipes` — the two
      remaining slivers of the magic-wand issue. See `ReaClaw_TECH_DECISIONS.md` §25.
- [x] **#84** Windows release binary (`.dll`), cross-compiled via CI. See
      `ReaClaw_TECH_DECISIONS.md`'s Windows ABI note (§2).
- [x] **#31** External-change event feed + SSE + attribution. See
      `ReaClaw_TECH_DECISIONS.md` §26.

---

## Epic #45 — Full coverage: provable reachability + legible map — **complete**

All six planned sub-issues shipped (#46 coverage matrix, #37 governance policy, #48
state-chunk keystone, #49 transport verbs, #50 take-FX verbs, #51 MIDI verbs) plus
project lifecycle (#34). Config vars (#44) deliberately deferred to the wish list —
still reachable via action/Lua tiers, so the 100%-reachability goal holds without it.
`ReaClaw_COVERAGE_REPORT.md` and the live `/capabilities` `sdk` object updated to
current measured figures (868 total / 188 called / 21.7%, up from 865/131/15.1% at
the report's original measurement). Epic closed 2026-07-02.

Still deferred: Pi plugin curation (#62) — manual devops + subjective creative
work, deliberately left for the user, not this marathon.

---

## Ongoing (All Phases)

- [x] Keep unit and integration tests passing before each commit — 38/38 unit tests pass; verified live against REAPER 7.74 (aarch64)
- [x] Update `docs/API.md` as endpoints are added or changed
- [x] Add `CHANGELOG.md` entry for each tagged release
- [x] Keep `vendor/` library versions pinned and documented in `CMakeLists.txt` comments

---

## Success Criteria

| Phase | Key criterion |
|---|---|
| 0 | Catalog indexed; action executes in REAPER via HTTPS; auth works |
| 1 | Agent-generated Lua runs in REAPER; cached and callable by ID; sequences with feedback work |
| 2 | MCP integration working; 10 concurrent agents handled; production-hardened |

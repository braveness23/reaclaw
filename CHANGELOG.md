# Changelog

All notable changes to ReaClaw are documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

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

### Changed
- `GET /state/tracks` track objects now include a `sends` array.

### Docs
- Added `ReaClaw_IDEAS.md` — perception/learning/discovery idea queue and the
  direction decisions taken for the Phase 4 build-out.
- `ReaClaw_TECH_DECISIONS.md` §16 — API coverage is **tiered** (structured verbs +
  action-runner + Lua escape hatch); resolves the open question in #7.

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

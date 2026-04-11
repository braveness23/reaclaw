# Changelog

All notable changes to ReaClaw are documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

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

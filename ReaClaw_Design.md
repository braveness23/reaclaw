# ReaClaw: Full Design Specification

---

## 1. Executive Summary

ReaClaw is a native C++ REAPER extension that embeds an HTTPS server inside REAPER's process. It gives any HTTP-capable AI agent full programmatic control over REAPER: browsing and executing actions, querying project state, and registering agent-generated Lua ReaScripts as custom actions.

**Design principle:** ReaClaw interfaces with REAPER. The agent reasons, generates, and decides. ReaClaw does not call LLMs, does not manage workflows on the agent's behalf, and does not second-guess the agent's generated code beyond a syntax check.

**Key properties:**
- Native extension — runs in REAPER's process; direct access to all REAPER API functions
- Agent-agnostic — any HTTP client works; no vendor coupling
- Self-contained — all dependencies bundled or header-only; no external services required
- Cross-platform — Windows, macOS, Linux; single codebase

---

## 2. Architecture

### 2.1 Component Overview

```
┌──────────────────────────────────────────────────────────────────┐
│  AI Agents (Claude, Sparky, custom, curl)                        │
└────────────────────────┬─────────────────────────────────────────┘
                         │  HTTPS / REST+JSON
                         ▼
┌──────────────────────────────────────────────────────────────────┐
│  reaper_reaclaw  (.dll / .dylib / .so)  — loaded by REAPER       │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  HTTPS Server Thread Pool  (cpp-httplib + OpenSSL)       │   │
│  │  ┌────────────────────────────────────────────────────┐  │   │
│  │  │  Router + Auth middleware                          │  │   │
│  │  │  /catalog  /state  /execute  /scripts  /history    │  │   │
│  │  └────────────────────────────────────────────────────┘  │   │
│  └─────────────────────────┬────────────────────────────────┘   │
│                             │                                    │
│              ┌──────────────▼──────────────┐                    │
│              │  Command Queue              │                    │
│              │  (std::queue + std::mutex)  │  ◀── web threads   │
│              │  (std::promise/future)      │  ──▶ main thread   │
│              └──────────────┬──────────────┘                    │
│                             │ timer callback (~30fps)           │
│  ┌──────────────────────────▼───────────────────────────────┐   │
│  │  REAPER SDK  (via GetFunc / reaper_plugin_functions.h)   │   │
│  │                                                          │   │
│  │  kbd_enumerateActions   Main_OnCommand   GetTrack        │   │
│  │  AddRemoveReaScript     GetProjectTimeSignature2  ...    │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                  │
│  ┌────────────────────┐   ┌─────────────────────────────────┐   │
│  │  SQLite            │   │  Config                         │   │
│  │  reaclawdb.sqlite  │   │  {ResourcePath}/reaclaw/        │   │
│  └────────────────────┘   │    config.json                  │   │
│                            │    certs/                       │   │
│                            └─────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────┘
```

### 2.2 Startup Sequence

When REAPER loads the extension (`ReaperPluginEntry` is called):

1. Load and validate `config.json`; write defaults if missing
2. Open (or create) the SQLite database; run migrations
3. Bind all needed REAPER API function pointers via `GetFunc`
4. Register a main-thread timer callback via `plugin_register("timer", ...)`
5. Generate a self-signed TLS cert if none exists and `tls.generate_if_missing` is true
6. Build the action catalog index in SQLite (enumerate via `kbd_enumerateActions`)
7. Start the `cpp-httplib` SSL server on the configured port in a background thread
8. Log `"ReaClaw listening on https://0.0.0.0:{port}"` to the REAPER console

On REAPER shutdown (`ReaperPluginEntry` called with `rec == NULL`):

1. Signal the server thread to stop; join it
2. Flush SQLite writes
3. Unregister timer callback

### 2.3 Threading Model

The REAPER SDK distinguishes threadsafe from non-threadsafe functions. The web server runs on background threads; REAPER's main thread drives the timer callback.

**Background threads (cpp-httplib thread pool):**
- Parse requests and run route handlers
- Call threadsafe REAPER functions directly (e.g., `CountTracks`, `GetTrack`, `GetTrackName`, `GetProjectTimeSignature2`, `GetPlayState`)
- For non-threadsafe calls (primarily `Main_OnCommand`), post a `Command` to the queue and block on its `std::future<Result>` with a configurable timeout (default 5s)

**Main thread (timer callback at ~30fps):**
- Drain the command queue
- Execute each command using the REAPER API
- Set the `std::promise<Result>` to unblock the waiting handler thread

**Command queue structure:**
```cpp
struct Command {
    std::function<void()>        execute;   // Called on main thread
    std::promise<nlohmann::json> result;
};

std::queue<Command> command_queue;
std::mutex          queue_mutex;
```

---

## 3. REAPER SDK Integration

### 3.1 Plugin Entry Point

```cpp
extern "C" REAPER_PLUGIN_DLL_EXPORT int ReaperPluginEntry(
    HINSTANCE hInstance,
    reaper_plugin_info_t *rec)
{
    if (!rec) {
        ReaClaw::shutdown();
        return 0;
    }
    if (rec->caller_version != REAPER_PLUGIN_VERSION) return 0;
    if (!REAPERAPI_LoadAPI(rec->GetFunc)) return 0;

    ReaClaw::init(rec);
    return 1;
}
```

`REAPERAPI_LoadAPI` is provided by `reaper_plugin_functions.h` when compiled with `#define REAPERAPI_IMPLEMENT`. It populates global function pointers for all REAPER API functions.

### 3.2 Action Catalog Enumeration

```cpp
KbdSectionInfo *section = SectionFromUniqueID(0); // Main section

int idx = 0;
const char *name = nullptr;
int cmd_id = 0;

while ((cmd_id = kbd_enumerateActions(section, idx, &name)) != 0) {
    // Store {cmd_id, name} in SQLite actions table
    idx++;
}
```

Called once at startup. The catalog is rebuilt if the stored REAPER version differs from `GetAppVersion()`.

### 3.3 Action Execution

`Main_OnCommand` must be called from the main thread. Handlers post to the command queue:

```cpp
// From web server handler thread:
Command cmd;
auto future = cmd.result.get_future();
cmd.execute = [cmd_id]() { Main_OnCommand(cmd_id, 0); };

{
    std::lock_guard<std::mutex> lock(queue_mutex);
    command_queue.push(std::move(cmd));
}

if (future.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
    // Return HTTP 408
}
```

### 3.4 ReaScript Registration

```cpp
// Write the agent-generated script to disk, then register it:
int new_cmd_id = AddRemoveReaScript(
    true,        // isAdd
    0,           // sectionID (Main)
    script_path, // absolute path to .lua file
    true         // commit — persist to reaper-kb.ini
);
// Returns command ID, or 0 on failure
```

To unregister:
```cpp
AddRemoveReaScript(false, 0, script_path, true);
```

Scripts are written to `{GetResourcePath()}/reaclaw/scripts/{id}.lua`.

### 3.5 State Queries (threadsafe — called directly from handler threads)

```cpp
// BPM and time signature
double bpm, beat;
GetProjectTimeSignature2(nullptr, &bpm, &beat);

// Transport
int play_state = GetPlayState(); // 0=stopped, 1=playing, 2=paused, 4=recording

// Cursor
double cursor_pos = GetCursorPosition();

// Tracks
int num_tracks = CountTracks(nullptr);
for (int i = 0; i < num_tracks; i++) {
    MediaTrack *track = GetTrack(nullptr, i);
    char name[256] = {};
    GetTrackName(track, name, sizeof(name));
    bool muted = *(bool*)GetSetMediaTrackInfo(track, "B_MUTE", nullptr);
}
```

---

## 4. API Specification

All endpoints:
- Accept and return `application/json`
- Require `Authorization: Bearer {key}` header when `auth.type` is `api_key`
- Return standard error shape on failure:
  ```json
  { "error": "description", "code": "ERROR_CODE", "context": {} }
  ```

HTTP status codes: `200 OK`, `400 Bad Request`, `401 Unauthorized`, `404 Not Found`, `408 Request Timeout`, `500 Internal Server Error`

---

### 4.1 System

**GET `/health`**

```json
{
  "status": "ok",
  "version": "0.1.0",
  "reaper_version": "7.12",
  "catalog_size": 65234,
  "uptime_seconds": 3600
}
```

---

### 4.2 Action Catalog

**GET `/catalog`**

Returns full action catalog. Supports pagination.

Query params: `limit` (default 100), `offset` (default 0), `section` (default `"main"`)

```json
{
  "total": 65234,
  "offset": 0,
  "limit": 100,
  "actions": [
    {
      "id": 40285,
      "name": "Track: Toggle mute for selected tracks",
      "category": "Track",
      "section": "main"
    }
  ]
}
```

**GET `/catalog/search?q=query`**

Full-text search across action names and categories. SQLite FTS5.

Query params: `q` (required), `category` (optional filter), `limit` (default 20)

```json
{
  "query": "mute",
  "total": 12,
  "actions": [...]
}
```

**GET `/catalog/categories`**

```json
{
  "categories": [
    { "name": "Track", "count": 4200 },
    { "name": "Item", "count": 3100 }
  ]
}
```

---

### 4.3 State Queries

**GET `/state`**

```json
{
  "project": {
    "name": "MySession.rpp",
    "path": "/Users/dave/Documents/REAPER Projects/MySession.rpp",
    "bpm": 120.0,
    "time_signature": "4/4",
    "cursor_position": 15.523
  },
  "transport": {
    "playing": false,
    "recording": false,
    "paused": false,
    "loop_enabled": true,
    "loop_start": 0.0,
    "loop_end": 8.0
  },
  "track_count": 12
}
```

**GET `/state/tracks`**

```json
{
  "tracks": [
    {
      "index": 0,
      "guid": "{A1B2C3...}",
      "name": "Kick",
      "muted": false,
      "soloed": false,
      "armed": true,
      "volume_db": 0.0,
      "pan": 0.0,
      "fx": [
        { "slot": 0, "name": "ReaComp (Cockos)", "enabled": true }
      ],
      "send_count": 1
    }
  ]
}
```

**GET `/state/items`**

Media items in the current project. Includes position, length, track index, and take name.

**GET `/state/selection`**

Currently selected tracks and items.

**GET `/state/automation`**

Automation envelopes for the selected track. Includes parameter name, mode, and envelope points.

---

### 4.4 Action Execution

**POST `/execute/action`**

Execute a single action by numeric ID or registered script action ID string.

Request:
```json
{
  "id": 40285,
  "feedback": true
}
```

- `id`: Integer (built-in) or string (registered script, e.g. `"_parallel_comp_a1b2c3"`)
- `feedback`: If true, capture and return current state after execution

Response:
```json
{
  "status": "success",
  "action_id": 40285,
  "executed_at": "2026-04-07T14:24:10Z",
  "feedback": {
    "tracks": [...],
    "transport": {...}
  }
}
```

**POST `/execute/sequence`**

Execute multiple actions in order. Returns per-step results. The agent inspects each step's feedback to decide whether to continue.

Request:
```json
{
  "steps": [
    { "id": 40285, "label": "mute kick" },
    { "id": "_setup_sidechain_abc", "label": "setup sidechain" },
    { "id": 1013, "label": "record" }
  ],
  "feedback_between_steps": true,
  "stop_on_failure": true
}
```

Response:
```json
{
  "status": "success",
  "steps_completed": 3,
  "steps": [
    {
      "label": "mute kick",
      "action_id": 40285,
      "status": "success",
      "feedback": { "tracks": [...] }
    }
  ]
}
```

---

### 4.5 Script Management

**POST `/scripts/register`**

The agent provides a Lua script it has generated. ReaClaw validates the syntax, writes it to disk, and registers it as a custom REAPER action via `AddRemoveReaScript`.

Request:
```json
{
  "name": "parallel_comp_drums",
  "script": "local tr = reaper.GetSelectedTrack(0,0)\n...",
  "tags": ["compression", "parallel", "drums"]
}
```

Response on success:
```json
{
  "action_id": "_parallel_comp_drums_a1b2c3",
  "registered": true,
  "script_path": "/Users/dave/.../reaclaw/scripts/_parallel_comp_drums_a1b2c3.lua"
}
```

Response on syntax error:
```json
{
  "registered": false,
  "syntax_error": {
    "line": 7,
    "message": "'end' expected (to close 'function') near '<eof>'"
  }
}
```

Idempotent: if a script with the same name is already registered, return the existing `action_id`.

**GET `/scripts/cache`**

```json
{
  "scripts": [
    {
      "action_id": "_parallel_comp_drums_a1b2c3",
      "name": "parallel_comp_drums",
      "tags": ["compression", "parallel", "drums"],
      "created_at": "2026-04-07T10:00:00Z",
      "execution_count": 5,
      "last_executed": "2026-04-07T14:24:10Z"
    }
  ]
}
```

**GET `/scripts/{action_id}`**

Returns the full Lua source and metadata for the given action ID.

**DELETE `/scripts/{action_id}`**

Unregisters the script from REAPER via `AddRemoveReaScript(false, ...)`, deletes the `.lua` file, and removes the entry from SQLite.

---

### 4.6 History

**GET `/history`**

Query params: `limit` (default 50), `offset`, `agent_id`

```json
{
  "executions": [
    {
      "id": 1042,
      "type": "action",
      "target_id": "40285",
      "agent_id": "claude-sonnet-4-6",
      "status": "success",
      "executed_at": "2026-04-07T14:24:10Z"
    }
  ]
}
```

---

## 5. Data Models (SQLite Schema)

```sql
-- Action catalog (indexed from REAPER at startup)
CREATE TABLE actions (
    id         INTEGER PRIMARY KEY,
    name       TEXT NOT NULL,
    category   TEXT,
    section    TEXT DEFAULT 'main',
    created_at TEXT DEFAULT (datetime('now'))
);
CREATE VIRTUAL TABLE actions_fts USING fts5(name, category, content=actions);

-- Agent-registered Lua ReaScripts
CREATE TABLE scripts (
    id              TEXT PRIMARY KEY,    -- e.g. "_parallel_comp_a1b2c3"
    name            TEXT NOT NULL,
    body            TEXT NOT NULL,
    script_path     TEXT NOT NULL,
    tags            TEXT,                -- JSON array
    execution_count INTEGER DEFAULT 0,
    created_at      TEXT DEFAULT (datetime('now')),
    last_executed   TEXT
);

-- Execution audit log
CREATE TABLE execution_history (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    type        TEXT NOT NULL,           -- 'action', 'script', 'sequence'
    target_id   TEXT NOT NULL,           -- action/script ID
    agent_id    TEXT,                    -- from X-Agent-Id header
    status      TEXT NOT NULL,           -- 'success', 'failed', 'timeout'
    error       TEXT,
    executed_at TEXT DEFAULT (datetime('now'))
);
```

---

## 6. Security

### Authentication

Two modes, set in `config.json`:

**`none`** — No auth. Use only on localhost or a fully trusted network.

**`api_key`** — All requests must include `Authorization: Bearer {key}`. Key is set in config. Recommended for any network-accessible deployment.

### TLS

ReaClaw always runs HTTPS. Two certificate modes:

**Self-signed (development / home network):**
```json
"tls": {
  "enabled": true,
  "generate_if_missing": true
}
```
On first run, ReaClaw generates a 4096-bit RSA key and self-signed cert, saved to `{ResourcePath}/reaclaw/certs/`. Agents skip certificate verification for self-signed (`-k` with curl, or equivalent in SDK).

**CA-signed:**
```json
"tls": {
  "enabled": true,
  "cert_file": "/etc/letsencrypt/live/example.com/fullchain.pem",
  "key_file":  "/etc/letsencrypt/live/example.com/privkey.pem"
}
```

### Script Validation

Before registering any agent-submitted script, ReaClaw runs a syntax check:

```bash
luac -p {script_file}
```

If `luac` is not available on PATH, fall back to a lightweight bracket/keyword check. On failure, return the error with line number — do not register. On success, register immediately.

No static analysis, no approval gate. The agent is trusted; syntax validation exists only to catch generation errors before they reach REAPER.

---

## 7. Configuration Reference

Config file: `{GetResourcePath()}/reaclaw/config.json`

If missing, ReaClaw writes defaults on startup.

```json
{
  "server": {
    "host": "0.0.0.0",
    "port": 9091,
    "thread_pool_size": 4
  },
  "tls": {
    "enabled": true,
    "generate_if_missing": true,
    "cert_file": "",
    "key_file": ""
  },
  "auth": {
    "type": "api_key",
    "key": "sk_change_me"
  },
  "database": {
    "path": ""
  },
  "script_security": {
    "validate_syntax": true,
    "log_all_executions": true,
    "max_script_size_kb": 512
  },
  "logging": {
    "level": "info",
    "file": ""
  }
}
```

Notes:
- `database.path`: Defaults to `{ResourcePath}/reaclaw/reaclawdb.sqlite`
- `auth.type`: `"none"` or `"api_key"`
- `logging.file`: If empty, logs to REAPER console via `ShowConsoleMsg`

---

## 8. Build System

### Dependencies

All bundled in `vendor/` except OpenSSL (system or vcpkg):

| Library | Version | How |
|---|---|---|
| cpp-httplib | latest | Single header `httplib.h` |
| nlohmann/json | 3.x | Single header `json.hpp` |
| SQLite | 3.x | Amalgamation `sqlite3.c` + `sqlite3.h` |
| REAPER SDK | current | Headers from justinfrankel/reaper-sdk |
| OpenSSL | 3.x | System package or vcpkg |

### CMake Structure

```cmake
cmake_minimum_required(VERSION 3.20)
project(reaper_reaclaw VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)

add_library(reaper_reaclaw SHARED
    src/main.cpp
    src/server/server.cpp
    src/server/router.cpp
    src/handlers/catalog.cpp
    src/handlers/state.cpp
    src/handlers/execute.cpp
    src/handlers/scripts.cpp
    src/handlers/history.cpp
    src/reaper/api.cpp
    src/reaper/catalog.cpp
    src/reaper/executor.cpp
    src/reaper/scripts.cpp
    src/db/db.cpp
    src/auth/auth.cpp
    src/config/config.cpp
    src/util/tls.cpp
    src/util/logging.cpp
    vendor/sqlite3.c
)

target_include_directories(reaper_reaclaw PRIVATE
    vendor/
    vendor/reaper-sdk/
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(reaper_reaclaw PRIVATE OpenSSL::SSL OpenSSL::Crypto)

set_target_properties(reaper_reaclaw PROPERTIES
    PREFIX ""
    OUTPUT_NAME "reaper_reaclaw"
)

if(DEFINED REAPER_USER_PLUGINS)
    install(TARGETS reaper_reaclaw DESTINATION "${REAPER_USER_PLUGINS}")
endif()
```

Build:
```bash
cmake -B build -DREAPER_USER_PLUGINS="/path/to/UserPlugins"
cmake --build build --config Release
cmake --install build
```

On Windows, must use MSVC (required for REAPER C++ ABI compatibility).

---

## 9. Implementation Phases Summary

See `ReaClaw_IMPLEMENTATION_CHECKLIST.md` for full task breakdown.

**Phase 0 — Foundation:** Extension scaffold; REAPER API binding; action catalog; state queries; single action execution; HTTPS + auth; SQLite

**Phase 1 — Scripts & Sequences:** Script registration (`AddRemoveReaScript`); Lua syntax validation; script cache; multi-step sequences with per-step feedback; execution history

**Phase 2 — Integration & Hardening:** Optional MCP wrapper for OpenClaw/Sparky; agent identification; performance profiling; security audit

---

## 10. Example Agent Flows

### Simple action execution
```
Agent: "Mute the selected track"
  → GET /catalog/search?q=mute+selected+track
  → Returns: id=40285, "Track: Toggle mute for selected tracks"
  → POST /execute/action { "id": 40285, "feedback": true }
  → Returns: { "status": "success", "feedback": { "tracks": [{"muted": true, ...}] } }
```

### Agent generates and registers a script
```
Agent generates Lua for parallel compression (using its own LLM capabilities)
  → GET /scripts/cache?tags=parallel  →  empty, not cached
  → POST /scripts/register {
      "name": "parallel_comp_drums",
      "script": "local tr = reaper.GetSelectedTrack(0,0)\n...",
      "tags": ["compression", "parallel"]
    }
  → Returns: { "action_id": "_parallel_comp_drums_a1b2c3", "registered": true }
  → POST /execute/action { "id": "_parallel_comp_drums_a1b2c3", "feedback": true }

Next time:
  → GET /scripts/cache?tags=parallel  →  found: _parallel_comp_drums_a1b2c3
  → POST /execute/action { "id": "_parallel_comp_drums_a1b2c3" }
  → No regeneration needed.
```

### Multi-step sequence
```
Agent: "Record drums with sidechain from kick"
  → POST /execute/sequence {
      "steps": [
        { "id": 40280, "label": "select kick" },
        { "id": "_setup_sidechain_xyz", "label": "setup sidechain" },
        { "id": 1013, "label": "record" }
      ],
      "feedback_between_steps": true,
      "stop_on_failure": true
    }
  → Step 1 feedback: tracks[0].name = "Kick" ✓
  → Step 2 feedback: tracks[3].fx[1].name = "ReaComp" ✓
  → Step 3: recording started
```

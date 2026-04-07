# ReaClaw: Full Design Specification

---

## 1. Executive Summary

ReaClaw is a native C++ REAPER extension that embeds an HTTPS server inside REAPER's process. It gives any HTTP-capable AI agent full programmatic control over REAPER: browsing and executing actions, querying project state, generating and registering custom ReaScripts, and building reusable multi-step workflows with feedback and verification.

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
│  │  │  /catalog  /state  /execute  /scripts  /workflows  │  │   │
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
8. Log "ReaClaw listening on https://0.0.0.0:{port}" to the REAPER console

On REAPER shutdown (`ReaperPluginEntry` called with `rec == NULL`):

1. Signal the server thread to stop; join it
2. Flush SQLite writes
3. Unregister timer callback

### 2.3 Threading Model

The REAPER SDK distinguishes threadsafe from non-threadsafe functions. The web server runs on background threads; REAPER's main thread drives the timer callback.

**Background threads (cpp-httplib thread pool):**
- Parse requests and run route handlers
- May call threadsafe REAPER functions directly (e.g., `CountTracks`, `GetTrack`, `GetTrackName`, `GetProjectTimeSignature2`, `GetPlayState`)
- For non-threadsafe calls (primarily action execution via `Main_OnCommand`), post a `Command` to the queue and block on its `std::future<Result>` with a configurable timeout (default 5s)

**Main thread (timer callback at ~30fps):**
- Drain the command queue
- Execute each command using the REAPER API
- Set the `std::promise<Result>` to unblock the waiting handler thread

**Command queue structure:**
```cpp
struct Command {
    std::function<void()>   execute;    // Called on main thread
    std::promise<nlohmann::json> result;
};

std::queue<Command>  command_queue;
std::mutex           queue_mutex;
```

---

## 3. REAPER SDK Integration

### 3.1 Plugin Entry Point

```cpp
// Entry point exported by the extension DLL/dylib/so
extern "C" REAPER_PLUGIN_DLL_EXPORT int ReaperPluginEntry(
    HINSTANCE hInstance,
    reaper_plugin_info_t *rec)
{
    if (!rec) {
        // Cleanup on unload
        ReaClaw::shutdown();
        return 0;
    }
    if (rec->caller_version != REAPER_PLUGIN_VERSION) return 0;

    // Load all API function pointers
    if (!REAPERAPI_LoadAPI(rec->GetFunc)) return 0;

    ReaClaw::init(rec);
    return 1;
}
```

`REAPERAPI_LoadAPI` is provided by `reaper_plugin_functions.h` when compiled with `#define REAPERAPI_IMPLEMENT`. It populates global function pointers for all REAPER API functions.

### 3.2 Action Catalog Enumeration

REAPER organizes actions into "sections" (Main, MIDI Editor, Media Explorer, etc.). The main section (section ID 0) contains the primary 65K+ actions.

```cpp
// Get the main keyboard section
KbdSectionInfo *section = SectionFromUniqueID(0);

int idx = 0;
const char *name = nullptr;
int cmd_id = 0;

while ((cmd_id = kbd_enumerateActions(section, idx, &name)) != 0) {
    // Store {cmd_id, name} in catalog
    idx++;
}
```

This is called once at startup; results are written to the `actions` SQLite table. The catalog is rebuilt if the REAPER version changes (detected by comparing stored version to `GetAppVersion()`).

### 3.3 Action Execution

Action execution must happen on the main thread. The handler posts to the command queue:

```cpp
// From web server handler thread:
Command cmd;
cmd.execute = [cmd_id]() {
    Main_OnCommand(cmd_id, 0);
};
auto future = cmd.result.get_future();

{
    std::lock_guard<std::mutex> lock(queue_mutex);
    command_queue.push(std::move(cmd));
}

// Wait for main thread to execute
auto status = future.wait_for(std::chrono::seconds(5));
if (status == std::future_status::timeout) {
    // Return 504 timeout error
}
```

### 3.4 ReaScript Registration

```cpp
// Register a Lua script file as a custom REAPER action
// sectionID 0 = Main section
// Returns the assigned command ID, or 0 on failure
int new_cmd_id = AddRemoveReaScript(
    true,         // isAdd
    0,            // sectionID (Main)
    script_path,  // absolute path to .lua file on disk
    true          // commit (true = persist to reaper-kb.ini)
);
```

To remove:
```cpp
AddRemoveReaScript(false, 0, script_path, true);
```

Scripts are written to `{GetResourcePath()}/reaclaw/scripts/{id}.lua` before registration.

### 3.5 State Queries (threadsafe — called directly from handler threads)

```cpp
// Project overview
double bpm;
double beat;
GetProjectTimeSignature2(nullptr, &bpm, &beat);

int play_state = GetPlayState(); // 0=stopped, 1=playing, 2=paused, 4=recording

double cursor_pos = GetCursorPosition();

// Track enumeration
int num_tracks = CountTracks(nullptr); // nullptr = current project
for (int i = 0; i < num_tracks; i++) {
    MediaTrack *track = GetTrack(nullptr, i);
    char name[256] = {};
    GetTrackName(track, name, sizeof(name));
    bool muted = *(bool*)GetSetMediaTrackInfo(track, "B_MUTE", nullptr);
    // ... etc.
}
```

---

## 4. API Specification

All endpoints:
- Accept and return `application/json`
- Require `Authorization: Bearer {key}` header if auth type is `api_key`
- Return standard error shape on failure:
  ```json
  { "error": "description", "code": "ERROR_CODE", "context": {} }
  ```

HTTP status codes: `200 OK`, `400 Bad Request`, `401 Unauthorized`, `404 Not Found`, `408 Request Timeout`, `429 Too Many Requests`, `500 Internal Server Error`

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

Query params: `limit` (default 100), `offset` (default 0), `section` (default "main")

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
      "tags": ["track", "mute"],
      "section": "main"
    }
  ]
}
```

**GET `/catalog/search?q=query`**

Full-text search across name, category, and tags. SQLite FTS5 index.

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

High-level project summary.

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
  "selection": {
    "track_count": 1,
    "item_count": 0
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
      "fx_count": 2,
      "fx": [
        { "slot": 0, "name": "ReaComp (Cockos)", "enabled": true }
      ],
      "send_count": 1,
      "receives_count": 0
    }
  ]
}
```

**GET `/state/items`**

Media items in the current project.

**GET `/state/selection`**

Currently selected tracks, items, and take.

**GET `/state/automation`**

Automation envelopes for selected track. Includes points, mode, parameter.

---

### 4.4 Action Execution

**POST `/execute/action`**

Execute a single action by numeric ID or custom script action ID string.

Request:
```json
{
  "id": 40285,
  "feedback": true
}
```

- `id`: Integer (built-in action) or string (custom action, e.g. `"_parallel_comp_abc123"`)
- `feedback`: If true, include state snapshot after execution

Response:
```json
{
  "status": "success",
  "action_id": 40285,
  "executed_at": "2026-04-07T14:24:10Z",
  "feedback": {
    "state_after": { ... }
  }
}
```

**POST `/execute/sequence`**

Execute multiple actions in order. Each step can include a feedback check between steps.

Request:
```json
{
  "steps": [
    { "id": 40285, "label": "mute kick" },
    { "id": 40286, "label": "mute snare" }
  ],
  "feedback_between_steps": true,
  "stop_on_failure": true
}
```

Response:
```json
{
  "status": "success",
  "steps_completed": 2,
  "steps": [
    {
      "label": "mute kick",
      "action_id": 40285,
      "status": "success",
      "feedback": { "state_after": {...} }
    }
  ]
}
```

---

### 4.5 Script Management

**POST `/scripts/generate`**

Calls the configured LLM to generate a ReaScript.

Request:
```json
{
  "intent": "Create parallel compression routing on the selected track",
  "context": {
    "selected_track_index": 0,
    "track_name": "Drums",
    "available_fx": ["ReaComp", "ReaEQ", "ReaGate"]
  },
  "language": "lua"
}
```

Response:
```json
{
  "script": "local tr = reaper.GetSelectedTrack(0,0)\n...",
  "language": "lua",
  "preview": "Inserts aux track, routes signal parallel, adds ReaComp at 4:1 ratio",
  "warnings": [],
  "syntax_valid": true
}
```

**POST `/scripts/validate`**

Validate a script without registering it.

Request: `{ "script": "...", "language": "lua" }`

Response:
```json
{
  "syntax_valid": true,
  "warnings": [
    { "line": 14, "message": "os.execute call detected — potential shell execution" }
  ]
}
```

**POST `/scripts/register`**

Write a script to disk and register it as a custom REAPER action via `AddRemoveReaScript`.

Request:
```json
{
  "name": "parallel_comp_drums",
  "script": "local tr = ...",
  "language": "lua",
  "tags": ["compression", "parallel", "drums"]
}
```

Response:
```json
{
  "action_id": "_parallel_comp_drums_a1b2c3",
  "registered": true,
  "script_path": "/Users/dave/Library/.../reaclaw/scripts/_parallel_comp_drums_a1b2c3.lua",
  "callable_immediately": true
}
```

**GET `/scripts/cache`**

```json
{
  "scripts": [
    {
      "action_id": "_parallel_comp_drums_a1b2c3",
      "name": "parallel_comp_drums",
      "language": "lua",
      "tags": ["compression", "parallel"],
      "created_at": "2026-04-07T10:00:00Z",
      "execution_count": 5
    }
  ]
}
```

**GET `/scripts/{action_id}`**

Returns the script source and metadata for the given action ID.

---

### 4.6 Workflows

**POST `/workflows`**

Save a named multi-step workflow to SQLite.

Request:
```json
{
  "name": "drum_sidechain_record",
  "description": "Route kick to sidechain, mute drums except hihat, arm, record",
  "tags": ["drums", "recording", "sidechain"],
  "steps": [
    {
      "id": "step_1",
      "type": "action",
      "action_id": 40280,
      "label": "Select kick track"
    },
    {
      "id": "step_2",
      "type": "script",
      "action_id": "_setup_sidechain_abc",
      "label": "Setup sidechain routing",
      "on_failure": "abort"
    },
    {
      "id": "step_3",
      "type": "action",
      "action_id": 1013,
      "label": "Record"
    }
  ]
}
```

Step types: `action` (built-in), `script` (registered custom action), `query` (read state, no execution), `condition` (branch on state)

Response: `{ "workflow_id": "wf_drum_sidechain_a1b2", "created": true }`

**POST `/workflows/{id}/execute`**

Run a saved workflow. Returns a step-by-step execution log.

**GET `/workflows`**

List all saved workflows. Supports `?tag=drums` filtering.

**GET `/workflows/{id}`**

Full workflow definition including steps.

**PUT `/workflows/{id}`**

Update a workflow definition.

**DELETE `/workflows/{id}`**

Delete a workflow.

---

### 4.7 Verification

**POST `/verify`**

Check that REAPER state matches expectations after an action.

Request:
```json
{
  "expected": {
    "tracks[0].muted": true,
    "transport.playing": false
  }
}
```

Response:
```json
{
  "verified": false,
  "checks": [
    { "path": "tracks[0].muted", "expected": true, "actual": true, "pass": true },
    { "path": "transport.playing", "expected": false, "actual": true, "pass": false }
  ]
}
```

---

### 4.8 History

**GET `/history`**

Query params: `limit` (default 50), `offset`, `agent_id`

```json
{
  "executions": [
    {
      "id": 1042,
      "type": "action",
      "action_id": 40285,
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
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL,
    category    TEXT,
    section     TEXT DEFAULT 'main',
    created_at  TEXT DEFAULT (datetime('now'))
);
CREATE VIRTUAL TABLE actions_fts USING fts5(name, category, content=actions);

-- Generated and registered ReaScripts
CREATE TABLE scripts (
    id              TEXT PRIMARY KEY,   -- e.g. "_parallel_comp_a1b2"
    name            TEXT NOT NULL,
    language        TEXT NOT NULL,      -- 'lua', 'eel2'
    body            TEXT NOT NULL,
    script_path     TEXT NOT NULL,
    generated_by    TEXT,               -- LLM model name if AI-generated
    intent          TEXT,               -- Original generation prompt
    tags            TEXT,               -- JSON array
    execution_count INTEGER DEFAULT 0,
    created_at      TEXT DEFAULT (datetime('now')),
    last_executed   TEXT
);

-- Saved workflows
CREATE TABLE workflows (
    id          TEXT PRIMARY KEY,
    name        TEXT NOT NULL,
    description TEXT,
    tags        TEXT,               -- JSON array
    steps       TEXT NOT NULL,      -- JSON array of WorkflowStep
    created_at  TEXT DEFAULT (datetime('now')),
    last_executed TEXT,
    execution_count INTEGER DEFAULT 0
);

-- Execution audit log
CREATE TABLE execution_history (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    type            TEXT NOT NULL,      -- 'action', 'script', 'workflow', 'sequence'
    target_id       TEXT NOT NULL,      -- action/script/workflow ID
    agent_id        TEXT,               -- Identifying header from request
    status          TEXT NOT NULL,      -- 'success', 'failed', 'timeout'
    state_before    TEXT,               -- JSON snapshot
    state_after     TEXT,               -- JSON snapshot
    error           TEXT,
    executed_at     TEXT DEFAULT (datetime('now'))
);

```

---

## 6. Security

### Authentication

Three modes, set in `config.json`:

**`none`** — No auth. Use only on localhost or fully trusted network. Default for local dev.

**`api_key`** — All requests must include `Authorization: Bearer {key}`. Key is set in config. Simple and effective for home-network use.

**`mtls`** — Mutual TLS. Client must present a certificate signed by the configured client CA. Strongest option for remote or multi-agent scenarios.

### TLS Configuration

ReaClaw always runs HTTPS. Two certificate modes:

**Self-signed (development / home network):**
```json
"tls": {
  "enabled": true,
  "generate_if_missing": true
}
```
On first run, ReaClaw generates a 4096-bit RSA key and self-signed cert, saved to `{ResourcePath}/reaclaw/certs/`. Agents connecting locally skip certificate verification (or use `-k` with curl).

**CA-signed (production):**
```json
"tls": {
  "enabled": true,
  "cert_file": "/etc/letsencrypt/live/example.com/fullchain.pem",
  "key_file":  "/etc/letsencrypt/live/example.com/privkey.pem"
}
```

### Script Security

| Script type | Trust level | Validation |
|---|---|---|
| Built-in REAPER actions | Fully trusted | None |
| Community scripts (ReaPack) | Trusted | None |
| User-written ReaScripts | Trusted | None |
| AI-generated scripts | Untrusted until validated | Syntax check + static analysis |

Static analysis warnings (not hard failures):
- `os.execute(...)` — shell execution
- `io.open(...)` with path outside project directory — filesystem access
- `reaper.ExecProcess(...)` — process spawning
- Calls to undefined Reaper API functions

All script executions are written to `execution_history`. Source code is retained in SQLite and on disk.

### Rate Limiting

Configurable per-IP and per-key limits. Defaults:
- 60 requests/minute per IP
- 5 script generations/minute per IP

---

## 7. Configuration Reference

Config file: `{GetResourcePath()}/reaclaw/config.json`

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
    "key_file": "",
    "client_ca": ""
  },
  "auth": {
    "type": "api_key",
    "key": "sk_change_me"
  },
  "database": {
    "path": ""
  },
  "llm": {
    "provider": "anthropic",
    "api_key": "",
    "model": "claude-sonnet-4-6",
    "base_url": ""
  },
  "script_security": {
    "validate_syntax": true,
    "static_analysis": true,
    "require_approval": false,
    "log_all_executions": true,
    "max_script_size_kb": 512
  },
  "rate_limiting": {
    "enabled": true,
    "requests_per_minute": 60,
    "scripts_per_minute": 5
  },
  "logging": {
    "level": "info",
    "file": ""
  }
}
```

Notes:
- `database.path`: If empty, defaults to `{ResourcePath}/reaclaw/reaclawdb.sqlite`
- `llm.base_url`: If set, overrides the provider's default endpoint (use for LiteLLM proxy or OpenAI-compatible)
- `tls.client_ca`: Required only when `auth.type` is `mtls`
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
    src/handlers/workflows.cpp
    src/handlers/verify.cpp
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

# Platform-specific output
set_target_properties(reaper_reaclaw PROPERTIES
    PREFIX ""
    OUTPUT_NAME "reaper_reaclaw"
)

# Install to REAPER UserPlugins
if(DEFINED REAPER_USER_PLUGINS)
    install(TARGETS reaper_reaclaw DESTINATION "${REAPER_USER_PLUGINS}")
endif()
```

Build and install:
```bash
cmake -B build -DREAPER_USER_PLUGINS="/path/to/UserPlugins"
cmake --build build --config Release
cmake --install build
```

On Windows, must use MSVC (required for REAPER C++ ABI compatibility).

---

## 9. Implementation Phases Summary

See `ReaClaw_IMPLEMENTATION_CHECKLIST.md` for full task breakdown.

**Phase 0 — Foundation (Weeks 1–2):** Extension scaffold; REAPER API binding; action catalog; state queries; single action execution; HTTPS + auth; SQLite

**Phase 1 — Script Generation (Weeks 3–4):** `/scripts/generate` (LLM API call); syntax validation; static analysis; `AddRemoveReaScript` registration; script cache

**Phase 2 — Feedback Loops (Weeks 5–6):** Multi-step sequences; `/verify` endpoint; state snapshots before/after; execution history; per-step feedback

**Phase 3 — Workflows (Weeks 7–8):** Workflow CRUD; conditional branching; workflow execution log; agent reuse detection

**Phase 4 — Integration & Hardening (Weeks 9–10):** Optional MCP wrapper for OpenClaw/Sparky; multiple concurrent agent support; performance profiling; security audit

---

## 10. Example Agent Flows

### Simple action execution
```
Agent: "Mute the selected track"
  → GET /catalog/search?q=mute+selected+track
  → Returns: id=40285, "Track: Toggle mute for selected tracks"
  → POST /execute/action { "id": 40285, "feedback": true }
  → Returns: { "status": "success", "feedback": { "tracks[0].muted": true } }
  → Agent: Verified. Done.
```

### Generate and cache a script
```
Agent: "Set up parallel compression on the drums bus"
  → GET /scripts/cache?tag=parallel → empty
  → POST /scripts/generate { "intent": "parallel compression on drums bus", "language": "lua" }
  → Returns: { "script": "...", "syntax_valid": true }
  → POST /scripts/register { "name": "parallel_comp_drums", "script": "..." }
  → Returns: { "action_id": "_parallel_comp_drums_a1b2c3" }
  → POST /execute/action { "id": "_parallel_comp_drums_a1b2c3", "feedback": true }
  → POST /verify { "expected": { "tracks[3].fx_count": 2 } }
  → Returns: { "verified": true }

Next time:
  → GET /scripts/cache?tag=parallel → found: _parallel_comp_drums_a1b2c3
  → POST /execute/action { "id": "_parallel_comp_drums_a1b2c3" }
  → No regeneration needed.
```

### Multi-step workflow with verification
```
Agent: "Record drums with sidechain routing from kick"
  → POST /execute/sequence {
      "steps": [
        { "id": 40280, "label": "select kick" },
        { "id": "_setup_sidechain_xyz", "label": "setup sidechain" },
        { "id": 1013, "label": "record" }
      ],
      "feedback_between_steps": true
    }
  → Step 1 feedback: { "selected_track": "Kick" } ✓
  → Step 2 feedback: { "sidechain_target": "Drums Bus" }
  → POST /verify { "expected": { "sidechain_target": "Drums Bus" } }
  → verified: true → continue
  → Step 3: recording started
```

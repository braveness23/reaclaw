# ReaClaw

**ReaClaw** is a native C++ REAPER extension that embeds an HTTPS server directly inside REAPER. It exposes a REST API that lets any HTTP-capable AI agent (Claude, OpenAI, Sparky, or any custom system) operate REAPER fully — browsing the action catalog, executing actions, querying project state, generating and registering custom ReaScripts, and building reusable workflows.

Because ReaClaw is a native extension (not an external process), it has direct access to every REAPER API function with no bridge scripts, no scraping, and no limitations imposed by REAPER's built-in web interface.

---

## Quick Summary

- **Native C++ extension** — Runs inside REAPER's process; full SDK access
- **Embedded HTTPS server** — Self-signed or CA-signed certificates; API key or mTLS auth
- **Full action catalog** — Enumerate all 65K+ actions; search by name, category, tag
- **Action execution** — Single actions, multi-step sequences, saved workflows
- **Script generation** — AI agents generate Lua/EEL2 ReaScripts; ReaClaw validates and registers them natively
- **State queries** — Tracks, BPM, transport, FX chains, automation, selection
- **Feedback loops** — Agents verify their own work; pre/post state snapshots
- **Audit trail** — SQLite persistence for all executions, scripts, and workflows
- **Cross-platform** — Windows, macOS, Linux; one codebase

---

## Architecture

```
AI Agent (Claude, Sparky, curl, etc.)
  │
  │  HTTPS (REST/JSON)
  ▼
┌─────────────────────────────────────────────────────────────┐
│  ReaClaw Extension  (reaper_reaclaw.dll / .dylib / .so)     │
│                                                             │
│  ┌─────────────────┐   ┌──────────────────────────────┐    │
│  │  HTTPS Server   │   │  Command Queue               │    │
│  │  (cpp-httplib)  │──▶│  (web thread → main thread)  │    │
│  └─────────────────┘   └──────────────┬───────────────┘    │
│                                        │                    │
│  ┌─────────────────────────────────────▼───────────────┐   │
│  │  REAPER SDK  (full API access via GetFunc)          │   │
│  │  Action catalog · Execution · State · Scripts       │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
│  ┌──────────────────────┐  ┌──────────────────────────┐    │
│  │  SQLite              │  │  Config                  │    │
│  │  (scripts, history,  │  │  (reaclaw/config.json)   │    │
│  │   workflows, cache)  │  │                          │    │
│  └──────────────────────┘  └──────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
  │
  │  In-process REAPER API calls
  ▼
REAPER
  ├─ Action System (65K+ commands)
  ├─ ReaScript Runtime (Lua / EEL2)
  ├─ Project State (tracks, items, automation)
  └─ Plugin/FX chain
```

---

## How It Works

ReaClaw is a standard REAPER extension (`.dll` on Windows, `.dylib` on macOS, `.so` on Linux). REAPER loads it at startup from the `UserPlugins` directory. On load, ReaClaw:

1. Reads its config from `{REAPER_RESOURCE_PATH}/reaclaw/config.json`
2. Spawns a background thread running an HTTPS server (cpp-httplib + OpenSSL)
3. Registers a main-thread timer callback for executing actions safely
4. Indexes the REAPER action catalog into SQLite
5. Begins accepting API requests

All REAPER API calls that require the main thread are dispatched through a thread-safe command queue and executed on REAPER's timer callback. Threadsafe REAPER calls (state reads) are made directly from the server thread.

---

## Installation

1. Build ReaClaw for your platform (see `DESIGN.md` → Build section)
2. Copy `reaper_reaclaw.dll` / `.dylib` / `.so` to your REAPER `UserPlugins` directory:
   - **Windows:** `%APPDATA%\REAPER\UserPlugins\`
   - **macOS:** `~/Library/Application Support/REAPER/UserPlugins/`
   - **Linux:** `~/.config/REAPER/UserPlugins/`
3. Copy `config.example.json` to `{REAPER_RESOURCE_PATH}/reaclaw/config.json` and edit
4. Restart REAPER
5. Verify: `curl -k https://localhost:9091/health`

---

## API Endpoints (Overview)

### System
- `GET /health` — Server status and version

### Catalog
- `GET /catalog` — Full action catalog
- `GET /catalog/search?q=query` — Search by name, tag, category
- `GET /catalog/categories` — Category list with counts

### State
- `GET /state` — Project overview (BPM, cursor, transport)
- `GET /state/tracks` — All tracks with properties and FX chains
- `GET /state/items` — Media items in project
- `GET /state/selection` — Current selection context
- `GET /state/automation` — Automation envelopes

### Execution
- `POST /execute/action` — Execute a single action
- `POST /execute/sequence` — Multi-step sequence with per-step feedback

### Scripts
- `POST /scripts/generate` — Ask an LLM to generate a ReaScript
- `POST /scripts/validate` — Validate script syntax without registering
- `POST /scripts/register` — Register a script as a custom REAPER action
- `GET /scripts/cache` — List all cached scripts
- `GET /scripts/{id}` — Get script source

### Workflows
- `POST /workflows` — Save a workflow (named sequence of steps)
- `POST /workflows/{id}/execute` — Run a saved workflow
- `GET /workflows` — List all workflows
- `GET /workflows/{id}` — Get workflow definition
- `PUT /workflows/{id}` — Update workflow
- `DELETE /workflows/{id}` — Delete workflow

### Verification & History
- `POST /verify` — Verify expected state after an action
- `GET /history` — Execution history

---

## Tech Stack

| Component | Choice | Reason |
|---|---|---|
| Language | C++ (C++17) | Required for REAPER native extension |
| Build | CMake | Cross-platform; industry standard |
| HTTP/HTTPS | cpp-httplib | Header-only; OpenSSL TLS; cross-platform |
| TLS | OpenSSL | Industry standard; self-signed + CA certs |
| Database | SQLite (amalgamation) | Embedded; no external deps |
| JSON | nlohmann/json | Header-only; standard |
| REAPER SDK | justinfrankel/reaper-sdk | Official SDK |

---

## Configuration

Config lives at `{REAPER_RESOURCE_PATH}/reaclaw/config.json`. See `config.example.json` for all options. Minimal working config:

```json
{
  "server": { "port": 9091 },
  "tls": { "enabled": true, "generate_if_missing": true },
  "auth": { "type": "api_key", "key": "sk_change_me" }
}
```

---

## Directory Structure

```
reaclaw/
├── README.md                         (this file)
├── ReaClaw_Design.md                 (full specification)
├── ReaClaw_TECH_DECISIONS.md         (architecture decisions)
├── ReaClaw_IMPLEMENTATION_CHECKLIST.md (phase-by-phase tasks)
├── config.example.json               (config template)
├── CMakeLists.txt
├── src/
│   ├── main.cpp                      (ReaperPluginEntry, init/teardown)
│   ├── server/
│   │   ├── server.cpp / .h           (httplib setup, TLS, thread lifecycle)
│   │   └── router.cpp / .h           (endpoint registration)
│   ├── handlers/
│   │   ├── catalog.cpp / .h          (GET /catalog, /catalog/search)
│   │   ├── state.cpp / .h            (GET /state/*)
│   │   ├── execute.cpp / .h          (POST /execute/*)
│   │   ├── scripts.cpp / .h          (POST /scripts/*)
│   │   ├── workflows.cpp / .h        (POST/GET /workflows/*)
│   │   ├── verify.cpp / .h           (POST /verify)
│   │   └── history.cpp / .h          (GET /history)
│   ├── reaper/
│   │   ├── api.cpp / .h              (REAPER API wrappers + GetFunc bindings)
│   │   ├── catalog.cpp / .h          (action enumeration, indexing)
│   │   ├── executor.cpp / .h         (action execution, command queue)
│   │   └── scripts.cpp / .h          (ReaScript registration via AddRemoveReaScript)
│   ├── db/
│   │   ├── db.cpp / .h               (SQLite connection, migrations)
│   │   └── schema.sql                (table definitions)
│   ├── auth/
│   │   └── auth.cpp / .h             (API key, mTLS middleware)
│   ├── config/
│   │   └── config.cpp / .h           (JSON config loading)
│   └── util/
│       ├── tls.cpp / .h              (self-signed cert generation)
│       └── logging.cpp / .h          (structured logging)
├── vendor/
│   ├── httplib.h                     (cpp-httplib, header-only)
│   ├── json.hpp                      (nlohmann/json, header-only)
│   ├── sqlite3.c / sqlite3.h         (SQLite amalgamation)
│   └── reaper-sdk/                   (REAPER SDK headers)
├── certs/                            (TLS certs, .gitignored)
├── tests/
└── docs/
    ├── API.md                        (detailed endpoint reference)
    └── EXAMPLES.md                   (workflow examples for agents)
```

---

## Implementation Phases

See `ReaClaw_IMPLEMENTATION_CHECKLIST.md` for full task breakdown.

| Phase | Scope | Deliverable |
|---|---|---|
| 0 | Extension scaffold, catalog, state, single action execution | v0.0.1 |
| 1 | Script generation, validation, registration | v0.1.0 |
| 2 | Multi-step sequences, verification, feedback loops | v0.2.0 |
| 3 | Workflows, conditional branching, caching | v0.3.0 |
| 4 | Performance, MCP integration, hardening | v1.0.0 |

---

## Integration Examples

### With Claude (tool use)
```json
{
  "tool": "http_request",
  "method": "POST",
  "url": "https://localhost:9091/execute/action",
  "headers": { "Authorization": "Bearer sk_your_key" },
  "body": { "id": 40285, "feedback": true }
}
```

### With Sparky/OpenClaw (MCP — Phase 4)
```
Sparky: "Set up drum recording with sidechain"
  → calls reaclawExecuteWorkflow("drum_sidechain_record")
  → ReaClaw runs 6-step workflow in REAPER
  → Returns: {"status": "success", "steps_completed": 6}
```

### With curl (testing)
```bash
curl -sk -H "Authorization: Bearer sk_your_key" \
  https://localhost:9091/catalog/search?q=mute | jq .
```

---

## License

TBD

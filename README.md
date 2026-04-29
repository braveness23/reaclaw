# ReaClaw

[![CI](https://github.com/braveness23/reaclaw/actions/workflows/ci.yml/badge.svg)](https://github.com/braveness23/reaclaw/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![GitHub Release](https://img.shields.io/github/v/release/braveness23/reaclaw)](https://github.com/braveness23/reaclaw/releases/latest)

**ReaClaw** is a native C++ REAPER extension that embeds an HTTPS server directly inside REAPER. It exposes a REST API that lets any HTTP-capable AI agent operate REAPER fully — browsing the action catalog, executing actions, querying project state, registering custom Lua ReaScripts, and running multi-step sequences with per-step feedback.

Because ReaClaw is a native extension (not an external process), it has direct access to every REAPER API function with no bridge scripts, no scraping, and no limitations.

---

## Quick Summary

- **Native C++ extension** — Runs inside REAPER's process; full SDK access
- **Embedded HTTPS server** — Self-signed or CA-signed certificates; API key auth
- **Full action catalog** — Enumerate all 65K+ actions; search by name, category, tag
- **Action execution** — Single actions and multi-step sequences with per-step feedback
- **Script registration** — Agent generates Lua; ReaClaw validates syntax and registers natively via `AddRemoveReaScript`
- **State queries** — Tracks, BPM, transport, FX chains, automation, selection
- **Audit trail** — SQLite persistence for all executions and registered scripts
- **Cross-platform** — Windows, macOS, Linux; one codebase

---

## Architecture

```
AI Agent (Claude, Sparky, curl, etc.)
  │
  │  HTTPS (REST/JSON)
  ▼
┌─────────────────────────────────────────────────────────────┐
│  reaper_reaclaw  (.dll / .dylib / .so)                      │
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
│  │   action catalog)    │  │                          │    │
│  └──────────────────────┘  └──────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
  │
  │  In-process REAPER API calls
  ▼
REAPER
  ├─ Action System (65K+ commands)
  ├─ ReaScript Runtime (Lua)
  └─ Project State (tracks, items, automation)
```

---

## How It Works

ReaClaw is a standard REAPER extension loaded at startup from the `UserPlugins` directory. On load it:

1. Reads config from `{REAPER_RESOURCE_PATH}/reaclaw/config.json`
2. Opens the SQLite database and runs schema migrations
3. Spawns a background HTTPS server thread
4. Registers a main-thread timer callback for safe REAPER API execution
5. Indexes the full action catalog into SQLite
6. Begins accepting API requests

The agent is responsible for generating scripts using its own LLM capabilities. ReaClaw's job is to validate syntax and register them with REAPER — not to call an LLM itself.

---

## Installation

1. Build ReaClaw for your platform (see [CONTRIBUTING.md](CONTRIBUTING.md) for build instructions)
2. Copy the built file to REAPER's `UserPlugins` directory:
   - **Windows:** `%APPDATA%\REAPER\UserPlugins\`
   - **macOS:** `~/Library/Application Support/REAPER/UserPlugins/`
   - **Linux:** `~/.config/REAPER/UserPlugins/`
3. Copy `config.example.json` to `{REAPER_RESOURCE_PATH}/reaclaw/config.json` and set your API key
4. Restart REAPER
5. Verify: `curl -sk -H "Authorization: Bearer sk_your_key" https://localhost:9091/health`

---

## API Endpoints

### System
- `GET /health` — Server status, version, catalog size

### Catalog
- `GET /catalog` — Full action catalog (paginated)
- `GET /catalog/search?q=query` — Search by name, category, tag
- `GET /catalog/{id}` — Look up a single action by numeric ID
- `GET /catalog/categories` — Category list with counts

### State
- `GET /state` — Project overview (BPM, cursor, transport)
- `GET /state/tracks` — All tracks with properties and FX chains
- `POST /state/tracks/{index}` — Set track properties (mute, arm, volume, pan) directly by index
- `GET /state/items` — Media items in project
- `GET /state/selection` — Current selection context
- `GET /state/automation` — Automation envelopes for selected track

### Execution
- `POST /execute/action` — Execute a single action by ID
- `POST /execute/sequence` — Multi-step sequence with per-step state feedback

### Scripts
- `POST /scripts/register` — Validate and register agent-generated Lua as a custom action
- `GET /scripts/cache` — List all registered scripts
- `GET /scripts/{id}` — Get script source and metadata
- `DELETE /scripts/{id}` — Unregister and remove a script

### History
- `GET /history` — Execution audit log

---

## Tech Stack

| Component | Choice | Reason |
|---|---|---|
| Language | C++ (C++17) | Required for REAPER native extension |
| Build | CMake | Cross-platform; industry standard |
| HTTP/HTTPS | cpp-httplib | Header-only; OpenSSL TLS; cross-platform |
| TLS | OpenSSL | Self-signed + CA certs |
| Database | SQLite (amalgamation) | Embedded; no external deps |
| JSON | nlohmann/json | Header-only; standard |
| REAPER SDK | justinfrankel/reaper-sdk | Official SDK |

---

## Configuration

Config at `{REAPER_RESOURCE_PATH}/reaclaw/config.json`. See `config.example.json`. Minimal working config:

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
├── README.md
├── ReaClaw_Design.md
├── ReaClaw_TECH_DECISIONS.md
├── ReaClaw_IMPLEMENTATION_CHECKLIST.md
├── config.example.json
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
│   │   ├── scripts.cpp / .h          (POST/GET/DELETE /scripts/*)
│   │   └── history.cpp / .h          (GET /history)
│   ├── reaper/
│   │   ├── api.cpp / .h              (REAPER API wrappers + GetFunc bindings)
│   │   ├── catalog.cpp / .h          (action enumeration, indexing)
│   │   ├── executor.cpp / .h         (action execution, command queue)
│   │   └── scripts.cpp / .h          (ReaScript registration via AddRemoveReaScript)
│   ├── db/
│   │   └── db.cpp / .h               (SQLite connection, schema, migrations)
│   ├── auth/
│   │   └── auth.cpp / .h             (API key middleware)
│   ├── config/
│   │   └── config.cpp / .h           (JSON config loading)
│   └── util/
│       ├── tls.cpp / .h              (self-signed cert generation)
│       └── logging.cpp / .h
├── vendor/
│   ├── httplib.h
│   ├── json.hpp
│   ├── sqlite3.c / sqlite3.h
│   └── reaper-sdk/
├── certs/                            (.gitignored)
├── tests/
└── docs/
    ├── API.md
    └── EXAMPLES.md
```

---

## Implementation Phases

| Phase | Scope | Deliverable |
|---|---|---|
| 0 | Extension scaffold, catalog, state, action execution, HTTPS + auth | v0.1.0 |
| 1 | Script registration, syntax validation, multi-step sequences, history | v0.2.0 |
| 2 | MCP wrapper, performance, security hardening | v1.0.0 |

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

### Agent generates and registers a script
```
Agent generates Lua script for parallel compression
  → POST /scripts/register { "name": "parallel_comp", "script": "local tr = ..." }
  → Returns: { "action_id": "_parallel_comp_a1b2c3" }
  → POST /execute/action { "id": "_parallel_comp_a1b2c3" }
```

### With Sparky/OpenClaw (Phase 2 MCP)
```
Sparky: "Set up drum recording"
  → reaclawExecuteSequence([mute_all, arm_drums, route_sidechain, record])
  → Returns: { "status": "success", "steps_completed": 4 }
```

### With curl
```bash
curl -sk -H "Authorization: Bearer sk_your_key" \
  "https://localhost:9091/catalog/search?q=mute" | jq .
```

---

## License

MIT License — see [LICENSE](LICENSE) for full text.

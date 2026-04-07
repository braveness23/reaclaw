# ReaClaw

**ReaClaw** is a programmable HTTP API that bridges AI agents (Claude, or any LLM) to REAPER's action system and ReaScript runtime. It enables agents to query Reaper's state, execute actions, generate custom workflows, and learn from execution history—all without manual intervention.

## Quick Summary

- **Agent-Agnostic:** Works with Claude, OpenAI, Anthropic, or any HTTP-capable AI system
- **HTTPS + Auth:** Self-signed or signed certs; API key, mTLS, or no-auth options
- **Action Execution:** Call any of Reaper's 65K+ actions over HTTP
- **Script Generation:** AI agents generate Lua ReaScripts dynamically
- **Caching & Learning:** Reuse workflows, avoid regeneration
- **Full Audit Trail:** SQLite persistence for history, verification, debugging
- **Feedback Loops:** Query state before/after actions; agents verify their own work

## Architecture

```
Agent (Claude, etc.)
  ↓ HTTP/HTTPS
ReaClaw API Server
  ├─ Action Catalog Service
  ├─ State Query Service
  ├─ Script Generation & Registration
  ├─ Action Execution Engine
  ├─ Caching (SQLite)
  └─ Audit Logging
  ↓ HTTP (Reaper Web Control)
REAPER
  ├─ Web Interface
  ├─ ReaScript Runtime
  ├─ Action System (65K+)
  └─ Project State
```

## Key Features

### Action Catalog
- Full Reaper action list (ID, name, category, tags, description)
- Fuzzy search by keyword/tag
- Static indexing for fast lookups

### State Queries
- Current project state (BPM, cursor, transport)
- Track list with names, properties, FX
- Media items, automation, selection state
- Post-execution verification

### Script Generation
- AI generates Lua ReaScripts based on intent
- Syntax validation before registration
- Static analysis warnings (suspicious patterns)
- Optional human approval workflow

### Action Execution
- Single actions: `POST /execute/action`
- Sequences: `POST /execute/sequence` with conditional branching
- Workflows: Saved multi-step sequences with logic
- Feedback after each step

### Caching & Reuse
- Generated scripts cached with unique action IDs
- Agents check cache before regenerating
- Execution history for learning patterns

## Configuration

See `config.example.yaml`:

```yaml
server:
  host: "0.0.0.0"
  port: 9091

tls:
  enabled: true
  cert_file: "certs/self_signed.crt"
  key_file: "certs/self_signed.key"
  generate_if_missing: true

auth:
  type: "api_key"  # or "mtls" or "none"
  key: "sk_your_secret_key"

reaper:
  web_interface_url: "http://localhost:8808"

database:
  type: "sqlite"
  path: "reaclawdb.sqlite"

script_security:
  validate_syntax: true
  static_analysis: true
  require_approval: false
  log_all_executions: true
  max_script_size_mb: 5

rate_limiting:
  enabled: true
  requests_per_minute: 60
  scripts_per_minute: 5
```

## API Endpoints (Overview)

### Catalog
- `GET /catalog` — Full action catalog
- `GET /catalog/search?q=query` — Search actions
- `GET /catalog/categories` — Category list

### State
- `GET /state` — Project state (high-level)
- `GET /state/tracks` — Track details
- `GET /state/items` — Media items
- `GET /state/automation` — Automation envelopes

### Execution
- `POST /execute/action` — Execute single action
- `POST /execute/sequence` — Multi-step sequence with feedback

### Scripts
- `POST /scripts/generate` — Generate script from intent
- `POST /scripts/register` — Register generated script as action
- `GET /scripts/cache` — List cached scripts
- `GET /scripts/{action_id}` — Get script source

### Workflows
- `POST /workflows` — Create workflow
- `POST /workflows/{id}/execute` — Run workflow
- `GET /workflows` — List workflows

### Verification & History
- `POST /verify` — Verify action effects
- `GET /history` — Execution history

## Implementation Phases

See `IMPLEMENTATION_CHECKLIST.md` for detailed breakdown.

**Phase 0: MVP (Weeks 1-2)**
- Action catalog indexing
- Basic state queries
- Action execution

**Phase 1: Script Generation (Weeks 3-4)**
- `/scripts/generate` endpoint
- Script registration & caching
- Syntax validation

**Phase 2: Feedback Loops (Weeks 5-6)**
- Multi-step sequences
- Verification & state snapshots
- Execution history

**Phase 3: Workflows (Weeks 7-8)**
- Workflow CRUD
- Conditional branching
- Workflow caching

**Phase 4: Integration (Weeks 9-10)**
- Performance optimization
- Distributed agent support
- Error recovery patterns

## Tech Stack

- **Language:** Go
- **Database:** SQLite (embedded, file-based)
- **HTTP:** Go `net/http` + routing library (e.g., `chi`, `gin`)
- **Reaper Integration:** HTTP to Reaper's built-in Web Control (MVP), custom ReaScript bridge (Phase 2+)
- **TLS:** Go `crypto/tls` (built-in cert support)

## Security

- **HTTPS:** Self-signed or signed certs
- **Auth:** API key, mTLS, or none (for trusted networks)
- **Script Validation:** Syntax check + static analysis for generated scripts
- **Audit Logging:** SQLite persistence of all executions
- **Rate Limiting:** Configurable per-IP and per-key

## Getting Started

1. Clone repo: `git clone https://github.com/braveness23/reclaw.git`
2. Read `DESIGN.md` for full specification
3. Check `TECH_DECISIONS.md` for key architecture choices
4. Follow `IMPLEMENTATION_CHECKLIST.md` phase by phase
5. Configure `config.yaml` (or use defaults)
6. Build & run: `go build && ./reclaw`

## Integration Examples

### With Claude (via Artifacts)
Claude generates Go code that POSTs to ReaClaw:
```go
resp, _ := http.Post("https://localhost:9091/execute/action",
  "application/json",
  bytes.NewBufferString(`{"id": 40285}`))
```

### With OpenClaw/Sparky
ReaClaw exposed as MCP tool (optional):
```
Sparky needs to: Record drums
  → Calls ReaClaw via MCP
  → Execute workflow "drum_recording_setup"
  → Sparky gets back: {"status": "success", "armed": true}
```

### With Curl (Manual Testing)
```bash
curl -H "Authorization: Bearer sk_your_key" \
  https://localhost:9091/catalog/search?q=mute
```

## Directory Structure (Planned)

```
reclaw/
├── README.md                       (this file)
├── DESIGN.md                       (full specification)
├── TECH_DECISIONS.md               (architecture choices)
├── IMPLEMENTATION_CHECKLIST.md     (phase breakdown)
├── config.example.yaml             (config template)
├── go.mod
├── go.sum
├── main.go
├── cmd/                            (CLI entry points)
├── pkg/
│   ├── api/                        (HTTP handlers)
│   ├── reaper/                     (Reaper Web Control integration)
│   ├── catalog/                    (Action catalog indexing)
│   ├── script/                     (Script generation & validation)
│   ├── db/                         (SQLite persistence)
│   ├── auth/                       (TLS & auth)
│   └── models/                     (Data structures)
├── tests/
├── certs/                          (TLS certs, .gitignored)
├── docs/
│   ├── API.md                      (Detailed API docs)
│   └── EXAMPLES.md                 (Workflow examples)
└── .gitignore
```

## Contributing / Extending

This is a specification + reference implementation. Feel free to:
- Implement in your own language
- Add new endpoints
- Extend with custom Reaper integrations
- Build client libraries

## License

(TBD)

## Contact

Built for maximum flexibility and AI agent autonomy in REAPER.

# ReaClaw: Implementation Checklist

Each phase is a shippable unit. Complete and test each phase before starting the next. All tasks within a phase can be worked in parallel unless noted with a dependency marker (→ depends on).

---

## Phase 0: Foundation (Weeks 1–2)

**Goal:** A working REAPER extension that starts an HTTPS server, indexes the action catalog, responds to state queries, and executes single actions.

### Project Setup

- [ ] Create `CMakeLists.txt` (see Design doc §8 for structure)
- [ ] Create `vendor/` directory and populate:
  - [ ] Download `httplib.h` from yhirose/cpp-httplib (latest release)
  - [ ] Download `json.hpp` from nlohmann/json (3.x, single header)
  - [ ] Download SQLite amalgamation (`sqlite3.c`, `sqlite3.h`) from sqlite.org
  - [ ] Clone or copy REAPER SDK headers into `vendor/reaper-sdk/`
        (justinfrankel/reaper-sdk on GitHub)
- [ ] Add `.gitignore`:
  - `build/`
  - `certs/*.crt`, `certs/*.key`, `certs/*.pem`
  - `*.sqlite`
  - `.DS_Store`, `*.user`, `.vs/`
- [ ] Verify CMake builds a `.dll`/`.dylib`/`.so` with no source yet (scaffold only)
- [ ] Confirm the output file can be loaded by REAPER (copy to UserPlugins, restart, check for crash)

### Extension Entry Point (`src/main.cpp`)

- [ ] Implement `ReaperPluginEntry(HINSTANCE, reaper_plugin_info_t*)`
  - [ ] Check `rec == NULL` branch (unload) and return 0
  - [ ] Check `rec->caller_version != REAPER_PLUGIN_VERSION` and return 0
  - [ ] Call `REAPERAPI_LoadAPI(rec->GetFunc)` — bind all REAPER API function pointers
  - [ ] On success, call `ReaClaw::init(rec)` and return 1
  - [ ] On unload, call `ReaClaw::shutdown()`
- [ ] Print startup message to REAPER console: `ShowConsoleMsg("ReaClaw: starting...\n")`
- [ ] Register main-thread timer callback via `plugin_register("timer", timer_callback_fn)`
- [ ] Unregister timer on unload

### Config (`src/config/`)

- [ ] Implement `Config::load()`:
  - [ ] Call `GetResourcePath()` to find REAPER resource dir
  - [ ] Look for `{ResourcePath}/reaclaw/config.json`
  - [ ] If directory `reaclaw/` doesn't exist, create it
  - [ ] If `config.json` doesn't exist, write default config and log notice
  - [ ] Parse with nlohmann/json into a `Config` struct
- [ ] Config struct fields: `server.host`, `server.port`, `server.thread_pool_size`, `tls.*`, `auth.*`, `database.path`, `rate_limiting.*`, `logging.*`
- [ ] Write `config.example.json` alongside this checklist (see `config.example.json` in repo)
- [ ] Unit test: load valid config, load config with missing fields (defaults applied)

### Database (`src/db/`)

- [ ] `db.cpp`: Open SQLite connection to `{ResourcePath}/reaclaw/reaclawdb.sqlite` (or config path)
- [ ] Run schema migrations on open (read `schema.sql`, apply each CREATE TABLE IF NOT EXISTS)
- [ ] Implement `schema.sql` with all tables from Design doc §5:
  - `actions` + `actions_fts` (FTS5)
  - `scripts`
  - `workflows`
  - `execution_history`
- [ ] Provide a simple `DB` class with `execute(sql)`, `query(sql, params) → rows`, `insert(...)` helpers
- [ ] Unit test: create DB, create tables, insert and query a row

### REAPER API Layer (`src/reaper/api.cpp`)

- [ ] Use `#define REAPERAPI_IMPLEMENT` before including `reaper_plugin_functions.h`
- [ ] Verify all needed function pointers are non-null after `REAPERAPI_LoadAPI` — log a warning for any missing (indicates an older REAPER version)
- [ ] Functions needed for Phase 0:
  - `kbd_enumerateActions`, `SectionFromUniqueID`
  - `CountTracks`, `GetTrack`, `GetTrackName`, `GetSetMediaTrackInfo`
  - `GetProjectTimeSignature2`, `GetCursorPosition`, `GetPlayState`
  - `Main_OnCommand`, `Main_OnCommandEx`
  - `GetResourcePath`, `GetAppVersion`
  - `ShowConsoleMsg`

### Action Catalog Indexer (`src/reaper/catalog.cpp`)

- [ ] On startup, check SQLite `actions` table:
  - [ ] If empty or REAPER version changed (compare `GetAppVersion()` against stored value), rebuild index
  - [ ] Enumerate via `SectionFromUniqueID(0)` + `kbd_enumerateActions` loop
  - [ ] For each action: extract command ID and name; derive category from name prefix (e.g., `"Track: "` → category `"Track"`)
  - [ ] Bulk-insert into `actions` table; rebuild `actions_fts` index
  - [ ] Log count: `"ReaClaw: catalog indexed N actions"`
- [ ] Unit test: mock `kbd_enumerateActions`; verify correct rows inserted

### Command Queue + Timer Callback (`src/reaper/executor.cpp`)

- [ ] Define `Command` struct: `std::function<void()> execute`, `std::promise<nlohmann::json> result`
- [ ] `std::queue<Command>` protected by `std::mutex`
- [ ] `post_command(fn) → std::future<nlohmann::json>`: enqueue and return future
- [ ] Timer callback (called on main thread at ~30fps):
  - Drain queue up to N commands per tick (configurable, default 10)
  - For each: call `execute()`, set `result` promise
- [ ] Timeout handling: if future not ready within 5s, return HTTP 408

### HTTPS Server (`src/server/`)

- [ ] `server.cpp`: Initialize `httplib::SSLServer` with cert/key paths
  - [ ] If `tls.generate_if_missing` and no cert/key found, call `TLS::generate_self_signed()` first
  - [ ] Set thread pool size from config
  - [ ] Bind auth middleware: check `Authorization: Bearer {key}` on every request if `auth.type == "api_key"`
  - [ ] Bind rate limiter: per-IP request count with 60s sliding window
  - [ ] Set common response headers: `Content-Type: application/json`
- [ ] `router.cpp`: Register all route handlers (stubs returning 501 for Phase 0 unimplemented routes)
- [ ] Start server on background thread; store thread handle for shutdown join

### TLS Utilities (`src/util/tls.cpp`)

- [ ] `TLS::generate_self_signed(cert_path, key_path)`:
  - [ ] Generate 4096-bit RSA key via `EVP_PKEY_keygen`
  - [ ] Create self-signed X.509 cert valid for 10 years
  - [ ] Write PEM-encoded cert and key to specified paths
  - [ ] Log: `"ReaClaw: generated self-signed TLS cert at {path}"`

### Phase 0 API Handlers

- [ ] `GET /health` → server status, version, catalog size, uptime
- [ ] `GET /catalog` → paginated action list from SQLite (→ depends on catalog indexed)
- [ ] `GET /catalog/search?q=...` → FTS5 search on `actions_fts`
- [ ] `GET /catalog/categories` → GROUP BY category from `actions` table
- [ ] `GET /state` → call REAPER API directly (threadsafe); return project/transport/selection JSON
- [ ] `GET /state/tracks` → enumerate tracks, FX chains, properties
- [ ] `POST /execute/action` → post to command queue; wait for result; log to `execution_history`

### Logging (`src/util/logging.cpp`)

- [ ] Structured logging with level filter from config (`debug`, `info`, `warn`, `error`)
- [ ] Write to REAPER console via `ShowConsoleMsg` if no log file configured
- [ ] Write to file if `logging.file` is set in config
- [ ] Format: `[LEVEL] [timestamp] message`

### Phase 0 Deliverable

- [ ] Extension loads in REAPER without crash on Windows, macOS, Linux
- [ ] HTTPS server starts; `GET /health` returns 200
- [ ] Action catalog indexed; `/catalog/search?q=mute` returns correct results
- [ ] `/state` returns correct BPM, transport state, track list
- [ ] `POST /execute/action {"id": 40285}` mutes the selected track in REAPER
- [ ] All executions written to `execution_history`
- [ ] Auth + rate limiting functional
- [ ] Push `main` branch; tag `v0.0.1`
- [ ] Write `docs/API.md` for Phase 0 endpoints

---

## Phase 1: Script Generation & Registration (Weeks 3–4)

**Goal:** AI agents can generate Lua ReaScripts, validate them, register them as custom REAPER actions via `AddRemoveReaScript`, and call them by action ID.

### Prerequisites

- [ ] Phase 0 complete and tagged

### LLM Client (`src/llm/`)

- [ ] `llm.cpp`: HTTP(S) client that calls an LLM to generate scripts
  - [ ] Support `provider: "anthropic"` — POST to `https://api.anthropic.com/v1/messages`
  - [ ] Support `provider: "openai"` — POST to `https://api.openai.com/v1/chat/completions`
  - [ ] Support `base_url` override (for LiteLLM proxy or any OpenAI-compatible endpoint)
  - [ ] Use cpp-httplib for the outbound request (separate client instance from server)
  - [ ] Config: `llm.api_key`, `llm.model`, `llm.base_url`
- [ ] System prompt for script generation: explain REAPER Lua API, request a complete, working script
- [ ] Parse LLM response; extract generated code block

### Script Validation (`src/script/validator.cpp`)

- [ ] Syntax validation:
  - [ ] Shell out to `luac -p {script_file}` if `luac` is available on PATH
  - [ ] If `luac` not available, use a simple bracket-matching and keyword check as fallback
  - [ ] Return `{ syntax_valid: bool, error: string, line: int }`
- [ ] Static analysis (warnings, not failures):
  - [ ] Scan for `os.execute`, `io.open`, `reaper.ExecProcess` — flag each occurrence with line number
  - [ ] Scan for undefined `reaper.*` calls: compare against known API list (build from REAPER SDK docs)
  - [ ] Return array of `{ line: int, message: string }` warnings

### Script Registration (`src/reaper/scripts.cpp`)

- [ ] `Scripts::register_script(name, body, language, tags) → action_id`:
  - [ ] Generate a unique ID: `_{name}_{sha256_prefix_8chars}`
  - [ ] Write script to `{ResourcePath}/reaclaw/scripts/{action_id}.lua`
  - [ ] Post to command queue: call `AddRemoveReaScript(true, 0, path, true)` on main thread
  - [ ] Receive assigned command ID back from REAPER
  - [ ] Insert into `scripts` table
- [ ] `Scripts::unregister_script(action_id)`: call `AddRemoveReaScript(false, ...)`, delete file, remove from DB
- [ ] Handle idempotency: if same name already registered, return existing ID

### Phase 1 API Handlers

- [ ] `POST /scripts/generate` → call LLM client; run validator; return script + warnings (→ depends on LLM client, validator)
- [ ] `POST /scripts/validate` → run validator only; no LLM call, no registration
- [ ] `POST /scripts/register` → validate then register via `AddRemoveReaScript` (→ depends on validator, script registrar)
- [ ] `GET /scripts/cache` → query `scripts` table
- [ ] `GET /scripts/{id}` → read from `scripts` table; return source + metadata

### Phase 1 Deliverable

- [ ] `POST /scripts/generate` produces a syntactically valid Lua script for common intents (parallel comp, mute track group, EQ setup)
- [ ] `POST /scripts/register` registers the script; it appears in REAPER's Actions list under the generated ID
- [ ] `POST /execute/action` with the generated ID runs the script in REAPER
- [ ] Calling `POST /scripts/register` twice with same name returns the same ID (idempotent)
- [ ] Static analysis catches obvious bad patterns; warnings returned to agent
- [ ] Push `main`; tag `v0.1.0`
- [ ] Update `docs/API.md` with script endpoints
- [ ] Add `docs/EXAMPLES.md` with "generate and cache" example

---

## Phase 2: Feedback Loops & State Verification (Weeks 5–6)

**Goal:** Agents execute multi-step sequences and verify the effects of their actions.

### Prerequisites

- [ ] Phase 1 complete and tagged

### Multi-Step Execution (`src/handlers/execute.cpp`)

- [ ] `POST /execute/sequence`:
  - [ ] Iterate steps array
  - [ ] For each step: post to command queue, await result
  - [ ] If `feedback_between_steps: true`, query state after each step
  - [ ] If `stop_on_failure: true`, abort sequence on first failure; log completed + failed steps
  - [ ] Collect per-step results; return full execution log

### State Snapshots

- [ ] `StateSnapshot::capture() → nlohmann::json`:
  - [ ] Call all threadsafe state read functions
  - [ ] Return a full JSON blob of: project info, transport, all tracks + FX
- [ ] Before each action execution: capture snapshot, store in `execution_history.state_before`
- [ ] After each action execution: capture snapshot, store in `execution_history.state_after`

### Verification (`src/handlers/verify.cpp`)

- [ ] `POST /verify`:
  - [ ] Accept `expected` as a map of JSON path → expected value
  - [ ] Capture current state snapshot
  - [ ] For each key in `expected`: resolve the JSON path in the snapshot; compare values
  - [ ] Return: `verified` bool, per-check results with `pass/fail`, `actual` values
- [ ] JSON path resolution: support simple dot-notation (`tracks[0].muted`, `transport.playing`)

### History Enhancement

- [ ] Extend `GET /history` response to include `state_before`, `state_after`, `verification_result`
- [ ] Add query params: `type` filter (action/script/workflow), `status` filter (success/failed), date range

### Phase 2 Deliverable

- [ ] `POST /execute/sequence` with 5 steps executes reliably; all step results returned
- [ ] `feedback_between_steps: true` returns state after each step
- [ ] `POST /verify` correctly identifies when an action did or did not have the expected effect
- [ ] `execution_history` entries include state_before and state_after JSON
- [ ] Push `main`; tag `v0.2.0`
- [ ] Add "multi-step feedback loop" example to `docs/EXAMPLES.md`

---

## Phase 3: Workflows & Macro System (Weeks 7–8)

**Goal:** Save and reuse named multi-step workflows; agents learn to reuse instead of regenerate.

### Prerequisites

- [ ] Phase 2 complete and tagged

### Workflow CRUD (`src/handlers/workflows.cpp`)

- [ ] `POST /workflows` — validate step array; assign ID; insert into `workflows` table
- [ ] `GET /workflows` — list all; support `?tag=` filter
- [ ] `GET /workflows/{id}` — fetch single workflow with all steps
- [ ] `PUT /workflows/{id}` — update name, description, tags, or steps
- [ ] `DELETE /workflows/{id}` — remove from DB (does not unregister referenced scripts)

### Workflow Execution (`src/reaper/executor.cpp`)

- [ ] `Executor::run_workflow(workflow_id) → ExecutionLog`:
  - [ ] Fetch workflow from DB
  - [ ] Iterate steps:
    - `type: "action"` → post to command queue via `Main_OnCommand`
    - `type: "script"` → same (uses registered script's command ID)
    - `type: "query"` → capture state, no execution
    - `type: "condition"` → evaluate condition expression against current state; branch to `on_success` or `on_failure` step ID
  - [ ] Collect step results; write to `execution_history`
  - [ ] Update `workflows.last_executed`, increment `execution_count`

### Conditional Branching

- [ ] Condition expression: simple JSON path check, e.g. `"tracks[0].muted == false"`
- [ ] Evaluator: resolve JSON path in current state snapshot; compare with literal value
- [ ] `on_success` and `on_failure` are step IDs within the same workflow (or `"abort"` / `"continue"`)

### Cache Hints (Reuse Detection)

- [ ] On `GET /scripts/cache` and `GET /workflows`, include `execution_count` and `last_executed`
- [ ] When agent submits `POST /scripts/generate`, check `scripts` table for matching tags before calling LLM — return cached match hint in response: `"cache_hint": { "action_id": "...", "name": "..." }`

### Phase 3 Deliverable

- [ ] Save a "drum recording setup" workflow; execute it 10 times; all succeed
- [ ] Conditional branching skips steps correctly when track does not exist
- [ ] `execution_count` increments; `last_executed` updates correctly
- [ ] Cache hint returned when LLM generation would be redundant
- [ ] Push `main`; tag `v0.3.0`
- [ ] Document workflow JSON format in `docs/API.md`

---

## Phase 4: Integration & Hardening (Weeks 9–10)

**Goal:** Production-ready; optional MCP wrapper for OpenClaw/Sparky; multiple concurrent agent support; performance; security audit.

### Prerequisites

- [ ] Phase 3 complete and tagged

### Agent Identification

- [ ] Read `X-Agent-Id` header on all requests; store in `execution_history.agent_id`
- [ ] `GET /history?agent_id=sparky` — filter history by agent
- [ ] Per-agent rate limits (separate bucket per `X-Agent-Id` in addition to per-IP)

### Performance

- [ ] Profile all Phase 0–3 endpoints with a benchmarking tool (e.g., `wrk` or `k6`)
- [ ] Target: catalog search <50ms, state queries <100ms, action execution <200ms (excluding command queue wait)
- [ ] Add database indexes on `execution_history.executed_at`, `scripts.tags`, `workflows.tags`
- [ ] Cache hot state reads in memory with 1s TTL to reduce REAPER API call frequency

### Optional MCP Wrapper

- [ ] Design MCP tool definitions for:
  - `reaclawExecuteAction`
  - `reaclawQueryState`
  - `reaclawGenerateScript`
  - `reaclawExecuteWorkflow`
  - `reaclawSearchCatalog`
- [ ] Write `docs/MCP.md` explaining how to configure ReaClaw as an MCP server in OpenClaw

### Security Hardening

- [ ] Add `Strict-Transport-Security` header to all responses
- [ ] Validate all input sizes (script body, workflow steps array, intent string)
- [ ] Ensure script files are written only inside `{ResourcePath}/reaclaw/scripts/`; reject any path traversal in names
- [ ] Rate limit script generation separately from other endpoints
- [ ] Audit log: write security events (auth failures, rate limit hits, script warnings) to a dedicated log

### Error Recovery

- [ ] Script versioning: on `PUT /scripts/{id}`, keep previous version in DB; allow rollback to prior version

### Observability

- [ ] `GET /metrics` — expose request counts, error rates, average latency per endpoint, queue depth (Prometheus text format)
- [ ] Health check includes: DB connection, server thread alive, command queue depth

### Phase 4 Deliverable

- [ ] Load test: 10 concurrent agents making catalog searches — all <50ms
- [ ] MCP tool definitions documented; Sparky can call ReaClaw via MCP
- [ ] Security hardening checklist complete
- [ ] Push `main`; tag `v1.0.0`
- [ ] Write `docs/DEPLOYMENT.md` with platform-specific build and install instructions

---

## Ongoing (All Phases)

- [ ] Keep all unit tests passing before each commit
- [ ] Update `docs/API.md` as endpoints are added or changed
- [ ] Add `CHANGELOG.md` entry for each tagged release
- [ ] Respond to issues in GitHub; keep README accurate
- [ ] Keep `vendor/` library versions pinned and documented

---

## Success Criteria Summary

| Phase | Key criterion |
|---|---|
| 0 | Catalog indexed; action executed in REAPER via HTTP; HTTPS working |
| 1 | AI-generated script runs in REAPER; cached and reusable by ID |
| 2 | Agent verifies its own action effects; multi-step sequences with feedback |
| 3 | Saved workflows execute reliably; agent reuses instead of regenerating |
| 4 | 10 concurrent agents; MCP integration; production-hardened |

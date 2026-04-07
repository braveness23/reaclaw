# ReaClaw: Implementation Checklist

Break each phase into concrete, shippable tasks.

---

## Phase 0: MVP (Weeks 1-2)

### Goal
Basic API that queries Reaper's action catalog and executes actions via HTTP.

### Tasks

#### Core Server Setup
- [ ] Initialize Go project: `go mod init github.com/braveness23/reclaw`
- [ ] Set up project structure (`cmd/`, `pkg/`, `tests/`, etc.)
- [ ] Create `main.go` with HTTP server scaffold
- [ ] Choose HTTP router library (`chi` or `gin`); add dependency
- [ ] Implement graceful shutdown

#### Database Layer
- [ ] Create SQLite schema for:
  - `actions` (id, name, category, tags, description, reaper_command_id, created_at)
  - `execution_history` (timestamp, action_id, status, result, error)
- [ ] Write database initialization code (create tables if not exist)
- [ ] Create `pkg/db/` package with CRUD operations
- [ ] Write basic tests for DB operations

#### Reaper Integration (Web Interface)
- [ ] Verify Reaper's Web Interface is working locally (`http://localhost:8808`)
- [ ] Write code to query Reaper's action list (parse web interface response)
- [ ] Extract action metadata (ID, name, category)
- [ ] Store in SQLite `actions` table

#### Action Catalog Endpoints
- [ ] `GET /catalog` — Return full action list from DB
- [ ] `GET /catalog/search?q=query` — Fuzzy search by name/tag
- [ ] `GET /catalog/categories` — Return unique categories + counts
- [ ] Implement basic query/search logic
- [ ] Add response pagination (limit, offset)

#### State Query Endpoints
- [ ] `GET /state` — High-level project state (BPM, cursor, transport)
- [ ] `GET /state/tracks` — Track list with properties
- [ ] Parse Reaper's web interface responses into JSON
- [ ] Implement caching (5s TTL) to avoid hammering Reaper

#### Action Execution Endpoint
- [ ] `POST /execute/action` — Execute single action by ID
- [ ] Send command to Reaper via Web Interface
- [ ] Log execution to database
- [ ] Return success/failure + updated state

#### Configuration
- [ ] Create `config.yaml` template with sensible defaults:
  - Server host/port (default: 0.0.0.0:9091)
  - Reaper Web Interface URL (default: http://localhost:8808)
  - Database path (default: reaclawdb.sqlite)
- [ ] Load config on startup (override with env vars)
- [ ] Add logging (structured logs to stdout/file)

#### Security (MVP Minimal)
- [ ] Add TLS support (self-signed cert generation)
  - Generate cert if missing: `openssl req -x509 -newkey rsa:4096 ...`
  - Or use Go crypto/tls to generate programmatically
- [ ] Add optional API key auth (bearer token in Authorization header)
- [ ] Add basic rate limiting (60 requests/min per IP)

#### Documentation
- [ ] Write API.md with endpoint signatures (Phase 0 only)
- [ ] Add example curl requests for each endpoint
- [ ] Write quick-start guide in README

#### Testing
- [ ] Write unit tests for catalog search logic
- [ ] Write integration tests (mock Reaper responses)
- [ ] Test TLS cert generation
- [ ] Test API key auth

#### Deliverable
- [ ] Push to `main` branch
- [ ] Tag: `v0.0.1`
- [ ] Readme documents how to run: `go build && ./reclaw`
- [ ] All Phase 0 endpoints working and tested

---

## Phase 1: Script Generation & Registration (Weeks 3-4)

### Goal
Claude (or other LLMs) can generate Lua ReaScripts dynamically and register them as Reaper actions.

### Tasks

#### Script Generation Endpoint
- [ ] `POST /scripts/generate` — Request AI to generate script
  - Input: intent, context (track info, available plugins)
  - Calls Claude API (or configured LLM)
  - Returns Lua code + preview
- [ ] Create `pkg/script/generator.go` module
- [ ] Implement prompt engineering for common patterns (parallel comp, sidechain, etc.)
- [ ] Add error handling for LLM failures

#### Script Validation
- [ ] Lua syntax validation:
  - Call `lua -c` on generated script (or use Lua parser library)
  - Return line numbers + error messages to agent
- [ ] Static analysis (linting):
  - Detect undefined Reaper API calls
  - Warn on suspicious patterns (shell execution, filesystem access)
  - Create `pkg/script/analyzer.go`
- [ ] Size limits (max 5MB per script)

#### Script Registration Endpoint
- [ ] `POST /scripts/register` — Register validated script as custom action
  - Write script to `reaper_scripts/` folder
  - Call Reaper Web Interface to register as custom action
  - Get back action ID (e.g., `_parallel_comp_abc123`)
  - Store in SQLite `scripts` table
- [ ] Idempotent: If same script registered twice, return existing ID
- [ ] Ensure unique action IDs (append hash of script content)

#### Script Database Schema
- [ ] Extend SQLite schema:
  - `scripts` (id, action_id, name, body, language, generated_by, created_at, execution_count)
  - `script_errors` (script_id, error_message, resolved_at)
- [ ] Create migration if needed

#### Script Cache Endpoint
- [ ] `GET /scripts/cache` — List all generated + cached scripts
- [ ] `GET /scripts/{action_id}` — Retrieve script source
- [ ] Include execution count, last run, errors

#### Lua Templating System
- [ ] Create common script templates:
  - `parallel_compression.lua`
  - `sidechain_setup.lua`
  - `drum_sample_replace.lua`
  - `vocal_chain.lua`
- [ ] Allow agent to request template + customization params
- [ ] Store templates in repo: `pkg/script/templates/`

#### Testing
- [ ] Unit tests for syntax validation
- [ ] Unit tests for static analysis (positive/negative cases)
- [ ] Integration test: Generate → Validate → Register → Execute
- [ ] Test template substitution

#### Documentation
- [ ] Update API.md with `/scripts/generate`, `/scripts/register`, `/scripts/cache`
- [ ] Add example workflows in EXAMPLES.md:
  - "Generate parallel compression script"
  - "Cache and reuse script"
- [ ] Document prompt engineering approach

#### Deliverable
- [ ] Push to `main` branch
- [ ] Tag: `v0.1.0`
- [ ] Claude can successfully generate and execute a custom script
- [ ] Scripts are cached and reusable

---

## Phase 2: Feedback Loops & State Verification (Weeks 5-6)

### Goal
Agents verify that actions worked; support multi-step sequences with feedback between steps.

### Tasks

#### Multi-Step Execution Endpoint
- [ ] `POST /execute/sequence` — Execute actions in order
  - Input: list of actions, feedback_between_steps flag
  - After each action, query state
  - Return: step-by-step results
- [ ] Implement conditional branching (if state X, execute Y)
- [ ] Add error recovery (on_failure action)

#### Verification Endpoint
- [ ] `POST /verify` — Check if action had expected effect
  - Input: action_id, expected state
  - Query actual state
  - Compare: matches expected?
  - Return: verified boolean + discrepancies
- [ ] Create `pkg/verify/` module with comparison logic

#### State Snapshots
- [ ] Before/after action execution, capture full state snapshot
- [ ] Store in SQLite: `state_snapshots` table
- [ ] Enable agents to see what changed

#### Feedback Loop Example
- [ ] Agent: "Mute all drum tracks"
  - ReaClaw: GET /state/tracks → ["Kick", "Snare", "Hihat", "Kick drum"]
  - Agent: Identify drum tracks
  - For each: POST /execute/action {id: 40285, track: X}
  - After each: POST /verify {expected: {muted: true}}
  - Agent sees: "Track 0 muted ✓, Track 1 muted ✓, ..."

#### Query Endpoints (Enhanced)
- [ ] Improve `/state/*` endpoints for better feedback:
  - `/state/tracks` should include: mute state, solo, armed, FX, routing
  - `/state/selection` — Current selection context
  - `/state/automation` — Automation envelopes
- [ ] Add caching + cache invalidation (on action execution)

#### ReaClaw Bridge Script (Optional for Phase 2)
- [ ] If web interface proves limiting, create startup script:
  - `__reaclawbridge.lua` in Reaper scripts folder
  - Exposes richer state query endpoints (optional HTTP server)
  - Handles complex operations web interface can't do
- [ ] Defer if MVP web interface is sufficient

#### Execution History Enhancement
- [ ] Extend execution_history table:
  - state_before (JSON snapshot)
  - state_after (JSON snapshot)
  - verification_result (passed/failed)
- [ ] `GET /history` — Full execution trace with diffs

#### Testing
- [ ] Unit tests for state comparison logic
- [ ] Integration test: Multi-step sequence with feedback
- [ ] Test conditional branching
- [ ] Test rollback scenario (failed step)

#### Documentation
- [ ] Update API.md with `/execute/sequence`, `/verify`
- [ ] Add example: "Record drums with sidechain (5-step feedback loop)"
- [ ] Document state snapshot format

#### Deliverable
- [ ] Push to `main` branch
- [ ] Tag: `v0.2.0`
- [ ] Agents can verify their own actions
- [ ] Multi-step workflows with conditional branching work reliably

---

## Phase 3: Workflows & Macro System (Weeks 7-8)

### Goal
Save reusable workflows; agents cache and reuse them instead of regenerating.

### Tasks

#### Workflow CRUD Endpoints
- [ ] `POST /workflows` — Create workflow
  - Input: name, description, steps array, tags
  - Store in SQLite: `workflows` table
  - Return workflow_id
- [ ] `GET /workflows` — List all workflows
- [ ] `GET /workflows/{id}` — Get workflow details
- [ ] `PUT /workflows/{id}` — Update workflow
- [ ] `DELETE /workflows/{id}` — Delete workflow

#### Workflow Execution Endpoint
- [ ] `POST /workflows/{id}/execute` — Run saved workflow
  - Fetch workflow from DB
  - Execute steps in order
  - Apply conditions + branches
  - Log execution to `workflow_executions` table
  - Return execution log with all step results

#### Workflow Database Schema
- [ ] `workflows` (id, name, description, creator, tags, created_at, last_executed, idempotent)
- [ ] `workflow_steps` (id, workflow_id, step_number, type, target, params, condition, on_success, on_failure)
- [ ] `workflow_executions` (id, workflow_id, started_at, status, steps_completed, total_steps, result_json)

#### Learning System (Agent Memory)
- [ ] Track which workflows agents use most
- [ ] When agent asks to accomplish task X:
  - Check: Is there a cached workflow that does this?
  - Return: action_id for reuse (skip regeneration)
- [ ] Agents can query: `/workflows?tag=drums` or `/workflows?tag=automation`
- [ ] Store in `agent_context` table:
  - agent_id, workflow_id, last_used_at, use_count

#### Workflow Templates
- [ ] Create common workflow templates:
  - `drum_recording_setup.yaml` (routing, compression, arming)
  - `vocal_chain.yaml` (EQ, compression, reverb)
  - `sidechain_routing.yaml`
- [ ] Store in repo: `workflows/templates/`
- [ ] Allow agents to clone template → customize → execute

#### Conditional Branching in Workflows
- [ ] Implement if/then in workflow steps:
  - Condition: "if track_count > 4"
  - on_success: next step ID
  - on_failure: alternative step ID
- [ ] Example: "If drums exist, route to sidechain; else skip"

#### Testing
- [ ] Unit tests for workflow parsing
- [ ] Integration test: Save workflow → Execute → Verify
- [ ] Test conditional branching in workflow
- [ ] Test agent memory (reuse detection)

#### Documentation
- [ ] Update API.md with workflow endpoints
- [ ] Add example: Save "drum recording" workflow, reuse 10 times
- [ ] Document workflow YAML format
- [ ] Create workflow template examples

#### Deliverable
- [ ] Push to `main` branch
- [ ] Tag: `v0.3.0`
- [ ] Agents save workflows instead of regenerating
- [ ] Workflow reuse reduces API calls by 80%+

---

## Phase 4: Integration & Distributed Agents (Weeks 9-10)

### Goal
ReaClaw works seamlessly with OpenClaw/Sparky (optional); supports remote agents; optimized for scale.

### Tasks

#### OpenClaw/Sparky Integration (Optional)
- [ ] Expose ReaClaw as MCP server (optional):
  - Tool: `reaclawExecuteAction`
  - Tool: `reaclawGenerateScript`
  - Tool: `reaclawExecuteWorkflow`
  - Tool: `reaclawQueryState`
- [ ] If using Sparky, Sparky can call ReaClaw directly
- [ ] Not required; ReaClaw works standalone

#### Distributed Agent Support
- [ ] Agent identification: Add `agent_id` to all requests
  - Header: `X-Agent-Id: claude-sonnet-4`
  - Or param: `?agent_id=sparky-v1`
- [ ] Track per-agent statistics: workflows used, scripts generated, etc.
- [ ] Enable agents to query their own history: `/agent/{agent_id}/history`

#### PostgreSQL Migration Path
- [ ] Write migration script: SQLite → PostgreSQL
  - Dump schema + data
  - Load into PostgreSQL
  - Automatic backup before migration
- [ ] Update code to support both SQLite + PostgreSQL (configurable)
- [ ] Document migration process

#### Performance Optimization
- [ ] Profile API endpoints (measure latency)
- [ ] Optimize common paths:
  - Catalog search (add database indexes)
  - State queries (caching strategy)
  - Script registration (parallel validation)
- [ ] Batch operations: `/execute/batch` for multiple actions
- [ ] Async script generation (don't block on LLM calls)

#### Error Recovery Patterns
- [ ] Workflow rollback: Implement undo for failed steps
  - Store reverse_action for each step
  - On failure, execute reverse actions in reverse order
- [ ] Script versioning: Keep old script versions for fallback
- [ ] Retry logic: Configurable retry on transient failures

#### Monitoring & Observability
- [ ] Add structured logging (JSON logs)
- [ ] Metrics:
  - Request rate (per endpoint, per agent)
  - Error rate (by type)
  - Script generation success rate
  - Workflow execution time
- [ ] Health endpoint: `GET /health` (for monitoring)

#### Security Hardening
- [ ] Rate limiting: Implement per-agent limits (not just per-IP)
- [ ] Action whitelist: Option to disable dangerous actions
- [ ] mTLS support (enhanced from Phase 0)
- [ ] Audit logging enhancements (detailed security events)

#### Testing
- [ ] Load test: 100 concurrent agents
- [ ] PostgreSQL integration tests
- [ ] Workflow rollback tests
- [ ] Error recovery tests

#### Documentation
- [ ] Update DESIGN.md with distributed scenarios
- [ ] Add deployment guide (single machine, multi-machine)
- [ ] Write operational runbook (monitoring, backups, recovery)
- [ ] Create troubleshooting guide

#### Deliverable
- [ ] Push to `main` branch
- [ ] Tag: `v0.4.0` (or v1.0.0 if feature-complete)
- [ ] Production-ready (monitoring, error recovery, distributed support)
- [ ] Multiple agents can operate simultaneously
- [ ] PostgreSQL upgrade path available

---

## Ongoing Tasks (All Phases)

- [ ] Keep tests passing (unit + integration)
- [ ] Update README as features ship
- [ ] Create GitHub releases with changelog
- [ ] Security review of new endpoints
- [ ] Performance monitoring

---

## Success Criteria (MVP → Full Vision)

**Phase 0 (MVP):**
- ✓ Catalog search works; <200ms latency
- ✓ Action execution works; state queries accurate
- ✓ TLS + auth functional

**Phase 1:**
- ✓ Claude generates working scripts
- ✓ 90%+ first-run success rate for generated scripts
- ✓ Script caching avoids redundant generation

**Phase 2:**
- ✓ Agents verify actions; catch failures
- ✓ Multi-step workflows work reliably
- ✓ Execution history enables debugging

**Phase 3:**
- ✓ Agents reuse workflows; no regeneration
- ✓ 80%+ cache hit rate after 10 uses
- ✓ Workflow templates reduce boilerplate

**Phase 4:**
- ✓ Multiple agents operate simultaneously
- ✓ PostgreSQL option available
- ✓ Error recovery works; rollback on failure
- ✓ Production monitoring in place

---

## Notes

- Each phase should be **shippable and tested** before moving to the next
- Don't skip phases; each builds on the previous
- If a feature isn't working, fix it before moving on
- Keep the design document updated as you build
- Consider creating a `CHANGELOG.md` for each release

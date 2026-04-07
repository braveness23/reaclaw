# ReaClaw: Full Vision Design

## 1. Executive Summary

ReaClaw is a programmable API layer that bridges AI agents (Claude, or any compatible LLM) to REAPER's action system and ReaScript runtime. It enables agents to:

- Query Reaper's current state and action catalog
- Execute built-in actions or custom action sequences
- Generate, register, and execute new ReaScripts dynamically
- Cache and reuse generated workflows
- Provide feedback loops for verification and iteration

ReaClaw is **agent-agnostic**, **platform-independent**, and designed for complex, stateful workflows—not just single-action calls.

---

## 2. Architecture Overview

### 2.1 Component Stack

```
┌─────────────────────────────────────────────────────────┐
│ AI Agents (Claude, other LLMs)                          │
│ (Local or remote, any provider)                         │
└────────────────┬────────────────────────────────────────┘
                 │ HTTP/REST
                 ▼
┌─────────────────────────────────────────────────────────┐
│ ReaClaw API Server                                      │
│ ├─ Action Catalog Service                              │
│ ├─ State Query Service                                 │
│ ├─ Script Generation & Registration                    │
│ ├─ Action Execution Engine                             │
│ ├─ Caching & Persistence Layer                         │
│ └─ Feedback & Verification System                      │
└────────────────┬────────────────────────────────────────┘
                 │ Local Socket/HTTP (Reaper Web Control)
                 ▼
┌─────────────────────────────────────────────────────────┐
│ REAPER                                                  │
│ ├─ Web Interface (built-in HTTP server)                │
│ ├─ ReaScript Runtime (Lua/Python/EEL2)                │
│ ├─ Action System (65K+ commands)                       │
│ └─ Project State (tracks, items, automation, etc.)     │
└─────────────────────────────────────────────────────────┘
```

### 2.2 Data Flow: Simple Action Call

```
Agent: "Mute track 1"
  → ReaClaw: GET /actions?query=mute
  → Returns: [{"id": 40285, "name": "Mute track", "params": [...]}]
  → ReaClaw: POST /execute/action {"id": 40285, "params": {"track_index": 0}}
  → Reaper: Executes action 40285
  → ReaClaw: GET /state/tracks → Returns current track state
  → Agent: Gets feedback, verifies mute worked
```

### 2.3 Data Flow: Complex Workflow (Script Generation)

```
Agent: "Create parallel compression on this track"
  → ReaClaw: POST /scripts/generate {"intent": "parallel_compression", "context": {...}}
  → ReaClaw LLM: Generates Lua ReaScript
  → ReaClaw: POST /scripts/register {"name": "parallel_comp_track1", "body": "...lua..."}
  → Reaper: Saves script, registers as custom action _parallel_comp_track1
  → ReaClaw: Returns {"action_id": "_parallel_comp_track1", "cached": true}
  → Agent: Caches action_id for reuse
  → ReaClaw: POST /execute/action {"id": "_parallel_comp_track1"}
  → Feedback loop: GET /state → verify FX added, routing set, etc.
```

---

## 3. API Specification

### 3.1 Core Endpoints

#### 3.1.1 Action Catalog

**GET `/catalog`**
- Returns full action catalog with metadata
- Cached on startup, indexed by ID and tags
- Response:
```json
{
  "total": 65234,
  "indexed_at": "2025-04-06T14:23:45Z",
  "actions": [
    {
      "id": 40285,
      "name": "Mute track",
      "category": "Track Management",
      "tags": ["track", "mute", "silence"],
      "description": "Mute the selected track",
      "params": []
    }
  ]
}
```

**GET `/catalog/search?q=query`**
- Full-text search with fuzzy matching
- Query by name, tag, category
- Response: subset of catalog matching query
- Example: `/catalog/search?q=envelope+automation` → automation actions with envelope in name

**GET `/catalog/categories`**
- Returns available categories and count of actions per category
- Used for high-level navigation

#### 3.1.2 State Queries

**GET `/state`**
- Current Reaper project state (high-level summary)
- Response:
```json
{
  "project": {
    "name": "MyProject.rpp",
    "bpm": 120,
    "time_signature": "4/4",
    "cursor_position": 15.5
  },
  "selection": {
    "selected_tracks": [0, 2, 5],
    "selected_items": [1, 3],
    "selected_take": 0
  },
  "transport": {
    "playing": false,
    "recording": false,
    "paused": false
  }
}
```

**GET `/state/tracks`**
- Detailed track list with names, properties, FX chain
- Response:
```json
{
  "tracks": [
    {
      "index": 0,
      "name": "Kick",
      "muted": false,
      "soloed": false,
      "armed": true,
      "fx_chain": [
        {"slot": 0, "name": "ReaComp", "enabled": true}
      ]
    }
  ]
}
```

**GET `/state/items`**
- Media items in current take/track
- Includes position, length, source, takes

**GET `/state/automation`**
- Automation envelopes for selected track/parameter
- Returns points, modes, etc.

#### 3.1.3 Action Execution

**POST `/execute/action`**
- Execute a single action by ID
- Request:
```json
{
  "id": 40285,
  "track_index": 0,
  "params": {},
  "feedback": true
}
```
- Response:
```json
{
  "status": "success",
  "action_id": 40285,
  "executed_at": "2025-04-06T14:24:10Z",
  "feedback": {
    "track_muted": true,
    "current_state": {...}
  }
}
```

**POST `/execute/sequence`**
- Execute multiple actions in order with conditional branching
- Request:
```json
{
  "actions": [
    {"id": 40285, "params": {"track_index": 0}},
    {"id": 1007}
  ],
  "feedback_between_steps": true
}
```
- Returns status after each step; agent can inspect and decide whether to continue

#### 3.1.4 Script Management

**POST `/scripts/generate`**
- Request Claude/LLM to generate a ReaScript based on intent
- Request:
```json
{
  "intent": "Create parallel compression on track 1",
  "context": {
    "selected_track": 0,
    "available_plugins": ["ReaComp", "ReaEQ"],
    "track_name": "Drums"
  },
  "language": "lua",
  "template": "optional_template_name"
}
```
- Response:
```json
{
  "script": "reaper.InsertTrackAtIndex(...)\n...",
  "language": "lua",
  "preview": "Adds ReaComp to track 1, enables parallel return routing",
  "estimated_complexity": "medium"
}
```

**POST `/scripts/register`**
- Register a generated ReaScript as a custom action
- Request:
```json
{
  "name": "parallel_comp_drums",
  "body": "reaper.InsertTrackAtIndex...",
  "language": "lua",
  "category": "Custom/Automation",
  "tags": ["compression", "parallel", "automation"],
  "idempotent": true
}
```
- Response:
```json
{
  "action_id": "_parallel_comp_drums_abc123",
  "registered_at": "2025-04-06T14:24:50Z",
  "cached": true,
  "callable_immediately": true
}
```

**GET `/scripts/cache`**
- List all generated/cached scripts with their action IDs
- Useful for agents to check if a workflow already exists

**GET `/scripts/{action_id}`**
- Retrieve source code of a cached script
- Allows agent to understand what was generated previously

#### 3.1.5 Workflow & Macro Management

**POST `/workflows`**
- Define and save a workflow (sequence of actions/scripts + decision logic)
- Request:
```json
{
  "name": "drum_chain_sidechain_record",
  "description": "Route kick to sidechain, mute drums except hihat, record",
  "steps": [
    {"action": "query_state"},
    {"action": 40285, "params": {"track": "kick"}, "condition": "if_exists"},
    {"action": "_setup_sidechain", "track": 0},
    {"action": 1013}
  ],
  "tags": ["drums", "production", "recording"],
  "idempotent": false
}
```
- Response: Workflow saved with ID, can be called repeatedly

**POST `/workflows/{workflow_id}/execute`**
- Execute a saved workflow
- Returns step-by-step execution log

**GET `/workflows`**
- List all saved workflows

#### 3.1.6 Feedback & Verification

**POST `/verify`**
- Verify that an action had the expected effect
- Request:
```json
{
  "action_id": 40285,
  "expected": {"track_muted": true, "selected_track": 0},
  "tolerance": "strict"
}
```
- Response:
```json
{
  "verified": true,
  "actual": {"track_muted": true, "selected_track": 0},
  "discrepancies": []
}
```

**GET `/history`**
- Execution history (last N actions/workflows executed)
- Useful for debugging and agent reasoning

---

## 4. Data Models

### 4.1 Action Model

```
Action {
  id: int | string,                    // Numeric for Reaper natives, _xxx for custom
  name: string,                        // "Mute track"
  category: string,                    // "Track Management"
  tags: string[],                      // ["track", "mute", "silence"]
  description: string,                 // Human-readable
  parameters: Parameter[],             // Required params, if any
  side_effects: string[],              // "Modifies selection", "Plays audio", etc.
  reverse_action_id?: int | string,    // Undo equivalent (if known)
  requires_ui_focus?: boolean,         // Some actions need focus
  reaper_command_id: int,              // Raw Reaper ID for execution
  created_at: timestamp,               // When registered
  generated_by?: string                // "claude-sonnet-4" if agent-generated
}

Parameter {
  name: string,                        // "track_index"
  type: "int" | "string" | "bool",
  required: boolean,
  default?: any,
  description: string
}
```

### 4.2 Script Model

```
Script {
  id: string,                          // Unique identifier
  action_id: string,                   // Registered Reaper action ID
  name: string,                        // "parallel_comp_drums"
  language: "lua" | "python" | "eel2",
  body: string,                        // Full script source
  generated_by: string,                // "claude-sonnet-4"
  generation_intent: string,           // "Create parallel compression"
  created_at: timestamp,
  last_executed: timestamp,
  execution_count: int,
  tags: string[],
  idempotent: boolean,                 // Safe to run multiple times?
  dependencies: string[],              // Other script IDs it depends on
  error_history: ExecutionError[]      // Errors and fixes applied
}

ExecutionError {
  timestamp: timestamp,
  error: string,
  context: object,
  agent_response: string,              // How agent fixed it
  resolved: boolean
}
```

### 4.3 Workflow Model

```
Workflow {
  id: string,
  name: string,
  description: string,
  creator: string,                     // "claude-sonnet-4" or user
  steps: WorkflowStep[],
  tags: string[],
  idempotent: boolean,
  created_at: timestamp,
  last_executed: timestamp,
  execution_history: ExecutionLog[]
}

WorkflowStep {
  id: string,                          // Step ID for feedback reference
  type: "action" | "script" | "query" | "condition" | "branch",
  target: int | string,                // Action/script ID
  params?: object,
  condition?: string,                  // "if track_exists('drums')"
  feedback_required?: boolean,         // Wait for verification?
  on_success?: string,                 // Next step ID
  on_failure?: string                  // Fallback step
}

ExecutionLog {
  workflow_id: string,
  started_at: timestamp,
  steps_completed: int,
  total_steps: int,
  status: "success" | "partial" | "failed",
  steps: {step_id: StepResult}
}

StepResult {
  action_id: int | string,
  executed_at: timestamp,
  status: "success" | "failed" | "skipped",
  result: object,                      // Return value or state change
  error?: string,
  feedback: object                     // Verification result
}
```

### 4.4 Agent Context Model

```
AgentContext {
  agent_id: string,                    // "claude-sonnet-4", "sparky-v1", etc.
  session_id: string,                  // For tracking multi-turn interactions
  action_cache: {
    [workflow_name]: {
      action_id: string,
      generated_at: timestamp,
      execution_count: int
    }
  },
  learned_patterns: string[],          // "If user wants parallel comp, use workflow X"
  execution_history: ExecutionLog[],   // Agent's recent actions
  preferences: object                  // Agent-specific settings
}
```

---

## 5. Implementation Phases

### Phase 0: Foundation & MVP (Weeks 1-2)

**Deliverables:**
- ReaClaw server boilerplate (Rust/Go/Python, TBD)
- Static action catalog extracted from Reaper
- `/catalog`, `/catalog/search` endpoints
- `/state` and `/state/tracks` endpoints
- `/execute/action` for single action calls
- Web Control integration (query existing Reaper web interface)
- Basic in-memory caching

**Success Criteria:**
- Claude can query the full action catalog
- Claude can execute basic actions (mute, play, etc.)
- Reaper state queries work reliably
- <200ms roundtrip latency

### Phase 1: Script Generation & Registration (Weeks 3-4)

**Deliverables:**
- `/scripts/generate` endpoint (calls Claude API from inside ReaClaw)
- `/scripts/register` endpoint (writes to disk, registers with Reaper)
- Script templating system (parallel comp, sidechain, EQ, etc.)
- Lua/Python validation before registration
- Script cache layer (SQLite or similar)

**Success Criteria:**
- Claude generates a working parallel comp script
- Script registers and executes in Reaper
- Generated scripts are cached and reusable
- Error handling for syntax errors

### Phase 2: State Management & Feedback Loops (Weeks 5-6)

**Deliverables:**
- `/execute/sequence` for multi-step workflows
- `/verify` endpoint for post-execution verification
- Execution history tracking
- State snapshots before/after actions
- Feedback loop: agent generates → executes → verifies → iterates

**Success Criteria:**
- Complex 5-action sequences work reliably
- Verification catches when things go wrong
- Agent can reason about state changes

### Phase 3: Workflow & Macro System (Weeks 7-8)

**Deliverables:**
- `/workflows` CRUD endpoints
- Workflow step branching and conditionals
- Workflow caching and reuse
- Agent learns when to reuse workflows vs. generate new ones

**Success Criteria:**
- Save a "drum recording setup" workflow, call it 10 times
- Agent knows not to regenerate same workflow repeatedly

### Phase 4: Integration & Optimization (Weeks 9-10)

**Deliverables:**
- OpenClaw/Sparky integration layer
- Distributed tracing (which agent called what when)
- Performance optimization (batch action calls, async execution)
- Error recovery patterns
- Security hardening (rate limits, auth if distributed)

**Success Criteria:**
- ReaClaw works seamlessly with OpenClaw infrastructure
- Agent can reason about its own previous actions
- Multi-agent scenarios work (Sparky + other agents)

---

## 6. Tech Stack Decisions

### 6.1 Server Language

**Options:**
1. **Rust** — Type safety, performance, async-first. Best for distributed use.
2. **Go** — Simplicity, fast compilation, good concurrency. Lighter than Rust.
3. **Python** — Fast iteration, easy integration with LLM APIs, but slower.

**Recommendation: Go**
- Good balance of speed and simplicity
- Easy HTTP/REST handling
- Can spawn ReaScript processes efficiently
- Lower operational overhead than Rust for this use case
- Python integration via os.exec if needed

### 6.2 Persistence Layer

**Options:**
1. **SQLite** — Single-file, no server, perfect for local use
2. **PostgreSQL** — Overkill for MVP, but scales if multi-agent
3. **JSON files + Git** — Version control for workflows/scripts, but slower queries

**Recommendation: SQLite for MVP, plan PostgreSQL for Phase 4 (if distributed)**
- Fast reads for action catalog
- Reliable caching of generated scripts
- Easy to backup and version
- Schema: `actions`, `scripts`, `workflows`, `execution_logs`, `agent_context`

### 6.3 Script Validation

**Before registering a ReaScript:**
- Lua syntax check (lua -c)
- Static analysis for obvious errors (undefined vars, wrong API calls)
- Sandbox test run in isolated Reaper instance (if possible)
- Fallback: human approval queue for first-run scripts

### 6.4 Integration with Reaper

**How does ReaClaw call Reaper?**

**Option A: Reaper Web Interface (built-in)**
- Use Reaper's HTTP server on port 8808 (configurable)
- Send commands via `/_/action_id` syntax
- Pros: No plugins, already in Reaper
- Cons: Limited query capabilities, web interface limitations

**Option B: Custom ReaScript "Bridge"**
- ReaClaw-aware ReaScript running in Reaper
- Exposes richer API via local socket or HTTP
- Pros: Full Reaper API access, flexible
- Cons: Requires script in Reaper

**Recommendation: Hybrid**
- Phase 0-1: Use Reaper Web Interface for simple actions
- Phase 2+: Deploy a "ReaClaw Bridge" ReaScript in Reaper that exposes richer state queries and script registration

The Bridge script:
```lua
-- __reaclawbridge.lua (startup script)
function reaclawbridge_register_script(name, body)
  -- Writes script to disk
  -- Registers with Reaper
  -- Returns action ID
end

function reaclawbridge_get_state()
  -- Returns full project state as JSON
end
```

---

## 7. Integration Points

### 7.1 Generic Agent Integration

ReaClaw is **agent-agnostic**. Any AI system that speaks HTTP can use it:

- **Claude (via Artifacts or CLI)**
- **OpenAI, Anthropic, or other LLM providers**
- **Sparky/OpenClaw (optional integration)**
- **Custom scripts or bots**
- **Humans via REST clients (curl, Postman)**

Example:
```
Agent (any type): POST /execute/action {"id": 40285}
ReaClaw: Returns {"status": "success", ...}
```

### 7.2 Optional: OpenClaw/Sparky Integration

If you want ReaClaw as part of your OpenClaw stack, expose it as an MCP tool:

```
ReaClaw MCP Server (optional):
- Tool: reaclawExecuteAction
- Tool: reaclawGenerateScript
- Tool: reaclawExecuteWorkflow
- Tool: reaclawQueryState
```

This is purely *optional* and doesn't require any OpenClaw dependencies.

### 7.3 Execution History for Agent Learning

ReaClaw maintains execution history so any agent can review what was done:

```
GET /history?agent_id=claude-sonnet&limit=20

Response:
{
  "executions": [
    {"timestamp": "2025-04-06T14:24:10Z", "action_id": 40285, "params": {}, "status": "success"},
    {"timestamp": "2025-04-06T14:24:50Z", "script_id": "_parallel_comp_xyz", "status": "success"}
  ]
}
```

Agents can independently decide to:
- Cache results in their own memory system
- Reuse previous workflows by ID
- Learn patterns from execution history

---

## 8. Security & Authentication

### 8.1 HTTPS Configuration

ReaClaw supports both **self-signed** and **certificate authority-signed** certificates:

**Self-Signed (Development/Local Network):**
```
reaclawconfig.yaml:
tls:
  enabled: true
  cert_file: "certs/self_signed.crt"
  key_file: "certs/self_signed.key"
  generate_if_missing: true
```

On startup, if cert/key don't exist, ReaClaw generates them automatically.

**Signed Certificates (Production):**
```
tls:
  enabled: true
  cert_file: "/etc/letsencrypt/live/reaclawmusic.com/fullchain.pem"
  key_file: "/etc/letsencrypt/live/reaclawmusic.com/privkey.pem"
```

### 8.2 Authentication

**Option A: API Key (Simple)**
```
reaclawconfig.yaml:
auth:
  type: "api_key"
  key: "sk_your_secret_key_here"
```

All requests must include header: `Authorization: Bearer sk_your_secret_key_here`

**Option B: mTLS (Secure)**
```
tls:
  enabled: true
  require_client_cert: true
  client_ca: "certs/client_ca.crt"
```

Client must present valid certificate. Agents provide their cert.

**Option C: None (Trusted Network Only)**
```
auth:
  type: "none"
```

Assumes ReaClaw is on a private network. Fast, simple.

**Recommendation:** 
- **MVP/local:** No auth
- **Home network:** Self-signed cert + API key
- **Remote access:** Self-signed cert + API key, or mTLS if you want it tighter

### 8.3 Rate Limiting

Prevent abuse (DoS, runaway agents):

```
rate_limiting:
  enabled: true
  per_ip:
    requests_per_minute: 60
    scripts_per_minute: 5
  per_key:
    requests_per_minute: 120
    scripts_per_minute: 10
```

### 8.4 Script Trust Model

**Built-in & Community Scripts (Reaper Core):**
- Fully trusted
- Execute without validation
- Examples: "Mute track", "Play", actions from SWS extension

**User-Created ReaScripts:**
- Trusted (you created them)
- Execute without validation

**Agent-Generated Scripts (Claude, etc.):**
- **Syntax validation required** (Lua parser, check for errors)
- **Static analysis recommended** (warn on suspicious patterns):
  - Undefined Reaper API calls
  - Accessing filesystem outside project directory
  - Network calls (suspicious)
  - Shell execution (`os.execute`, `ExecProcess`)
- **Optional human approval** (before execution):
  - Config option to require approval for generated scripts
  - Agent can request `/scripts/preview` to show human a diff
- **Execution sandboxing:** None (trust Reaper's own sandbox)
- **Detailed error logging:** Every generated script execution is logged with full source

**Configuration:**
```
script_security:
  validate_syntax: true              # Always check Lua syntax
  static_analysis: true              # Warn on suspicious patterns
  require_approval: false             # Set to true for extra caution
  approval_timeout_hours: 24
  log_all_executions: true            # Keep audit trail
  max_script_size_mb: 5
```

### 8.5 Audit Logging

All actions logged to `reaclawaudit.log`:
```
2025-04-06T14:24:10Z | ACTION | agent=claude-sonnet | action_id=40285 | status=success
2025-04-06T14:24:50Z | SCRIPT_EXEC | agent=claude-sonnet | script_id=_parallel_comp | status=success
2025-04-06T14:25:30Z | SCRIPT_EXEC | agent=claude-sonnet | script_id=_unknown_script | status=failed | error=syntax_error
```

---

## 9. Script Trust & Validation Details

### 9.1 Syntax Validation

Before registering any agent-generated script:

```
# Lua syntax check (built-in Lua compiler)
lua -c script.lua

# If fails: Return error to agent with line number
# If passes: Proceed to static analysis
```

### 9.2 Static Analysis (Recommended but Not Blocking)

Warning patterns for agent-generated scripts:

```
WARN: Undefined API call: reaper.GetTrack_TYPO()
WARN: Suspicious filesystem access: /etc/passwd
WARN: Network call detected: tcp_connect()
WARN: Shell execution: os.execute('rm -rf')
```

These are **warnings**, not failures. Agent sees them, can fix if needed.

### 9.3 Preview Before Execution

Agent can request a preview of what the script will do:

```
POST /scripts/preview
{
  "script": "reaper.InsertTrackAtIndex(1, true); ...",
  "language": "lua"
}

Response:
{
  "operations": [
    "Insert track at index 1",
    "Get track and set name to 'aux'",
    "Render/bounce (suspicious)"
  ],
  "warnings": ["Possible file deletion operation"],
  "safe_to_execute": false
}
```

### 9.4 Execution History & Rollback

If a script misbehaves, you can:
1. See full source in `/scripts/{action_id}`
2. See execution log with timestamps and errors
3. Disable/delete the script
4. Notify agent to regenerate with fixes

---

## 10. Error Handling & Recovery

### 9.1 Action Execution Failures

If an action fails:
1. Capture error code from Reaper Web Interface
2. Return to agent with context
3. Agent decides: retry, alternative action, or abort

Example:
```json
{
  "status": "failed",
  "action_id": 40285,
  "error": "Track index out of bounds",
  "context": {"num_tracks": 2, "requested_index": 5},
  "suggestions": ["Reduce track index", "Add more tracks first"]
}
```

### 9.2 Script Generation Failures

If generated script has syntax errors:
1. Return error to agent with line number
2. Agent regenerates with fixes
3. Revalidate before registering

### 9.3 Workflow Rollback

Complex workflows should support partial rollback:
- Mark each step result
- If step N fails, optionally undo steps 1..N-1
- Store undo action IDs for each step

---

## 11. Extensibility & Future Features

### 10.1 Plugin/Template System

Pre-built script templates for common tasks:
- Parallel compression template
- Sidechain setup template
- Drum sample replacement template
- Vocal chain template

Agents can request a template, customize, and execute.

### 10.2 Learning from Execution

Over time, agents learn:
- Which workflows work best for which tasks
- Common parameter ranges (compression ratios, EQ frequencies)
- Which scripts are reliable vs. fragile

Store this in agent context / OB1 memory.

### 10.3 Multi-DAW Support

ReaClaw could abstract over DAWs:
- DAW-agnostic workflow format
- Backend plugins for Reaper, Logic, Ableton, etc.
- Agent writes once, runs on any DAW

### 10.4 Collaborative Sessions

Multiple agents (Sparky + external Claude + user) collaborating:
- Shared workflow state
- Conflict resolution (who gets to record?)
- Interaction logs

---

## 12. Success Metrics

- **Latency:** Action execution <200ms (including network roundtrip)
- **Reliability:** 95%+ success rate on standard workflows
- **Cache Hit Rate:** After 10 uses, agent reuses workflows 80%+ of the time
- **Script Quality:** 90%+ of generated scripts execute without errors on first try
- **Agent Independence:** Agent can run a 20-step workflow with no human intervention

---

## 13. Open Questions / Decisions Needed

1. **HTTPS & Auth for MVP:** Self-signed + API key, or no auth? (Recommend: no auth for MVP, add later)
2. **Script approval workflow:** Automatic syntax check only, or require human approval? (Recommend: syntax check automatic, approval optional)
3. **Reaper Integration:** Web Interface only, or custom Bridge script? (Recommend: Hybrid)
4. **Distributed or local-only?** (For MVP, assume localhost. Plan for remote agents in Phase 4.)
5. **Rollback capability?** (Full undo on failure, or best-effort?) (Best-effort for MVP)

---

## 14. Appendix: API Example Flows

### Example 1: Agent Discovers Action

```
Agent: "How do I mute a track?"
ReaClaw: GET /catalog/search?q=mute
Response: [
  {"id": 40285, "name": "Mute track", "description": "Mute the selected track", "tags": ["track", "mute"]},
  {"id": 40352, "name": "Mute all tracks", "tags": ["track", "mute", "all"]},
  ...
]
Agent: "Got it. I'll use action 40285 on track 0"
```

### Example 2: Agent Generates and Caches Script

```
Agent: "Create a parallel compression setup"
ReaClaw: POST /scripts/generate {
  "intent": "parallel compression",
  "context": {"track": 0, "available_plugins": ["ReaComp", "ReaEQ"]}
}
Response: {
  "script": "local tr = reaper.GetTrack(0, 0)\nlocal aux = reaper.InsertTrackAtIndex(1, true)\n...",
  "preview": "Creates aux track, routes to parallel, adds ReaComp with 4:1 ratio"
}

Agent: "Looks good, register it"
ReaClaw: POST /scripts/register {
  "name": "parallel_comp_track0",
  "body": "...",
  "tags": ["compression", "parallel"]
}
Response: {
  "action_id": "_parallel_comp_track0_xyz123",
  "registered": true,
  "callable_immediately": true
}

Agent caches: {"parallel_comp": "_parallel_comp_track0_xyz123"}

Later...
Agent: "Set up parallel comp again"
Agent checks cache: Found! Use _parallel_comp_track0_xyz123
ReaClaw: POST /execute/action {"id": "_parallel_comp_track0_xyz123"}
Done. No regeneration needed.
```

### Example 3: Multi-Step Workflow with Feedback

```
Agent: "Record drums with sidechain from kick"
ReaClaw: POST /execute/sequence {
  "actions": [
    {"id": 40280, "description": "Select first track (kick)"},
    {"id": "_setup_sidechain", "description": "Setup sidechain"},
    {"id": 40285, "description": "Mute all but hihat"},
    {"id": 1013, "description": "Record"}
  ],
  "feedback_between_steps": true
}

Step 1: Executed. Feedback: {"selected_track": 0, "track_name": "Kick"}
Agent checks: "Yep, that's the kick"
Proceed to step 2...

Step 2: Executed. Feedback: {"sidechain_routed": true, "target": "Snare"}
Agent checks: "Wait, should route to drums, not snare"
ReaClaw: POST /verify {
  "expected": {"sidechain_target": "Drums"},
  "actual": {"sidechain_target": "Snare"}
}
Response: {"verified": false, "suggestion": "Run setup again with different params"}

Agent: "Let me fix that"
ReaClaw: POST /scripts/generate {
  "intent": "setup sidechain, target all drums"
}
Regenerate, re-register, execute...

Continue...
```

---

## End of Design Document

# ReaClaw: Technical Decisions

This document captures key architectural decisions and the rationale behind them.

## 1. Language: Go

**Decision:** Go for the server implementation

**Rationale:**
- Fast startup and compilation
- Excellent concurrency support (goroutines)
- Native HTTPS/TLS support
- Simple HTTP/REST handling
- Lightweight binary (~10-20MB)
- Easy to cross-compile for Linux/macOS/Windows
- Good JSON support out of the box

**Alternatives Considered:**
- Rust: More performant but overkill for this use case; slower iteration
- Python: Fast to prototype but slower at runtime; harder to distribute

**Trade-offs:** None significant for this use case.

---

## 2. Persistence: SQLite

**Decision:** SQLite for all persistent storage (action catalog, scripts, workflows, history)

**Rationale:**
- Zero infrastructure (single file `reaclawdb.sqlite`)
- No server to manage; no external dependencies
- Fast for read-heavy workloads (catalog lookups)
- Reliable transactions (ACID)
- Go has good SQLite driver (`github.com/mattn/go-sqlite3`)
- Easy to backup (copy the file)
- Scales for single-user/single-machine use
- Upgrade path: If distributed agents needed, migrate to PostgreSQL

**Alternatives Considered:**
- PostgreSQL: Overkill for MVP; requires server setup
- JSON files: Simple but slow for large datasets; no querying
- In-memory only: Loses history between restarts

**Trade-offs:** 
- Not suitable for multi-machine deployments (Phase 4 can add PostgreSQL)
- Single-writer model (acceptable for one Reaper instance)

---

## 3. Reaper Integration: Hybrid

**Phase 0-1: Use Built-in Web Interface**
- Reaper's HTTP server (`http://localhost:8808`)
- Send commands via `/_/action_id` syntax
- Query limited state via web API

**Phase 2+: Add Custom "ReaClaw Bridge" Script**
- Optional ReaScript running in Reaper
- Exposes richer state queries
- Handles script registration with full Reaper API

**Rationale:**
- MVP doesn't require custom plugins
- Web Interface is already in Reaper; no installation friction
- Bridge script (when needed) is just a normal Lua file in Reaper's scripts folder
- Keeps core ReaClaw separate from Reaper-specific logic

**Alternatives Considered:**
- Custom C++ plugin from day one: Over-engineering for MVP
- Only Web Interface forever: Limited to what web API exposes
- Only Bridge script: Requires plugin setup

**Trade-offs:**
- Phase 0-1 have some limitations (state queries less rich)
- Phase 2+ requires deploying a script to Reaper (easy one-time step)

---

## 4. Script Validation: Trust + Audit

**Decision:**
- Built-in & community scripts: Fully trusted, no validation
- Generated scripts: Mandatory syntax check, optional static analysis, optional approval
- All executions logged to SQLite

**Rationale:**
- Reaper ecosystem (built-in + community via ReaPack) is vetted
- Generated scripts are new and potentially risky; validate before execution
- Syntax errors caught before registration
- Static analysis warns without blocking (agent can fix)
- Human approval optional (configurable)
- Audit trail means if something goes wrong, you can trace it

**Alternatives Considered:**
- No validation: Risk of breaking Reaper
- Strict sandboxing: Reaper has no native sandbox; would require external process
- Always require approval: Too slow for agents; defeats automation

**Trade-offs:**
- Syntax validation adds latency (milliseconds; acceptable)
- Static analysis is imperfect (may have false positives/negatives)
- Requires trust in Reaper's own sandbox (reasonable assumption)

---

## 5. Authentication: Flexible Defaults

**Decision:** Three auth modes, selectable in config

1. **None** (default for MVP): Assumes trusted network
2. **API Key:** Simple bearer token in Authorization header
3. **mTLS:** Client certificate required

**Rationale:**
- Different trust models for different deployment scenarios
- MVP (local): No auth overhead
- Home network: API key (simple, effective)
- Remote/distributed: mTLS (stronger)
- HTTPS always available (self-signed or signed certs)

**Alternatives Considered:**
- OAuth/OpenID: Overkill for local use
- None (no auth, no TLS): Insecure for remote access

**Trade-offs:**
- Multiple auth modes add complexity (but it's optional; pick one)
- No auth by default requires network isolation

---

## 6. Caching Strategy: Lazy + Indexed

**Decision:**
- Action catalog: Pre-indexed on startup (read-only, cached in SQLite)
- Generated scripts: Cached with unique action IDs after registration
- Agents check cache before regenerating

**Rationale:**
- Catalog is static (only changes when Reaper updates); index once, use forever
- Generated scripts are unique per workflow; cache them with IDs so agents can reuse
- Avoids redundant script generation (expensive API calls)
- Execution history enables agents to learn patterns

**Alternatives Considered:**
- No caching: Slow; agents regenerate constantly
- Full in-memory cache: Loses data on restart; uses RAM

**Trade-offs:**
- Agents need to check cache (simple API call)
- Cache invalidation simple (just rebuild catalog if Reaper updates)

---

## 7. API Design: REST + Stateless

**Decision:** RESTful HTTP API with stateless endpoints

**Rationale:**
- Agent-agnostic (any HTTP client can use it)
- Stateless design means requests are independent
- Easy to scale/distribute later
- Standard practices; familiar to developers
- JSON for request/response (standard)

**Alternatives Considered:**
- gRPC: Faster but requires generated stubs; less agent-friendly
- WebSockets: Good for real-time but unnecessary for this use case
- Stateful sessions: Complex; agents would need to manage state

**Trade-offs:**
- JSON is more verbose than binary protocols
- Statelessness means agents must provide context (acceptable)

---

## 8. Agent Independence: No Hard Dependencies

**Decision:** ReaClaw has NO dependencies on OpenClaw, OB1, Sparky, or any specific agent

**Rationale:**
- Standalone; works with any HTTP-capable AI system
- Optional MCP integration for OpenClaw (not required)
- Agents manage their own state/memory independently
- No vendor lock-in

**Alternatives Considered:**
- Tight coupling to OpenClaw: Limits reusability
- Hard dependency on specific LLM: Limits flexibility

**Trade-offs:**
- None; this is a pure win

---

## 9. Configuration: YAML + Defaults

**Decision:** YAML config file with sensible defaults; env vars optional

**Rationale:**
- YAML is human-readable
- Defaults allow zero-config for MVP
- Env vars allow container/cloud deployment
- Easy to version control (check in defaults, exclude secrets)

**Example:**
```yaml
server:
  port: 9091
  host: 0.0.0.0

auth:
  type: "none"  # or "api_key", "mtls"

script_security:
  validate_syntax: true
  static_analysis: true
```

---

## 10. Error Handling: Fail Gracefully, Log Everything

**Decision:**
- Actions fail with descriptive errors (not silent failures)
- All failures logged to SQLite audit trail
- Agent gets context for recovery (error type, state before/after)

**Rationale:**
- Agents need to know when something went wrong
- Audit trail is critical for debugging
- Context enables agents to retry or use alternatives

**Example Error Response:**
```json
{
  "status": "failed",
  "action_id": 40285,
  "error": "Track index out of bounds",
  "context": {"num_tracks": 2, "requested_index": 5},
  "suggestions": ["Reduce track index", "Add more tracks first"]
}
```

---

## 11. Extensibility: Plugin-Ready Architecture

**Decision:** Modular code structure; easy to add new endpoints, integrations, backends

**Structure:**
```
pkg/
├── api/          # HTTP handlers (add new endpoints here)
├── reaper/       # Reaper-specific code (swap out for other DAWs)
├── script/       # Script generation (extensible for EEL2, Python)
├── db/           # Database layer (swap SQLite for PostgreSQL)
└── models/       # Data structures
```

**Rationale:**
- Clean separation of concerns
- Future: Support other DAWs (Logic, Ableton) by swapping `pkg/reaper`
- Future: Switch to PostgreSQL by swapping `pkg/db`
- Future: Add new script languages easily

---

## 12. Development Philosophy: Planning > Iteration

**Decision:** Full design upfront; phases guide implementation; avoid scope creep

**Rationale:**
- Clear roadmap from MVP to full vision
- Phases are shipping units (complete and testable)
- No "oh we should add this later" surprises
- Technical debt minimized by planning

**Phases:**
1. **Phase 0 (MVP):** Action catalog + basic execution
2. **Phase 1:** Script generation + registration
3. **Phase 2:** Feedback loops + state verification
4. **Phase 3:** Workflows + macro system
5. **Phase 4:** Integration + distributed agents

---

## Open Questions (TBD)

1. **Script approval workflow** — Currently optional; enable by default? 
   - Recommendation: No (automatic syntax check sufficient for MVP; enable if issues arise)

2. **Reaper Bridge script** — When to deploy?
   - Recommendation: Phase 2 (MVP uses web interface; upgrade when needed)

3. **PostgreSQL migration** — Trigger point for switching from SQLite?
   - Recommendation: Phase 4 (only if multi-machine agents needed)

4. **Multi-DAW support** — Priority?
   - Recommendation: Out of scope for MVP (focus on Reaper; architecture allows it)

---

## Summary

ReaClaw is designed for:
- **Simplicity:** Go + SQLite + HTTPS (no complex infrastructure)
- **Flexibility:** Agent-agnostic, multiple auth modes, extensible architecture
- **Trust:** Validation for generated scripts; audit log for all actions
- **Scalability:** Starts local, scales to distributed agents (PostgreSQL upgrade path)

All decisions assume a single Reaper instance on a trusted network (MVP). Future phases add distributed agents and stronger security.

# CLAUDE.md — ReaClaw Project

## Before Touching Any Code

**Read these files first. Every time. No exceptions.**

1. `ReaClaw_TECH_DECISIONS.md` — architectural decisions and rationale; many things that look like bugs are intentional choices
2. `ReaClaw_IMPLEMENTATION_CHECKLIST.md` — phase-by-phase task list; shows what's done, what's in scope, and what's deferred
3. `ReaClaw_Design.md` — full design spec including API contracts, data models, and security model
4. `SECURITY.md` — security model, scope, and deployment guidance

Do not propose changes that contradict settled decisions in `TECH_DECISIONS.md`. If a decision seems wrong, raise it with the user rather than silently overriding it.

---

## Update Docs As You Go

After completing each checklist item, mark it done in `ReaClaw_IMPLEMENTATION_CHECKLIST.md`. After adding or changing any API endpoint, update `docs/API.md`. After any config change, update the config reference in `ReaClaw_Design.md` §7. This is how the project survives crashes, context compaction, and resumed sessions.

---

## Project Overview

ReaClaw is a native C++ REAPER extension (`.dll`/`.dylib`/`.so`) that embeds an HTTPS server inside REAPER's process and exposes a REST/JSON API for AI agents to control REAPER.

**Key settled decisions (do not re-litigate without reading TECH_DECISIONS.md first):**
- Bind address `0.0.0.0` is intentional — loopback-only is a user config choice, not a default
- No rate limiting — single-user tool; network isolation is the right defense layer
- Auth: `none` or `api_key` only — no mTLS, no OAuth
- Lua validation: syntax only, no static analysis, no approval gate — agent is trusted
- No LLM client in the extension

---

## Phase Status

| Phase | Tag | Status |
|-------|-----|--------|
| Phase 0 — Foundation | v0.1.0 | Complete |
| Phase 1 — Scripts & Sequences | v0.2.0 | Complete |
| Phase 2 — Integration & Hardening | v1.0.0 | **Complete** |

### Phase 2 Security Hardening — Task Breakdown

**All Phase 2 items complete.** See `ReaClaw_IMPLEMENTATION_CHECKLIST.md` for full record.

Key additions in Phase 2:
- Security: HSTS header, path traversal guard, auth failure audit log
- Performance: 1s TTL state cache (`/state`, `/state/tracks`, `/state/items`); SQLite indexes were already in schema
- Observability: health endpoint gains `queue_depth`, `db_ok`, `server_running`; JSON log format option
- Docs: `docs/MCP.md`, `docs/DEPLOYMENT.md`

### Phase 2 Performance — Task Breakdown

- [ ] SQLite indexes: `execution_history(executed_at)`, `scripts(name)`
- [ ] Profile catalog search, state queries, action execution (targets: <50ms, <100ms, <200ms)
- [ ] 1s TTL cache for frequent state reads

### Phase 2 MCP Wrapper (Optional)

- [ ] Design MCP tool definitions (see checklist for list)
- [ ] Write `docs/MCP.md`

### Phase 2 Observability

- [ ] `GET /health` enhancements: queue depth, DB status, server thread alive
- [ ] Structured log format option

---

## Source Layout

```
src/
├── app.h                    # Global singletons (g_config, g_db, g_start_time)
├── main.cpp                 # Defines globals; ReaperPluginEntry
├── auth/auth.{h,cpp}        # Auth check + reject helpers
├── config/config.{h,cpp}    # Config struct, load(), save()
├── db/db.{h,cpp}            # SQLite wrapper
├── handlers/                # HTTP route handlers
│   ├── common.h             # json_ok, json_error, now_iso, agent_id, vol_to_db
│   ├── catalog.{h,cpp}      # GET /catalog, /catalog/search, /catalog/categories, /catalog/{id}
│   ├── execute.{h,cpp}      # POST /execute/action, /execute/sequence
│   ├── history.{h,cpp}      # GET /history
│   ├── scripts.{h,cpp}      # POST /scripts/register, GET/DELETE /scripts/{id}, GET /scripts/cache
│   └── state.{h,cpp}        # GET /state/*, POST /state/tracks/{index}
├── reaper/                  # REAPER SDK integration
│   ├── api.{h,cpp}          # init(), shutdown(), timer_callback(); REAPERAPI_IMPLEMENT
│   ├── catalog.{h,cpp}      # Action catalog indexer (kbd_enumerateActions)
│   ├── executor.{h,cpp}     # Command queue + main-thread dispatch
│   └── scripts.{h,cpp}      # register_script(), unregister_script()
├── server/
│   ├── router.{h,cpp}       # Route registration + auth_wrap
│   └── server.{h,cpp}       # SSLServer lifecycle
└── util/
    ├── logging.{h,cpp}      # Log::info/warn/error, level filter
    └── tls.{h,cpp}          # Self-signed cert generation
```

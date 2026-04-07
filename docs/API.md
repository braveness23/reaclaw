# ReaClaw API Reference

All endpoints:
- Accept and return `application/json`
- Require `Authorization: Bearer {key}` when `auth.type` is `"api_key"`
- Return a standard error shape on failure

## Error format

```json
{ "error": "description", "code": "ERROR_CODE", "context": {} }
```

HTTP status codes: `200 OK`, `400 Bad Request`, `401 Unauthorized`, `404 Not Found`,
`408 Request Timeout`, `500 Internal Server Error`, `501 Not Implemented`

Optional request header on all endpoints:
```
X-Agent-Id: <string>
```
Stored in `execution_history.agent_id` for filtering via `GET /history?agent_id=`.

---

## Phase 0 (v0.1.0)

### GET /health

Returns server status.

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

### GET /catalog

Returns paginated action list.

Query params: `limit` (default 100), `offset` (default 0)

```json
{
  "total": 65234,
  "offset": 0,
  "limit": 100,
  "actions": [
    { "id": 40285, "name": "Track: Toggle mute for selected tracks", "category": "Track", "section": "main" }
  ]
}
```

### GET /catalog/search?q=\<query\>

Full-text search (SQLite FTS5) across action names and categories.

Query params: `q` (required), `limit` (default 20)

```json
{ "query": "mute", "total": 12, "actions": [...] }
```

### GET /catalog/{id}

Single action by numeric ID. Returns 404 if not found.

```json
{ "id": 40285, "name": "Track: Toggle mute for selected tracks", "category": "Track", "section": "main" }
```

### GET /catalog/categories

```json
{ "categories": [ { "name": "Track", "count": 4200 }, ... ] }
```

---

### GET /state

Project and transport state snapshot.

```json
{
  "project": { "bpm": 120.0, "time_signature": "4/4", "cursor_position": 15.5 },
  "transport": { "playing": false, "recording": false, "paused": false },
  "track_count": 12
}
```

### GET /state/tracks

All tracks with name, mute, solo, armed, volume, pan, and FX list.

```json
{
  "tracks": [
    { "index": 0, "name": "Kick", "muted": false, "soloed": false,
      "armed": true, "volume_db": 0.0, "pan": 0.0, "fx": [...] }
  ]
}
```

### POST /state/tracks/{index}

Set track properties directly (no action lookup needed). Supported fields:
`muted` (bool), `soloed` (bool), `armed` (bool), `volume_db` (float), `pan` (float −1.0→1.0)

```json
// Request
{ "muted": true, "volume_db": -6.0 }

// Response — updated track state
{ "index": 0, "name": "Kick", "muted": true, "volume_db": -6.0, "pan": 0.0 }
```

Returns 404 if index is out of range.

### GET /state/items

Media items: position, length, track index, take name.

### GET /state/selection

Currently selected tracks and items.

### GET /state/automation

Automation envelopes for the selected track.

---

### POST /execute/action

Execute a single action by numeric ID or registered script action ID.

```json
// Request
{ "id": 40285, "feedback": true }
// id may also be a string: "_parallel_comp_a1b2c3"

// Response
{
  "status": "success",
  "action_id": 40285,
  "executed_at": "2026-04-07T14:24:10Z",
  "feedback": { "transport": {...}, "tracks": [...] }
}
```

Returns 408 if the REAPER main thread doesn't respond within 5 seconds.

---

## Phase 1 (v0.2.0)

### POST /scripts/register

Register an agent-generated Lua ReaScript as a custom REAPER action.

**Request:**
```json
{
  "name": "parallel_comp_drums",
  "script": "local tr = reaper.GetSelectedTrack(0,0)\n...",
  "tags": ["compression", "parallel", "drums"]
}
```

**Response — success:**
```json
{
  "action_id": "_parallel_comp_drums_a1b2c3d4",
  "registered": true,
  "script_path": "/path/to/UserPlugins/../reaclaw/scripts/_parallel_comp_drums_a1b2c3d4.lua"
}
```

**Response — Lua syntax error:**
```json
{
  "registered": false,
  "syntax_error": {
    "line": 7,
    "message": "'end' expected (to close 'function') near '<eof>'"
  }
}
```

Idempotent: sending the same `name` again returns the existing `action_id`.

### GET /scripts/cache

List all registered scripts. Optional `?tags=<tag>` filter.

```json
{
  "scripts": [
    {
      "action_id": "_parallel_comp_drums_a1b2c3d4",
      "name": "parallel_comp_drums",
      "tags": ["compression", "parallel", "drums"],
      "execution_count": 5,
      "created_at": "2026-04-07T10:00:00Z",
      "last_executed": "2026-04-07T14:24:10Z"
    }
  ]
}
```

### GET /scripts/{action_id}

Full metadata and Lua source for a single registered script.

```json
{
  "action_id": "_parallel_comp_drums_a1b2c3d4",
  "name": "parallel_comp_drums",
  "script": "local tr = reaper.GetSelectedTrack(0,0)\n...",
  "script_path": "/path/to/.../scripts/_parallel_comp_drums_a1b2c3d4.lua",
  "tags": ["compression", "parallel"],
  "execution_count": 5,
  "created_at": "2026-04-07T10:00:00Z"
}
```

### DELETE /scripts/{action_id}

Unregisters the script from REAPER's action list, deletes the `.lua` file, and removes the DB row.

```json
{ "deleted": true, "action_id": "_parallel_comp_drums_a1b2c3d4" }
```

Returns 404 if the action ID is not found.

---

### POST /execute/sequence

Execute multiple actions in order with optional per-step feedback.

**Request:**
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

- `steps`: array of `{id, label?}`. Max 100 steps. `id` may be integer or script string.
- `feedback_between_steps`: if true, capture state after each step.
- `stop_on_failure`: if true, skip remaining steps after the first failure.

**Response:**
```json
{
  "status": "success",
  "steps_completed": 3,
  "steps": [
    {
      "label": "mute kick",
      "action_id": 40285,
      "status": "success",
      "feedback": { "transport": {...}, "tracks": [...] }
    }
  ]
}
```

Step status values: `"success"`, `"failed"`, `"timeout"`, `"skipped"`.

---

### GET /history

Query execution audit log.

Query params: `limit` (default 50, max 500), `offset` (default 0), `agent_id` (optional filter)

```json
{
  "total": 1042,
  "offset": 0,
  "limit": 50,
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

`type` is one of `"action"`, `"sequence"`.

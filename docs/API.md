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

All responses include:
```
Strict-Transport-Security: max-age=31536000
```

`GET /state`, `GET /state/tracks`, and `GET /state/items` responses are cached for 1 second. A `POST /state/tracks/{index}` write immediately invalidates the cache.

---

## Phase 0 (v0.1.0)

### GET /health

Returns server status.

```json
{
  "status": "ok",
  "version": "1.0.0",
  "reaper_version": "7.12",
  "catalog_size": 65234,
  "uptime_seconds": 3600,
  "queue_depth": 0,
  "db_ok": true,
  "server_running": true
}
```

- `queue_depth` — commands currently waiting for the REAPER main thread
- `db_ok` — SQLite connection is open
- `server_running` — HTTPS listener thread is active

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

All tracks with name, mute, solo, armed, volume, pan, folder nesting, color, and FX list.

```json
{
  "tracks": [
    { "index": 0, "name": "Kick", "muted": false, "soloed": false,
      "armed": true, "volume_db": 0.0, "pan": 0.0,
      "folder_depth": 1, "color": "#EC436F", "fx": [...] }
  ]
}
```

`folder_depth`: `1` = folder parent (children follow), `0` = normal track,
negative = closes that many folder levels (last track in a folder).
`color`: track custom color as `"#RRGGBB"`, or `null` when the track uses the
default color. Each track also carries a `sends` array
(`{index, dest_track, volume_db, pan}`) so routing is verifiable via the API.

### POST /state/tracks/{index}

Set track properties directly (no action lookup needed). Writable fields:
`name` (string), `color` (`"#RRGGBB"` or `null` to clear), `folder_depth` (int),
`muted` (bool), `soloed` (bool), `armed` (bool), `volume_db` (float), `pan`
(float −1.0→1.0). Returns the updated track. 404 if index out of range.

```json
// Request
{ "name": "Kick", "color": "#33AA55", "volume_db": -6.0 }
```

### POST /state/tracks

Create and/or batch-update tracks in one call. `create` appends tracks in order;
`update` patches existing tracks by `index`. Each spec accepts the same writable
fields as `POST /state/tracks/{index}`.

```json
// Request
{ "create": [ { "name": "DRUMS", "color": "#CC3333", "folder_depth": 1 },
              { "name": "Kick", "folder_depth": -1, "volume_db": -3.0 } ],
  "update": [ { "index": 0, "name": "Master Bus" } ] }

// Response
{ "created": [ {track}, ... ], "updated": [ {track}, ... ] }
```

### DELETE /state/tracks/{index}

Remove a track. Returns `{ "deleted": <index> }`; 404 if out of range.

### POST /state/tracks/{index}/fx

Add an FX by name (e.g. `"ReaComp"`, `"ReaGate"`, or a full `"VST: ..."`).

```json
// Request
{ "name": "ReaComp", "enabled": true,
  "params": [ { "name": "Threshold", "value": 0.25 } ] }

// Response
{ "track": 2, "slot": 0, "name": "VST: ReaComp (Cockos)", "enabled": true }
```

`params` values are **normalized 0..1**, referenced by `index` or `name`.
Returns 400 (`FX not found: ...`) if the plugin can't be resolved.

### GET /state/tracks/{index}/fx/{slot}

FX slot detail incl. parameter list (`index`, `name`, normalized `value`,
`formatted`).

### POST /state/tracks/{index}/fx/{slot}

Set `enabled` (bool) and/or `params` (`[{index|name, value}]`). Returns the
updated FX slot with its params.

### DELETE /state/tracks/{index}/fx/{slot}

Remove an FX slot.

### POST /state/tracks/{index}/sends

Add a send from this track to another. `{ "to_track": j, "volume_db": x, "pan": y }`
(volume/pan optional). Returns `{ track, send_index, dest_track, volume_db, pan }`.

### DELETE /state/tracks/{index}/sends/{send}

Remove a send by its index.

### POST /state/selection

Set the track selection. `{ "tracks": [i, ...] }`, or `"all"` / `"none"`.
Returns `{ "selected_tracks": [...] }`.

### GET /capabilities

Manifest of what the API supports directly (structured verbs) vs. via an action
or a generated Lua script — reflects the tiered coverage model.

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
  "action_name": "Track: Toggle mute for selected tracks",
  "executed_at": "2026-04-07T14:24:10Z",
  "feedback": { "transport": {...}, "tracks": [...] }
}
```

`action_name` is the resolved human-readable name (from the bundled catalog;
omitted only when the id is unknown). Sequence step results include `action_name`
per step the same way.

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
      "target_name": "Track: Toggle mute for selected tracks",
      "agent_id": "claude-sonnet-4-6",
      "status": "success",
      "executed_at": "2026-04-07T14:24:10Z"
    }
  ]
}
```

`type` is one of `"action"`, `"sequence"`. `target_name` is the resolved action
name (omitted for rows logged before this field existed, or unknown ids).

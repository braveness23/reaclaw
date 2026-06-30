# ReaClaw API Reference

All endpoints:
- Accept and return `application/json`
- Require `Authorization: Bearer {key}` when `auth.type` is `"api_key"`
- Return a standard error shape on failure

## Stability & versioning

ReaClaw follows [SemVer](https://semver.org/). Any endpoint documented here and advertised in
`GET /capabilities` is **stable**: it changes only additively (new endpoints/fields → MINOR
release) and will not break except in a MAJOR release, with a deprecation notice first. The Lua
escape hatch (`/scripts/register`) and raw action IDs (`/execute/action`) are version-coupled to
REAPER and carry no stability guarantee beyond "the call is accepted." Full policy:
`ReaClaw_TECH_DECISIONS.md` §21.

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

`GET /state` and `GET /state/tracks` responses are cached for 1 second. A `POST /state/tracks/{index}` write immediately invalidates the cache. `GET /state/items` is not cached — it reads through the main thread, so item edits are always reflected.

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

When the main thread is stuck (queue non-empty for >10s), `status` changes to
`"degraded"` and a `degraded_reason` field is added. Only recovery is restarting
REAPER.

```json
{
  "status": "degraded",
  "degraded_reason": "command queue non-empty for >10s — main thread may be blocked; restart REAPER to recover",
  "queue_depth": 3,
  ...
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

Query params: `q` (required), `limit` (default 20), `category`, `section=midi_editor`.

**Synonym expansion (strict-first):** the literal query runs first, so precise queries keep
their precision. On a miss, the query is widened through a curated synonym map (e.g. "folder
depth" → "indent", "bounce" → "render", "colour" → "color") — AND-of-synonym-groups first,
then OR-of-all as a last resort. The response echoes `matched` (the FTS expression actually
used) and `expanded` (whether widening kicked in).

Each action carries an `interactive` flag — `true` when it opens a modal dialog (ellipsis/
prompt/known-modal id), so a headless agent can filter out actions that would hang on a dialog.

```json
{ "query": "set folder depth", "matched": "set OR folder OR indent OR depth …",
  "expanded": true, "total": 3,
  "actions": [ { "id": 53609, "name": "SWS: Indent selected track(s)", "interactive": false } ] }
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

All tracks with name, mute, solo, armed, volume, pan, folder nesting, color, icon, and FX list.

```json
{
  "tracks": [
    { "index": 0, "name": "Kick", "muted": false, "soloed": false,
      "armed": true, "volume_db": 0.0, "pan": 0.0,
      "folder_depth": 1, "color": "#EC436F", "icon": "kick.png", "fx": [...] }
  ]
}
```

`folder_depth`: `1` = folder parent (children follow), `0` = normal track,
negative = closes that many folder levels (last track in a folder).
`color`: track custom color as `"#RRGGBB"`, or `null` when the track uses the
default color. `icon`: current `P_ICON` value — a relative icon name or absolute
path — or `null` when no icon is set. Each track also carries a `sends` array
(`{index, dest_track, volume_db, pan}`) so routing is verifiable via the API.

### GET /state/track-icons

List all icon filenames available in `{ResourcePath}/Data/track_icons`.
The names returned here can be passed directly as the `icon` field on track
create/update. Absolute paths are accepted too but will not appear in this list.

```json
{
  "icons": ["ac_guitar.png", "amp.png", "bass.png", "kick.png", "vocal1.png", "..."],
  "count": 91,
  "search_path": "/home/user/.config/REAPER/Data/track_icons"
}
```

### POST /state/tracks/{index}

Set track properties directly (no action lookup needed). Writable fields:
`name` (string), `color` (`"#RRGGBB"` or `null` to clear), `folder_depth` (int),
`muted` (bool), `soloed` (bool), `armed` (bool), `volume_db` (float), `pan`
(float −1.0→1.0), `icon` (string or `null`). Returns the updated track. 404 if
index out of range.

`icon`: relative name (e.g. `"bass.png"`) resolved against `Data/track_icons`, or
an absolute path. `null` or `""` clears it. If a relative name doesn't resolve to a
file, the response includes an `icon_not_found` hint in `hints[]`.

```json
// Request
{ "name": "Kick", "color": "#33AA55", "volume_db": -6.0, "icon": "kick.png" }
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

FX slot detail incl. parameter list. Each param: `index`, `name`, normalized
`value` (0..1), `formatted` (display string), and the real-unit range `raw`
(current value), `min`, `max`, `mid` — so an agent can reason in real units.

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

Set the track and/or item selection. Each of `tracks` and `items` accepts an
index array, or the string `"all"` / `"none"`:
`{ "tracks": [i, ...] | "all" | "none", "items": [j, ...] | "all" | "none" }`.
Returns `{ "selected_tracks": [...], "selected_items": [...] }` (only the keys
you set). See also Epic #17 below for item selection.

### GET /capabilities

Manifest of what the API supports directly (structured verbs) vs. via an action
or a generated Lua script — reflects the tiered coverage model. Fields:

- `coverage_model`, `version`, `direct`, `via_script_or_action` — the tiered manifest.
- `coverage` — a **map of every REST-relevant REAPER domain** to `{status, note}`, so an
  agent can see the whole surface and know nothing is hidden. `status` ∈
  `structured` (typed verb), `chunk` (universal `/state/chunk` backstop), `action`
  (`/execute/action` only), `lua` (`/scripts/register` only), `out_of_scope` (deliberately
  not exposed — e.g. control surface, PCM/VST interfaces). Verdicts mirror
  `ReaClaw_COVERAGE_REPORT.md` §4.
- `sdk` — honest surface summary: `{functions_total, functions_called, raw_pct, reachable, note}`
  (reproducible; see the coverage report §1).
- `features` — optional-dependency detection so agents branch instead of probe-and-fail:
  `{sws, sws_r128_loudness, ffmpeg, xdotool, key_tempo_detector}`.

```json
{
  "coverage": { "tracks": {"status":"structured","note":"…"},
                "midi": {"status":"lua","note":"… pending #51"},
                "control_surface": {"status":"out_of_scope","note":"…"} },
  "sdk": { "functions_total": 865, "functions_called": 131, "raw_pct": 15.1,
           "reachable": "100% via verbs + actions (~6700) + Lua + chunk backstop" },
  "features": { "sws": true, "sws_r128_loudness": true, "ffmpeg": true,
                "xdotool": true, "key_tempo_detector": false }
}
```

### GET /state/items

Media items, enriched (Epic #17): `index`, `position`, `length`, `track_index`,
`selected`, `muted`, `volume_db`, `fade_in`, `fade_out`, plus a `take` object
(`name`, `volume_db`, `polarity_flipped`, `pan`, `pitch`, `playrate`,
`preserve_pitch`) and a `source` object (`file`, `type`, `length`,
`length_is_beats`, `sample_rate`, `num_channels`). `take` and `source` are
`null` for an empty item. See Epic #17 below for item create/update/split/delete.

### GET /state/selection

Currently selected tracks and items.

### GET /state/automation

Automation envelopes for the selected track.

---

### GET /state/chunk · POST /state/chunk

The **universal reachability backstop**: read or write the full RPP state chunk of any
`track`, `item`, or `envelope`. Any property REAPER serializes into the project file is
reachable here even when no dedicated structured verb exists — so combined with
`/execute/action` and `/scripts/register`, the automation surface is provably 100% reachable.

**GET** `?target=track|item|envelope&index=N[&envelope=M]` — `index` is the track/item index;
for `envelope`, `index` is the track and `envelope` is the envelope index on that track.

```json
{ "target": "track", "index": 0, "envelope": 0, "chunk": "<TRACK\n  NAME ChunkTest\n  ...\n>" }
```

**POST** `{ "target", "index", "envelope"?, "chunk" }` — applies the chunk. Writes are wrapped
in a single undo block ("ReaClaw: set state chunk") and bust the state read-cache.

```json
{ "target": "track", "index": 0, "envelope": 0, "applied": true }
```

Errors: `400` bad target/params or malformed request, `404` index out of range, `500` REAPER
rejected the chunk (malformed RPP). The read buffer grows up to 64 MB for very large objects.

---

### POST /execute/action

Execute a single action by numeric ID or registered script action ID.

```json
// Request
{ "id": 40285, "feedback": true }
// id may also be a string: "_parallel_comp_a1b2c3"

// With per-call timeout (useful for slow scripts on ARM/Pi rigs):
{ "id": "_midi_build_6f9653d4", "timeout_ms": 30000 }

// Response
{
  "status": "success",
  "action_id": 40285,
  "action_name": "Track: Toggle mute for selected tracks",
  "executed_at": "2026-04-07T14:24:10Z",
  "feedback": { "transport": {...}, "tracks": [...] }
}
```

**Fields:**
- `id` *(required)* — integer action ID or string registered-script action ID
- `feedback` *(optional, bool)* — include a post-execution state snapshot in the response
- `timeout_ms` *(optional, int)* — override the default 15 000 ms main-thread wait; clamped to [1 000, 120 000]
- `async` *(optional, bool)* — fire via SWELL SetTimer instead of blocking; returns `{"status":"queued"}` immediately

`action_name` is the resolved human-readable name (from the bundled catalog;
omitted only when the id is unknown). Sequence step results include `action_name`
per step the same way.

Returns 408 if the REAPER main thread doesn't respond within the timeout (default 15s).

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

**Response — Lua syntax error or unsafe call:**
```json
{
  "registered": false,
  "syntax_error": {
    "line": 7,
    "message": "'end' expected (to close 'function') near '<eof>'"
  }
}
```

Scripts containing `ShowConsoleMsg` are rejected at registration — this call blocks
the main thread in headless sessions. Use `reaper.SetExtState("ns","key",val,false)`
to return data from scripts instead.

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

---

## Phase 4 — Tier-A control verbs (v1.3.0, #16)

All structured mutations below run inside a REAPER **undo block**, so each lands
as one user-undoable step (a validation no-op creates no undo point).

### GET /undo

`{ "can_undo": "<desc>"|null, "can_redo": "<desc>"|null }` — what the next
undo/redo would do.

### POST /undo · POST /redo

Perform one undo / redo step. Returns `{ "undone"|"redone": <bool>, "description": "<desc>" }`.

### GET /state/markers

`{ "markers": [ { enum_index, id, is_region, position, region_end, name, color } ] }`.
`color` is `"#RRGGBB"` or `""`.

### POST /state/markers

Add a marker or region. `{ position, name?, is_region?, region_end?,
color?("#RRGGBB"), id? }` (`id` = -1/omitted auto-assigns). Returns `{ id, is_region, position }`.

### DELETE /state/markers/{id}

Delete by displayed marker/region id. Query: `?is_region=true|false`.

### GET /state/tempo

Full tempo / time-signature map: `{ count, markers: [ { index, time, measure,
beat, bpm, timesig_num, timesig_denom, linear } ] }`.

### POST /state/tempo

Add a tempo/time-sig marker. `{ time, bpm, timesig_num?, timesig_denom?, linear? }`.

### GET /time

Beat↔time conversion. `?time=SEC` → `{ time, full_beats, beat_in_measure,
measure, measure_length_beats, timesig_denom }`. `?beats=B[&measure=M]` →
`{ beats, time }`.

### GET /state/tracks/{index}/fx/{slot}/preset

`{ track, slot, preset, preset_index, preset_count }`.

### POST /state/tracks/{index}/fx/{slot}/preset

Load a preset by `{ "name": "..." }` or step with `{ "navigate": -1|1 }`.
Returns the preset state. 400 if the preset isn't found / didn't change.

### POST /state/tracks/{index}/sends/{send}

Update an existing send: `{ volume_db?, pan?, muted?, phase?, mono?, mode? }`
(`mode`: 0 post-fader, 1 pre-fx, 2 pre-fader). Returns the updated send.

### POST /state/tracks/{index}/automation

Write envelope points. `{ envelope: "Volume", points: [ { time, value, shape?,
tension? } ], clear_range?: [start, end] }`. `value` is the envelope's native
value. The named envelope must already be active on the track (else 400).
Returns `{ track, envelope, points_written }`.

### GET /project · POST /project/notes

`GET /project` → `{ dirty, length, notes }` (dirty = unsaved-changes flag, a
prompt-save signal). `POST /project/notes { notes }` sets the project notes
scratchpad (persisted in the `.rpp`).

### Catalog additions

- `GET /catalog?section=midi_editor` and `GET /catalog/search?q=...&section=midi_editor`
  query the MIDI editor action section instead of the main section.
- Every catalog row now includes `interactive` (bool): true if the action opens a
  modal dialog (so a headless agent should avoid firing it).

---

## Phase 4 — Tier-B content manipulation (v1.4.0, #17)

Media items were read-only before this epic. These verbs add the write surface
for the objects *inside* a track. All mutations are wrapped in undo blocks (one
undoable step each). Items are addressed by **project-wide index** (the order
`GET /state/items` returns); a structural change (create/split/delete) can shift
those indices, so re-read after one.

### GET /state/items/{index}

A single media item, same shape as one element of `GET /state/items`. 404 if the
index is out of range.

### POST /state/items

Create and/or batch-update items.

```json
{
  "create": [ { "track": 0, "position": 1.0, "length": 2.0, "file": "/path/a.wav" } ],
  "update": [ { "index": 0, "position": 0.5, "muted": true,
                "take": { "name": "lead", "pan": -0.3, "pitch": 2, "playrate": 1.0 } } ]
}
```

- **create**: `track` (required), `position` (default 0), `length` (default the
  source length when `file` is given, else 1.0s), `file` (loads an audio/MIDI
  source as the item's active take). Also honors the update fields below.
- **update**: addresses an existing item by `index`; writable fields are
  `position`, `length`, `track` (moves the item to that track), `selected`,
  `muted`, `volume_db`, `fade_in`, `fade_out`, and a `take` object
  (`name`, `volume_db`, `pan`, `pitch` in semitones, `playrate`,
  `preserve_pitch`). Take fields are ignored for an empty item (no take).

Returns `{ created: [item...], updated: [item...] }`.

### POST /state/items/{index}

Update one item — same fields as a single `update` element above. Returns the
updated item.

### POST /state/items/{index}/split

Split an item at `{ "position": SECONDS }` (absolute timeline position, must be
inside the item, else 400). Returns `{ left: item, right: item }`.

### DELETE /state/items/{index}

Remove an item from its track. Returns `{ deleted, index }`.

### Track extras (in GET/POST /state/tracks)

Track reads and writes gain: `phase` (bool), `n_channels` (int, 2–128 even),
`pan_mode` (0=classic, 3=balance, 5=stereo, 6=dual), `dual_pan_l`/`dual_pan_r`
(−1..1, used when `pan_mode`=6), `rec_input` (int, <0 = none), `midi_hw_out`
(int, <0 = disabled), `main_send` (bool — sends audio to parent).

### FX additions

- FX reads (`GET /state/tracks/{index}/fx/{slot}` and the `fx[]` in track reads)
  include `offline` (bool). `POST .../fx/{slot}` and `POST .../fx` accept
  `offline` to take an FX online/offline.
- **POST /state/tracks/{index}/fx/{slot}/copy** — copy or move this FX to another
  track. `{ "to_track": j, "to_slot": -1, "move": false }` (`to_slot` −1 appends).
  Returns `{ from_track, from_slot, to_track, moved, dest_fx_count }`.

### Project ext state — `/project/extstate`

Per-project persistent scratchpad stored inside the `.rpp` (survives
close/reopen, unlike the global SQLite store).

- **GET /project/extstate?section=S&key=K** → `{ section, key, value }` (value
  `null` if unset). With only `section`: `{ section, values: { key: value, ... } }`.
- **POST /project/extstate** `{ section, key, value }` (all strings) → stores it.
- **DELETE /project/extstate?section=S&key=K** → removes that key.

> Note: REAPER stores ext-state keys case-insensitively and reports them
> upper-cased in the enumerated `values` map; look-ups by `key` are
> case-insensitive.

---

## Project lifecycle (issue #34)

Four endpoints that manage the full project lifecycle without any GUI modal. All
return **409 UNSAVED_CHANGES** when the current project has unsaved changes and
`discard_changes` is not `true`.

### POST /project/new

Open a blank project from REAPER's default template, replacing the current project.

```json
{ "discard_changes": false }
```

Returns `{ "ok": true }`.

### POST /project/open

Open a `.rpp` file, replacing the current project. Multi-project tab mode is not
supported — the file always replaces the active project.

```json
{ "path": "/absolute/path/to/project.rpp", "discard_changes": false }
```

Returns `{ "ok": true, "path": "/absolute/path/to/project.rpp" }`. Returns **400**
if `path` is missing or the file does not exist.

### POST /project/save

Save the current project. If `path` is provided, saves to that file and updates
the project's current filename (save-as). If `path` is omitted, saves in-place
to the existing filename; returns **400** if the project has never been saved.

```json
{ "path": "/absolute/path/to/output.rpp" }
```

Returns `{ "ok": true, "path": "/absolute/path/to/output.rpp" }`.

### POST /project/reset

Blank the current project in-place without opening a new file. Deterministic on a
headless/virtual display — no GUI modal.

Deletes: all tracks, items, envelopes, markers, regions, and extra tempo markers.
Resets: tempo to 120 BPM 4/4, time selection, loop range, cursor to 0, project notes.

```json
{ "discard_changes": false }
```

Returns `{ "ok": true, "tracks": 0, "markers": 0, "tempo_markers": 1 }`.

---

## MIDI verbs (issue #51)

Structured read/write for the MIDI content of a media item's active take. Items are
addressed by the same project-wide index as `GET /state/items`.

### GET /state/items/{index}/midi

Returns all MIDI notes and CC events from the active MIDI take.

Returns 404 when the item index is out of range. Returns 400 when the active take is
not a MIDI source.

```json
{
  "item_index": 0,
  "note_count": 3,
  "cc_count": 1,
  "notes": [
    {
      "index": 0,
      "pitch": 60,
      "note": "C4",
      "channel": 0,
      "velocity": 100,
      "start_ppq": 0.0,
      "end_ppq": 480.0,
      "start_time": 0.0,
      "end_time": 0.5,
      "selected": false,
      "muted": false
    }
  ],
  "cc": [
    {
      "index": 0,
      "chanmsg": 176,
      "number": 7,
      "value": 100,
      "channel": 0,
      "ppq": 0.0,
      "time": 0.0,
      "selected": false,
      "muted": false
    }
  ]
}
```

PPQ positions are take-relative (PPQ 0 = start of the item). `start_time`/`end_time`
are project times in seconds (absolute). `note` is the human-readable name (e.g. `"C4"`).
`chanmsg` is the MIDI status byte without the channel nibble (176 = 0xB0 = Control Change).

### POST /state/items/{index}/midi

Insert or replace MIDI notes and CC events. Wrapped in a single undo block.

```json
{
  "notes": [
    { "pitch": 60, "channel": 0, "velocity": 100, "start_ppq": 0.0, "end_ppq": 480.0 },
    { "pitch": 64, "channel": 0, "velocity": 80,  "start_time": 0.5, "end_time": 1.0 }
  ],
  "cc": [
    { "number": 7, "value": 100, "channel": 0, "ppq": 0.0 },
    { "number": 11, "value": 64,  "channel": 0, "time": 1.0 }
  ],
  "replace": false
}
```

**Field rules:**
- `pitch` — required, 0–127
- `channel` — optional, 0–15, default 0
- `velocity` — optional, 1–127, default 100
- Position: either `start_ppq`/`end_ppq` (take-relative PPQ) **or** `start_time`/`end_time`
  (project seconds). PPQ takes priority when both are present. Omitting the end position
  defaults to one quarter note (480 PPQ).
- `cc.number` — required, 0–127 (controller number)
- `cc.value` — required, 0–127
- `cc.chanmsg` — optional (defaults to 176 = 0xB0 = Control Change)
- `replace` — `false` (default) appends; `true` deletes all existing notes and CC first

**Response:**
```json
{
  "ok": true,
  "notes_inserted": 2,
  "cc_inserted": 1,
  "notes_deleted": 0,
  "cc_deleted": 0,
  "warnings": []
}
```

Validation errors for individual events are collected in `warnings[]` (the rest of
the batch still proceeds). A missing or unknown item index returns 404; a non-MIDI
take returns 400.

---

## Phase 4 — Audio perception (v1.5.0, #18)

"The agent hears itself." Built-in, always-available analysis plus consequence
hints. Every measure carries a **`method`** and **`confidence`** so the agent
knows how much to trust it:

| method | meaning | confidence |
|--------|---------|-----------|
| `offline_analysis` | REAPER's exact offline decode (loudness/peak via `CalculateNormalization`) | 1.0 |
| `derived` | computed from an exact measure (clipping from true-peak) | 1.0 |
| `introspection` | REAPER's own live meters | 1.0 |
| `estimated_dsp` | ReaClaw's own DSP over decoded samples (spectral FFT) | 0.6 |

### GET /analysis/item/{index}

Analyse a media item's active take source. Query: `measures=` (comma list of
`loudness` / `spectral`, default both), `start=` / `end=` (source-relative
seconds; default whole source). 400 if the item is empty or a MIDI take (no
audio source). Runs on the main thread with a 30 s budget — for very long
sources, pass a `start`/`end` window (else a `408` is possible).

```json
{
  "item_index": 1,
  "source": { "file": "...", "type": "WAVE", "length": 1.5, "sample_rate": 44100, "num_channels": 1 },
  "window": { "start": 0.0, "end": null },
  "loudness": { "lufs_i": -22.3, "rms_i": -21.7, "peak_db": -18.1, "true_peak_db": -18.1,
                "method": "offline_analysis", "confidence": 1.0 },
  "clipping": { "digital": false, "inter_sample": false, "true_peak_db": -18.1,
                "method": "derived", "confidence": 1.0 },
  "spectral": { "low": 0.00001, "mid": 0.9999, "high": 1e-8, "centroid_hz": 440.0,
                "dominant_band": "mid", "frames_analyzed": 17,
                "bands_hz": { "low": "<250", "mid": "250-4000", "high": ">4000" },
                "method": "estimated_dsp", "confidence": 0.6 }
}
```

- **loudness** — `lufs_i` (integrated LUFS), `rms_i`, `peak_db`, `true_peak_db`
  (all dBFS/LUFS; `-150` = silence).
- **clipping** — `digital` (sample peak ≥ 0 dBFS), `inter_sample` (true-peak > 0 dBTP).
- **spectral** — fractional energy in three bands (sum ≈ 1) + spectral
  `centroid_hz`; a rough digest, not a calibrated analyzer.

### GET /analysis/file?path=…

Same payload for an arbitrary audio file (e.g. a freshly rendered stem/mix).
`path` must be an absolute path REAPER can decode; 404 if it can't be opened.

### GET /state/meters

Live per-track and master peak metering — `peak_db` and `peak_hold_db` as
`[L, R]` arrays in dBFS, plus `audio_running`. These are REAPER's own meters
(`method: introspection`) and are only meaningful while audio is running
(play/record); `-150` = no signal.

### Consequence-aware hints

Mutating responses for **track update**, **add FX**, **add send**, and **item
create/update** now carry a `hints` array — the *consequence of that specific
edit* against the current session:

```json
"hints": [ { "code": "muted_track", "severity": "warn",
             "message": "This track is on track 0, which is muted — you won't hear it." } ]
```

Invariants (hand-authored set): `muted_track`, `solo_elsewhere`,
`near_silent_fader`, `routes_nowhere`, `phase_inverted`, `recarm_no_input`,
`fx_offline`, `fx_bypassed`, `send_dest_routes_nowhere`, `send_dest_muted`,
`empty_item`, `midi_no_instrument`. Empty array when nothing trips.

---

## Phase 4 — Audio visualization (#19, Q4)

Pictures of audio for when a curve reads faster than numbers. Each request
returns a machine-readable **`digest`** *and* (by default) a base64 PNG, so the
agent reads numbers rather than OCR-ing pixels. Built on the same offline decode
+ FFT as the analysis endpoints; tagged `method`/`confidence` like everything in
the perception layer.

### GET /analysis/item/{index}/visualize

Render a picture of a media item's active take source.

Query params:
- `type` — `spectrum` (default), `waveform`, or `loudness`.
- `start=` / `end=` — source-relative seconds (default whole source).
- `width=` (160–1024, default 640), `height=` (80–512, default 200).
- `image=none` — return the digest only, no PNG (cheaper). Default `png`.

The PNG is a dark-themed chart with labelled axes: spectrum is a log-frequency
**EQ curve** (Hz ticks at 100/1k/10k, dB scale), waveform/loudness carry a time
axis (seconds), and the level plots a dB scale (0/−12/−24/−48).

Long windows are capped at 120 s of decoded audio; `window.truncated` flags it.

```json
{
  "item_index": 1,
  "type": "spectrum",
  "source": { "file": "...", "type": "WAVE", "length": 4.0, "sample_rate": 44100, "num_channels": 1 },
  "window": { "start": 0.0, "end": 4.0, "analyzed_seconds": 4.0, "truncated": false },
  "digest": {
    "bands": [ { "hz_lo": 20.0, "hz_hi": 24.5, "db": -72.1 }, "… 32 log-spaced bands …" ],
    "peak_band_hz": 440.0, "centroid_hz": 441.3,
    "low": 0.01, "mid": 0.98, "high": 0.01, "dominant_band": "mid",
    "reference": "db relative to loudest band (curve shape)",
    "method": "estimated_dsp", "confidence": 0.6
  },
  "image": { "format": "png", "width": 480, "height": 160, "base64": "iVBORw0KGgo…" }
}
```

Digest by `type`:
- **spectrum** — 32 log-spaced `bands` (`hz_lo`/`hz_hi`/`db`, dB relative to the
  loudest band), `peak_band_hz`, `centroid_hz`, `low`/`mid`/`high` energy split,
  `dominant_band`. `estimated_dsp`, confidence 0.6.
- **waveform** — `peak_db`, `rms_db`, `clipping`, and a 32-point `envelope_db`
  peak envelope across the window. `estimated_dsp`, confidence 0.9.
- **loudness** — a `rms_contour_db` array (≤48 points) over time plus
  `min_db`/`max_db`/`mean_db`. `estimated_dsp`, confidence 0.85.

The PNG (teal signal on a labelled grid) is one the agent can `Read` directly.
A/B diff against an earlier snapshot is deferred to the shared snapshot layer.

### GET /analysis/file/visualize?path=…

Same payload and params for an arbitrary audio file (e.g. a rendered stem/mix).
`path` must be an absolute path REAPER can decode; 404 if it can't be opened.

---

## Phase 4 — Musical-attribute probes (#19, Q7)

A *probe* is the measure-counterpart of an action: it reads the material and
returns data instead of changing the project. Each result is tagged with its
**truth source** — exact `introspection` (the project already knows it, no
render) vs. `estimated_dsp` (decoded + analysed, carries `confidence`). Advanced
detection may use an **optional external tool**; when it is absent the probe
degrades gracefully (`available:false`) rather than failing.

> Probes are exposed as a flavour of the analysis surface (a `/probe`
> sub-resource), not a separate registry — see TECH_DECISIONS §17.

### GET /analysis/item/{index}/probe

Query: `probes=` (comma list of `pitch` / `key` / `tempo`, default all),
`start=` / `end=` (source-relative seconds). 404 if the item index is out of
range; 400 if the take has no audio source.

```json
{
  "item_index": 0,
  "source": { "file": "…", "type": "WAVE", "length": 4.0, "sample_rate": 44100, "num_channels": 1 },
  "pitch": { "note": "A4", "name": "A", "octave": 4, "frequency_hz": 440.3, "cents": 1.3,
             "midi": 69, "method": "estimated_dsp", "confidence": 0.95 },
  "key":   { "key": "A minor", "tonic": "A", "mode": "minor", "correlation": 0.68,
             "chroma": [ "… 12 pitch-class energies …" ],
             "method": "estimated_dsp", "confidence": 0.31 },
  "tempo": {
    "project":  { "bpm": 120.0, "timesig_num": 4, "timesig_denom": 4,
                  "method": "introspection", "confidence": 1.0,
                  "note": "the project tempo at this item's position — exact, not detected from audio" },
    "detected": { "available": false, "method": "estimated_dsp",
                  "note": "tempo-from-audio needs an optional external analyser (e.g. bpm-tools' `bpm-tag`) on PATH; not found." }
  }
}
```

- **pitch** — dominant fundamental → nearest equal-tempered note (`A4`) + signed
  `cents`. Built-in DSP (FFT peak, parabolic-interpolated), `estimated_dsp`.
- **key** — Krumhansl–Schmuckler correlation over a 12-bin chromagram → `tonic` +
  `mode`; `confidence` is the margin over the runner-up key. Built-in DSP.
- **tempo** — `project` is exact (`introspection`, the project tempo at the item's
  position); `detected` is the optional from-audio estimate via an external tool
  (`bpm-tag`), reported `available:false` when the tool isn't installed.

### GET /analysis/file/probe?path=…

Same for an arbitrary file. A loose file has no project timebase, so only the
`tempo.detected` (external) source applies — there is no exact `project` tempo.

---

## Phase 4 — On-demand screenshot (#19, Q5)

Structure-first is the default (use the `/state` reads); a screenshot is the
**fallback for GUI-only state** — custom plugin GUIs, metering displays — that
structured data can't express. Linux/X11 only; needs `ffmpeg` (x11grab) and, for
window framing, `xdotool`.

### GET /screenshot

Capture precedence: `region` > `window` > named `target`.

- `target=` — a **named surface**: `screen` (whole display, default),
  `arrange`/`reaper`, `mixer`, `fxchain`, `midi`, `routing`, `master`,
  `transport`, `explorer`. Each (except `screen`) auto-frames that REAPER window.
- `window=<title>` — frame the largest window whose title matches the substring.
- `region=x,y,w,h` — capture an explicit rectangle.
- `width=` — downscale the result to this width (keeps aspect) to bound token cost.

Capture rectangles are clamped to the screen, so a maximized window's geometry
won't make x11grab fail.

```json
{
  "framed": "arrange",
  "display": ":0.0",
  "region": { "x": 0, "y": 91, "w": 1920, "h": 989 },
  "image": { "format": "png", "width": 600, "height": 309, "base64": "iVBORw0KGgo…" },
  "note": "structure-first: prefer /state reads; use a screenshot only for GUI-only state"
}
```

Errors: `400` unknown target / malformed region; `404` the named surface isn't
open (`"No visible window matching '…'"`); `501` no `DISPLAY`, or `ffmpeg`/
`xdotool` missing; `503` the grab failed.

---

## Phase 4 — Snapshot / state-diff layer (#20 prep)

A shared, cross-cutting layer: capture a canonical project-state snapshot, store
it, and diff two snapshots (or a snapshot vs. live). It backs both the #19 A/B
visual diff and #20's correction mining ("what did the agent change, and was it
corrected?"). A snapshot is a focused, diff-stable slice — project (name, bpm,
`change_count`) and per-track name/volume/pan/mute/solo/arm/color/fx/sends/item-count.

### POST /snapshot

Capture the current state. Optional body `{ "label": "before mixdown" }`. Returns
`{ id, taken_at, label, summary: { track_count } }`.

### GET /snapshot · GET /snapshot/{id} · DELETE /snapshot/{id}

List stored snapshots (newest first), fetch one (`{ id, taken_at, state }`), or
delete one (`404` if absent).

### GET /snapshot/diff?from=&to=

Diff `from=<id>` against `to=<id>` — or, when `to` is omitted/`current`/`live`,
against a fresh live capture. Returns a flat change list:

```json
{
  "from": 3, "to": "current", "change_count": 2,
  "changes": [
    { "path": "tracks/0/muted", "op": "changed", "from": false, "to": true },
    { "path": "tracks/1/fx/0",  "op": "added",   "to": { "name": "ReaEQ", "enabled": true } }
  ]
}
```

`op` is `changed` (scalar differs; carries `from`+`to`), `added` (in `to` only),
or `removed` (in `from` only). Objects diff by key, arrays by index.

---

## Phase 4 — Learned suggestions (#20, Q8)

**The compounding moat.** Locally and opt-in, ReaClaw mines the agent's own edit
history into *"after X, agents usually do Y"* associations and surfaces them as
suggestions tagged `method:"learned"` with a `confidence`. Distinct from the
hand-authored `hints[]` of #18 (fixed invariants) though it shares the suggestion
channel — these are *learned from use*.

> **Local-first, opt-in, never phones home.** Off by default; set
> `learning.enabled=true` in `config.json` to turn it on. While disabled nothing
> is recorded. All state lives in the local SQLite DB — there is no network egress.

How it works: each structured edit (track create/update field, FX add, send add,
item create) is recorded as an event keyed to the agent. When the next edit by
the same agent falls within `learning.window_seconds`, the transition
(antecedent → consequent) is counted. `confidence = P(consequent | antecedent)`;
a pattern surfaces once it clears `min_support` and `min_confidence`.

### GET /suggestions?after=&agent=&limit=

Learned suggestions for what usually follows `after` (or the agent's most recent
edit when `after` is omitted; `agent` defaults to the `X-Agent-Id` header).

```json
{
  "enabled": true, "agent": "sparky", "after": "track.create",
  "suggestions": [
    { "after": "track.create", "suggest": "track.set:color", "support": 7,
      "confidence": 0.64, "method": "learned" }
  ],
  "note": "learned from this machine's local edit history only; advisory, not automatic."
}
```

When learning is disabled, returns `{ "enabled": false, "suggestions": [], "note": … }`.

### GET /learn/stats

What the learner has accumulated locally: `{ enabled, events, patterns, agents,
window_seconds, min_support, min_confidence }`.

### Config

```json
"learning": { "enabled": false, "window_seconds": 180, "min_support": 3, "min_confidence": 0.3 }
```

---

## Phase 5 — Offline render engine (#32 / #33)

### POST /render

Trigger an offline render to a file. Render runs on the main thread synchronously
(timeout 300 s, enough for ~100 min of project at 20× real-time). Render settings
are saved and restored after each call so the project's configured render
configuration is never permanently changed by an agent render.

**Request body** (all fields optional except `output`):

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `output` | string | **required** | Full path for the rendered file (directory must exist) |
| `format` | string | `"wav"` | `"wav"` \| `"flac"` \| `"mp3"` \| `"ogg"` |
| `bit_depth` | int | `24` | `16`, `24`, or `32` — WAV and FLAC only |
| `srate` | int | `44100` | Sample rate in Hz (8000–192000) |
| `channels` | int | `2` | Number of output channels (1–64) |
| `bounds` | string | `"project"` | `"project"` \| `"time_selection"` \| `"all_regions"` \| `"custom"` |
| `start` | float | `0.0` | Render start in seconds — required when `bounds="custom"` |
| `end` | float | `0.0` | Render end in seconds — required when `bounds="custom"`, must be > `start` |
| `mp3_bitrate` | int | `192` | CBR bitrate in kbps — MP3 only |
| `flac_compression` | int | `5` | Compression level 1–8 — FLAC only |

```json
{
  "output": "/tmp/mix.wav",
  "format": "wav",
  "bit_depth": 24,
  "srate": 44100,
  "channels": 2,
  "bounds": "project"
}
```

**Response:**

```json
{
  "output_path": "/tmp/mix.wav",
  "format": "wav",
  "srate": 44100,
  "channels": 2,
  "render_seconds": 0.36,
  "project_length": 8.0,
  "offline_ratio": 22.2,
  "rendered_at": "2026-06-26T10:00:00Z",
  "warnings": []
}
```

| Field | Description |
|-------|-------------|
| `output_path` | Full path of the rendered file |
| `render_seconds` | Wall-clock time the render took |
| `project_length` | Project length at render time (seconds) |
| `offline_ratio` | `project_length / render_seconds` — speed multiple (e.g. 22× real-time) |
| `rendered_at` | ISO-8601 UTC timestamp |
| `warnings` | Non-fatal notices (e.g. unsupported fields, bit-depth clamping) |

**Notes:**
- FLAC does not support 32-bit float; `bit_depth=32` is automatically clamped to 24 with a warning.
- `bounds="custom"` temporarily sets the project time selection to `[start, end]` and renders with `RENDER_BOUNDSFLAG=1` (time selection), then restores the original time selection.
- The `normalize` field is accepted but not yet implemented (warning emitted).
- Errors: `400` for bad/missing parameters, `408` for render timeout, `500` for REAPER API failure.

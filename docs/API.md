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
`408 Request Timeout`, `409 Conflict` (unsaved changes on project lifecycle calls;
cancelling a running render job), `500 Internal Server Error`, `501 Not Implemented`
(platform-unsupported, e.g. `/screenshot` or `/reaper/restart` off Linux),
`503 Service Unavailable` (screenshot grab failed)

Issue #72: the most commonly-guessed-wrong `400`s (`POST /render`, `/execute/action`,
`/execute/script`, `/scripts/register`, `/project/open`, `/project/save`) include a
`context.schema` describing every accepted field, so a wrong guess doesn't cost a
second round trip to discover the right one.

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

### GET /

Landing page — the **only endpoint served without authentication** (by design: it
carries orientation info only, never project data). A fresh agent hits `GET /` and
gets enough to orient: what ReaClaw is, the version, a 9-step quick-start recipe,
key gotchas, and a pointer to `GET /capabilities`.

```json
{
  "what_i_am": "ReaClaw — REST/JSON API for REAPER DAW. ...",
  "version": "1.16.0",
  "auth": "Authorization: Bearer <token> on all requests except GET /",
  "quick_start": ["1. GET /capabilities — full machine-readable API manifest (read first)", "..."],
  "key_gotchas": ["..."],
  "endpoints": { "capabilities": "GET /capabilities — full API manifest", "...": "..." }
}
```

### GET /agent/guide

The **agent onboarding manual** — `docs/AGENT_GUIDE.md`, embedded into the
binary at build time and served verbatim, so the copy an agent fetches always
matches the running version (single source of truth, no drift). It covers
connection discovery, the latency contract, the external-change sync protocol,
a cheat sheet of the common verbs, and the trap list — enough for any AI
harness (any vendor) to self-configure its own skill/memory equivalent from
one call. Requires auth like everything except `GET /` (which points here).

- Default: `200`, `Content-Type: text/markdown; charset=utf-8`, body = the guide.
- `?format=json`:

```json
{
  "version": "1.18.0",
  "markdown": "# ReaClaw Agent Guide\n…",
  "links": { "capabilities": "GET /capabilities", "catalog_search": "GET /catalog/search?q=",
             "recipes": "GET /recipes", "events": "GET /events?since=",
             "changes": "GET /state/changes" }
}
```

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
`"degraded"` and a `degraded_reason` field is added. `POST /queue/flush`
(issue #64) drops every *pending* (not-yet-executing) command so callers
blocked behind the backlog return immediately instead of waiting out their
timeout — it does not itself unwedge whatever call is actually stuck
executing on the main thread; only restarting REAPER recovers that.

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

### POST /queue/flush

Drains the pending command backlog (issue #64). Resolves every queued-but-
not-yet-executing command with `{"_flushed": true}` so its caller unblocks
immediately. Returns `{ "flushed": N }`.

### POST /reaper/restart

Kills and relaunches the REAPER process ReaClaw is embedded in — for
self-recovery when the main thread is actually wedged (not just a pending
backlog — `/queue/flush` alone can't fix that; only a full restart can).
Linux only (501 elsewhere).

```json
{ "save_project": true }
```

- `save_project` *(optional, bool, default `true`)* — best-effort in-place
  save before restarting, with a short (5s) timeout so a wedged main thread
  doesn't hold up the recovery path itself. Skipped (not attempted) if the
  project has never been saved (no filename yet) — no save-as dialog is ever
  triggered.

```json
{
  "restarting": true,
  "saved": true,
  "pid": 12345,
  "restart_command": ["/home/user/opt/REAPER/reaper", "-nosplash", "-newinst", "..."]
}
```

`restart_command` is REAPER's own current `argv`, read live from
`/proc/self/cmdline` at request time (this process *is* REAPER — ReaClaw is a
shared library loaded inside it) — surfaced so the caller can see exactly
what's about to relaunch. The full current environment
(`/proc/self/environ`, including `DISPLAY`/`XAUTHORITY`) is replayed
byte-for-byte on relaunch too, so the new instance's display/X-auth context
is exactly what's already working, not reconstructed from whatever shell
issues the HTTP request.

**Sequence:** best-effort save → fork a detached helper → respond
immediately (REAPER keeps running normally for the response to reach you) →
~300ms later the helper sends `SIGTERM`, waits up to 8s, escalates to
`SIGKILL` if still alive, then relaunches with the captured argv/environment.
Poll `GET /health` until it responds again (a fresh instance has
`uptime_seconds` near 0).

Any other in-flight request at the moment of the kill will simply see its
connection dropped, not a graceful error — expected given what this
endpoint does.

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

Full-text search (SQLite FTS5) across action names and categories by default; optional
embedding-based **semantic** ranking (issue #10) — see below.

Query params: `q` (required), `limit` (default 20), `category`, `section=midi_editor`,
`semantic=true` (opt-in, see below).

**Synonym expansion (strict-first, keyword mode):** the literal query runs first, so precise
queries keep their precision. On a miss, the query is widened through a curated synonym map
(e.g. "folder depth" → "indent", "bounce" → "render", "colour" → "color") — AND-of-synonym-
groups first, then OR-of-all as a last resort. The response echoes `matched` (the FTS
expression actually used) and `expanded` (whether widening kicked in). Every response also
carries `mode` (`"keyword"` or `"semantic"`) so the caller knows which path served it.

Each action carries three heuristic flags, name/category-based (not guarantees):
- `interactive` — opens a modal dialog (ellipsis/prompt/known-modal id); a headless agent
  should avoid these or route them through a structured verb instead.
- `mutates_state` — `false` only for REAPER's own `View:`/`Options:`-prefixed UI/session-only
  toggles; everything else defaults `true` (the safer assumption).
- `requires_selection` — `true` when the name says "selected" (REAPER's own convention for
  selection-dependent actions).

```json
{ "query": "set folder depth", "mode": "keyword", "matched": "set OR folder OR indent OR depth …",
  "expanded": true, "total": 3,
  "actions": [ { "id": 53609, "name": "SWS: Indent selected track(s)", "interactive": false,
                 "mutates_state": true, "requires_selection": true } ] }
```

**Semantic search (opt-in, off by default).** Pass `semantic=true` to rank by an embedding
model instead of keywords — catches phrasing the synonym map misses (e.g. "make the drums
quieter" → volume actions). Requires **both**:
1. `semantic_search.enabled: true` in `config.json` (off by default — see below).
2. `semantic=true` on the request.

If either is off, or the semantic path fails for any reason (Ollama unreachable, embedding
cache build failed), the request **silently falls back to keyword search** — never an error.
Results carry a `score` (cosine similarity, 0–1) instead of `matched`/`expanded`:

```json
{ "query": "make the drums quieter", "mode": "semantic", "total": 1,
  "actions": [ { "id": 40308, "name": "Track: Nudge volume for master track down",
                 "score": 0.81, "interactive": false, "mutates_state": true,
                 "requires_selection": false } ] }
```

**Config** (`config.json`, all optional, shown with defaults):

```json
{ "semantic_search": { "enabled": false, "ollama_url": "http://127.0.0.1:11434", "model": "nomic-embed-text" } }
```

`ollama_url` must resolve to loopback (`127.0.0.1`/`localhost`/`::1`) — a non-loopback URL is
rejected instantly (no network attempt), falling back to keyword. See
`ReaClaw_TECH_DECISIONS.md` §25 for why this is a deliberate, narrow exception to ReaClaw's
"no network egress" stance (§11): an embedding model, not a generative one, called only to
rank ReaClaw's own catalog — the same call the MCP client (`mcp/`) already makes, now
available without it.

The catalog's embeddings are built lazily on first semantic search and cached (keyed by
catalog size + model, invalidated automatically on a catalog rebuild or model change) — the
first call after a cache miss embeds every action name and can take a while (minutes, on a
CPU-only local Ollama, for the ~6700-action main catalog); subsequent calls are fast
(sub-second, only the query itself needs embedding).

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

### GET /recipes · GET /recipes/{id}

Vetted, structured recipes (issue #10) — the same snippets curated in
`skill/reaclaw/SKILL.md`, exposed as JSON so a plain REST/MCP client without the Skill
loaded still gets them: folder-group session build, search-then-run an action, register
+ run a Lua script, add + tune an FX, add a send, and the screenshot-verify technique.

`GET /recipes` lists all; `GET /recipes/{id}` fetches one (404 if unknown). Each recipe is
`{id, title, description, steps: [...], notes?}`; each step is either an HTTP call
(`{description, method, path, body?}`) or a shell command (`{description, command}` — only
the screenshot recipe, which needs `ffmpeg`).

```json
{
  "id": "search_then_run_action",
  "title": "Search the action catalog, then run what you find",
  "description": "For anything without a structured verb: search first ...",
  "steps": [
    { "description": "Search for a matching action", "method": "GET",
      "path": "/catalog/search?q=mute%20drums" },
    { "description": "Run the action you picked from the results", "method": "POST",
      "path": "/execute/action", "body": { "id": 40702 } }
  ],
  "notes": "Response includes action_name for confirmation. ..."
}
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

Create and/or batch-update tracks in one call — this array *is* the "bulk
create" endpoint (issue #79: no separate `/tracks/bulk` resource — pass N
specs in `create`). `create` appends tracks in order; `update` patches
existing tracks by `index`. Each spec accepts the same writable fields as
`POST /state/tracks/{index}`, plus an optional `instrument` (string, VSTi/CLAP
name) added via `TrackFX_AddByName` right after creation — one call instead
of a separate follow-up `POST .../fx`.

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
{ "track": 2, "slot": 0, "guid": "{A1B2C3D4-...}",
  "name": "VST: ReaComp (Cockos)", "enabled": true }
```

`params` values are **normalized 0..1**, referenced by `index` or `name`.
Returns 400 (`FX not found: ...`) if the plugin can't be resolved.

### GET /state/tracks/{index}/fx/{slot}[?limit=&offset=&q=]

FX slot detail incl. parameter list (`param_count` reports the true total).
Each param: `index`, `name`, normalized `value` (0..1), `formatted` (display
string), and the real-unit range `raw` (current value), `min`, `max`, `mid` —
so an agent can reason in real units. Issue #74: `limit`/`offset` paginate
(default: all params, no limit), `q` filters by case-insensitive name
substring — needed for big plugins (Surge XT: 2147 params).

Each param also carries a `modulation` object (issue #100): `lfo_active`,
`acs_active`, `plink_active` (bools) report whether the param is already
wired to an LFO, audio-rate (ACS) modulation, or a parameter-link/MIDI-CC
binding — so an agent doesn't set a value only to have it silently
overridden. When `plink_active` is true, a `plink` object gives the binding
detail: `effect` (`-100` = MIDI-CC link, per REAPER's own sentinel), `param`,
`midi_bus`, `midi_chan`, `midi_msg`, `midi_msg2`, `scale`, `offset` — mirrors
`TrackFX_GetNamedConfigParm`'s `param.X.plink.*` fields directly.

`{slot}` accepts either the numeric chain index or the FX's `guid` string
(issue #102, see "FX additions" below) — both resolve to the same FX.

### POST /state/tracks/{index}/fx/{slot}

Set `enabled` (bool) and/or `params` (`[{index|name, value?, plink?}]`).
`value` (normalized 0..1) and `plink` are independent — either, both, or
neither may be present per entry. `plink` (issue #100) sets or clears a
parameter-link/MIDI-CC binding, e.g.
`{"index":0,"plink":{"active":true,"effect":-100,"midi_chan":0,"midi_msg":176,"midi_msg2":1}}`;
`{"plink":{"active":false}}` clears it. Returns the updated FX slot with its
params.

### DELETE /state/tracks/{index}/fx/{slot}

Remove an FX slot.

### GET /state/tracks/{index}/fx/{slot}/pins

Issue #101 — the plugin's I/O pin count and channel routing, distinct from
the track-level `sends` verbs (this is routing *within* the plugin's own
pins). `{ track, slot, guid, supported, input_pins, output_pins, inputs: [
{ pin, channels: [...] } ], outputs: [ ... ] }`. `channels` is the decoded
0-based track-channel list each pin is wired to (from the raw 64-bit
`TrackFX_GetPinMappings` bitmask). `supported: false` if the plugin doesn't
expose pin data.

### POST /state/tracks/{index}/fx/{slot}/pins

Set one or more pins' channel mapping (replaces each pin's full mapping, not
additive): `{ "pins": [ { "pin": 0, "output": false, "channels": [0,1] } ] }`.
Returns the same shape as the GET.

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
                "midi": {"status":"structured","note":"notes + CC read/write via GET/POST /state/items/{i}/midi"},
                "control_surface": {"status":"out_of_scope","note":"…"} },
  "sdk": { "functions_total": 868, "functions_called": 188, "raw_pct": 21.7,
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

### POST /execute/script

One-shot script execution (issue #69): register + run (+ by default,
deregister) in a single call, for short throw-away scripts that don't earn a
place in the script library. Skips the `/scripts/register` → `/execute/action`
round trip.

```json
// Request
{ "script": "reaper.SetTempoTimeSigMarker(0,-1,0,-1,-1,95,4,4,false)" }

// Response
{ "status": "success", "executed_at": "2026-06-30T18:00:00Z" }
```

**Fields:**
- `script` *(required, string)* — Lua body (same rules as `/scripts/register`: no `ShowConsoleMsg`, syntax-checked with `luac` if available)
- `name` *(optional, string)* — default: auto-generated, unique per call
- `ephemeral` *(optional, bool, default `true`)* — deregister immediately after running; pass `false` to keep it in the library (response then includes `action_id`), equivalent to the two-step register+execute flow
- `feedback` *(optional, bool)* — same as `/execute/action`
- `timeout_ms` *(optional, int)* — same as `/execute/action`

A syntax error returns the same `{"registered": false, "syntax_error": {...}}`
shape as `/scripts/register`. A Lua runtime error returns `{"status":
"lua_error", "lua_error": "..."}`, same as `/execute/action`.

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

Set/add a tempo/time-sig marker. `{ time?, bpm, timesig_num?, timesig_denom?, time_signature?, linear? }`.
`time` defaults to `0.0` (issue #70) — `{"bpm": 95}` alone sets the project's
starting tempo, updating the marker at that position rather than duplicating
it. `time_signature` accepts a `"4/4"` string as sugar for `timesig_num`/`timesig_denom`.

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
- Each `fx[]` entry also carries `is_inline_eq` (bool) and, when `false`,
  `agent_slot` (int) — issue #66: REAPER auto-adds a disabled inline ReaEQ to
  every new track as raw slot 0, shifting the first real plugin to slot 1.
  `agent_slot` numbers only the non-inline-EQ entries (0, 1, 2, ...) so an
  agent that ignores `is_inline_eq: true` entries never hits the off-by-one.
  The raw REAPER `slot` (used by all `{slot}` path params) is unchanged.
- **POST /state/tracks/{index}/fx/{slot}/copy** — copy or move this FX to another
  track. `{ "to_track": j, "to_slot": -1, "move": false }` (`to_slot` −1 appends).
  Returns `{ from_track, from_slot, to_track, moved, dest_fx_count }`.
- **`guid` field** (issue #102) — every FX read/write response (`fx[]` entries,
  `GET`/`POST`/`DELETE .../fx/{slot}`, `.../copy`, `.../preset`, `.../pins`)
  carries a stable `guid` string (`TrackFX_GetFXGUID`, e.g.
  `"{A1B2C3D4-...}"`). Unlike the chain-positional `slot`, the GUID identifies
  *this plugin instance* regardless of later chain insertions/deletions/
  reorders. `{slot}` in every one of those routes accepts either the numeric
  index or the GUID string — resolved by trying an integer parse first, then
  falling back to a GUID match against the chain.

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

> **Issue #80 — use this, not action `40023`.** Firing REAPER's native
> "File: New project" action (id `40023`) via `/execute/action` opens a
> "Save current project?" modal when a project is already loaded — headless
> REAPER has no one to dismiss it, so the call blocks the main thread forever
> and wedges the command queue (recover with `POST /queue/flush` +, if the
> main thread itself is stuck, a REAPER restart). `POST /project/new` calls
> `Main_openProject("")` directly and never shows that dialog.

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

## Transport verbs (issue #49, #67, #71)

Backed by `CSurf_On*` rather than `Main_OnCommand` action IDs, so state-change
semantics are unambiguous and version-stable.

### GET /transport

Live transport position (issue #67) — bypasses the 1s `/state` cache
entirely, safe to poll during playback for accurate `position`.

```json
{ "playing": true, "paused": false, "recording": false, "position": 4.231,
  "loop_enabled": true, "loop_start": 0.0, "loop_end": 10.105 }
```

### POST /transport

```json
{ "action": "play" }   // "play" | "stop" | "pause" | "record"
```

Returns `{ "action": "play", "transport": { "playing": true, "recording": false, "paused": false, "position": 0.0 } }`.

### POST /transport/play · /transport/stop · /transport/pause · /transport/record

Aliases for `POST /transport` with the action baked into the route (issue #71
— a guessed `POST /transport/play` no longer 404s). Same response shape.

### POST /transport/cursor

```json
{ "position": 4.0, "moveview": false, "seekplay": false }
```

Returns `{ "position": 4.0 }`.

### POST /transport/loop

```json
{ "start": 0.0, "end": 8.0, "enabled": true }   // all fields optional; provide start+end and/or enabled
```

Returns `{ "start": 0.0, "end": 8.0, "enabled": true }`.

---

## Take-FX verbs (issue #50, v1.10.0)

The full `TakeFX_*` surface, mirroring the track-FX endpoints one-for-one at
`/state/items/{index}/takes/{take}/fx/...`. Items are addressed by the same
project-wide index as `GET /state/items`; `take` is the take index within the
item (`0` = first take). All mutations are wrapped in undo blocks. 404 if the
item or take index is out of range; 400 (`FX slot out of range`) for a bad slot.
Like the track-FX surface, `{slot}` accepts either the numeric chain index or
the FX's `guid` string (issue #102), and every response carries `guid`
(`TakeFX_GetFXGUID`).

### POST /state/items/{index}/takes/{take}/fx

Add an FX to a take by name — same body as `POST /state/tracks/{index}/fx`:
`{ "name": "ReaComp", "enabled": true, "offline": false, "params": [...] }`.
Returns `{ item, take, slot, guid, name, enabled, offline }`. 400
(`FX not found: ...`) if the plugin can't be resolved.

### GET /state/items/{index}/takes/{take}/fx/{slot}

Take-FX slot detail: `{ item, take, slot, guid, name, enabled, offline, params }`.
`params` is the full list (no pagination — unlike the track-FX read); each param
carries the same `index`/`name`/`value`/`formatted`/real-unit/`modulation`
fields as the track-FX read (issue #100).

### POST /state/items/{index}/takes/{take}/fx/{slot}

Set `enabled` (bool), `offline` (bool), and/or `params`
(`[{index|name, value?, plink?}]`, `value` normalized 0..1, `plink` sets/clears
a MIDI-CC/param-link binding — issue #100, same semantics as the track-FX
write). Returns the updated slot.

### DELETE /state/items/{index}/takes/{take}/fx/{slot}

Remove a take-FX slot.

### POST /state/items/{index}/takes/{take}/fx/{slot}/copy

Copy or move this FX to another take:
`{ "to_item": i, "to_take": t, "to_slot": -1, "move": false }` (`to_slot` −1
appends; `to_item`/`to_take` required).

### GET · POST /state/items/{index}/takes/{take}/fx/{slot}/preset

Read the current preset, load one by `{ "name": "..." }`, or step with
`{ "navigate": -1|1 }` — same semantics as the track-FX preset endpoints.

### GET · POST /state/items/{index}/takes/{take}/fx/{slot}/pins

Issue #101 — I/O pin count and channel routing for a take FX, same shape and
semantics as the track-FX `.../pins` endpoints above.

---

## MIDI verbs (issue #51)

Structured read/write for the MIDI content of a media item's active take. Items are
addressed by the same project-wide index as `GET /state/items`.

### GET /state/items/{index}/midi[?limit=&offset=]

Returns MIDI notes and CC events from the active MIDI take. `limit` (default
200, max 5000) and `offset` (default 0) paginate both `notes` and `cc`
independently (issue #75 — items with thousands of notes need this rather
than dumping everything on every call); `note_count`/`cc_count` always report
the item's true totals regardless of pagination.

Returns 404 when the item index is out of range. Returns 400 when the active take is
not a MIDI source.

```json
{
  "item_index": 0,
  "note_count": 3,
  "cc_count": 1,
  "limit": 200,
  "offset": 0,
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
A/B diff against an earlier snapshot is `GET /snapshot/diff/visualize` (#53),
in the snapshot section below.

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

**Optional audio target (issue #53)** — pass `audio: { "item": <index> }` or
`audio: { "file": "<path>" }` (plus optional `start`/`end`, source-relative
seconds) to also freeze a reference to a piece of audio for later A/B visual
diffing. `item` is resolved to its active take's source file path *at capture
time* — 400 if the item has no audio source. The response echoes back
`audio: { item?, file, start, end }` when set.

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

### GET /snapshot/diff/visualize?from=&to=&type=&width=&height=&image=

The A/B **visual** diff (issue #53) — paired visualizations of a snapshot's
audio target vs. current (or another snapshot), plus a digest delta. Both the
`from` and (when `to` is another snapshot id) `to` snapshots must have been
captured with an `audio` target — `400` if not (`"capture one with
audio:{item|file} in POST /snapshot"`).

- `from=<id>` *(required)* — always uses its frozen file + window.
- `to=<id>|current|live` *(default current)* — when `current`/omitted and the
  `from` audio was `item`-based, re-resolves that **same item index** live
  (picks up a changed take/source); when `from` audio was `file`-based, reuses
  the same literal path, re-decoded fresh. When `to` is another snapshot id,
  uses *its* frozen reference.
- `type=spectrum|waveform|loudness` (default `spectrum`), `width=`, `height=`,
  `image=none` — same params/semantics as `/analysis/*/visualize`.

```json
{
  "from": 1, "to": "current", "type": "spectrum",
  "images": {
    "from": { "item_index": null, "source": {...}, "window": {...}, "digest": {...}, "image": {...} },
    "to":   { "source": {...}, "window": {...}, "digest": {...}, "image": {...} }
  },
  "digest_delta": [
    { "path": "centroid_hz", "op": "changed", "from": 478.1, "to": 922.2 },
    { "path": "bands/14/db", "op": "changed", "from": 0.0, "to": -80.0 }
  ]
}
```

`images.from` / `images.to` are each the exact same shape as
`GET /analysis/file/visualize`'s response. `digest_delta` is the same
`jsondiff` shape as `/snapshot/diff`'s `changes`.

**Scope limitation, by design:** like the rest of the analysis surface (see
§17 of `ReaClaw_TECH_DECISIONS.md`), this analyses an item's *source file*,
not the post-fader/FX/mix signal — track-level edits (volume, mute, FX, pan)
that don't touch the source file produce no diff here. A frozen `file`-based
target is the way to diff two actual renders written to disk.

---

## Change Detection (issue #31)

Know that a human at the GUI, a control surface, or another API client changed the project
— not just what *you* changed. Three complementary paths, cheapest first:

### GET /state/changes

A monotonic counter (`GetProjectStateChangeCount`) — increments on essentially any project
edit, from any source. Cache the value; re-read state only when it advances.

```json
{ "change_count": 42 }
```

Resets on REAPER restart. Cheapest possible check; tells you *that* something changed, not
what — pair with `GET /snapshot/diff` or `GET /events` to find out.

### GET /events?since=&limit=

Granular, attributed events via an `IReaperControlSurface` hook — the real push feed, polled.
Covers track list/volume/pan/mute/selected/solo/recarm/title and play/repeat state.

> **Windows:** the event feed is disabled — the feed always reads empty and
> `/events/stream` never emits. Registering the C++ control-surface object from
> the MinGW-built DLL into MSVC-built REAPER corrupts the host's input handling
> (issue #111). Poll `GET /state/changes` (unaffected, C API) until the ABI
> shim lands.

- `since` *(optional, default 0)* — return events with `seq` greater than this (the response's
  `cursor`, from your last poll).
- `limit` *(optional, default 100, max 500)*.

```json
{
  "cursor": 187,
  "events": [
    { "seq": 186, "ts": "2026-07-01T22:52:25Z", "kind": "track_mute",
      "track_index": 0, "track_guid": "{EC1C5501-...}",
      "value": { "muted": false }, "source": "external" }
  ]
}
```

`kind` is one of `track_list_change`, `track_volume`, `track_pan`, `track_mute`,
`track_selected`, `track_solo`, `track_recarm`, `track_title`, `play_state`,
`repeat_state`. `track_index`/`track_guid` are present for track-scoped events (absent for
`track_list_change`/`play_state`/`repeat_state`). `source` is `"reaclaw"` or `"external"` —
**best-effort, not a guarantee**: REAPER doesn't promise every notification fires
synchronously within the triggering call, so a small fraction of ReaClaw's own edits can be
misattributed `"external"` (confirmed live — see `ReaClaw_TECH_DECISIONS.md` §26 for what
was actually observed). For an edit where attribution certainty matters, corroborate with
`GET /snapshot/diff` rather than trusting a single event in isolation.

The event ring is in-memory, bounded (last 1000), and resets on REAPER restart — an event
feed for the current session, not a durable log. FX/marker/other changes not in the `kind`
list above aren't covered yet (a deliberate v1 boundary, §26) — they still show up via
`GET /state/changes` and `GET /snapshot/diff`.

### GET /events/stream?since=

The same feed as Server-Sent Events (`text/event-stream`) — one `data: {...}\n\n` frame per
event, for a caller that wants push instead of polling:

```bash
curl -sk -N "$BASE/events/stream" -H "$AUTH"
```

Each connection is capped at 10 minutes; reconnect with `since` set to the last `seq` you saw
to pick up where you left off. No special client library needed — any SSE-aware HTTP client,
or a plain line-buffered read, works.

### GET /snapshot/diff?from=&to=

The fallback for clients that don't want an event feed at all — see the Snapshot section
above. Works regardless of session boundaries (a stored snapshot survives a REAPER restart;
the event ring and change-count token don't).

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
| `async` | bool | `false` | Issue #35: return a job handle immediately instead of blocking — see below |

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

### Async render jobs (issue #35)

Pass `"async": true` in the `POST /render` body to get a job handle back
immediately instead of blocking the HTTP connection for the render's
duration:

```json
{ "output": "/tmp/mix.wav", "async": true }
```

```json
{ "job_id": "job_1", "status": "queued" }
```

**`GET /render/jobs/{id}`** — poll a job's status:

```json
{
  "job_id": "job_1",
  "status": "running",
  "output_path": "/tmp/mix.wav",
  "format": "wav",
  "srate": 44100,
  "channels": 2,
  "created_at": "2026-07-01T14:40:29Z",
  "started_at": "2026-07-01T14:40:29Z",
  "elapsed_seconds": 5.27
}
```

`status` is one of `queued` → `running` → `done` | `error` | `cancelled`. A
`done` job's response also carries `render_seconds`, `project_length`, and
`offline_ratio` (same fields as the synchronous response); an `error` job
carries `error`. There is no numeric progress percentage — the REAPER SDK
exposes no render-progress primitive, so `elapsed_seconds` (measured) is
reported instead of a fabricated percent-complete.

**`GET /render/jobs`** — list all tracked jobs: `{ "jobs": [ ... ] }` (same
shape as a single job). Bounded to the 200 most recent; oldest *terminal*
jobs are evicted first, queued/running jobs are never evicted.

**`DELETE /render/jobs/{id}`** — cancel a job. Only works while the job is
still `queued` (hasn't started rendering yet) — returns the now-`cancelled`
job. Returns `409` if the job is already `running` (REAPER's SDK has no safe
way to abort an in-flight offline render — wait for it to finish) or already
in a terminal state. Returns `404` for an unknown job id.

**Important limitation — async does not make other endpoints stay responsive
during the render.** Confirmed live: REAPER's main thread pumps no message
loop at all while an offline render is in progress (`Main_OnCommand(41824)`
is a tight, uninterruptible loop) — every other endpoint that depends on
`Executor::post` (most of the API) will queue up and can time out during that
window, exactly as it would with a synchronous render. `async: true` solves
"don't hold the HTTP connection open and don't have to guess a timeout" — it
does not solve "REAPER stays responsive to other calls while rendering." A
second async render request queues cleanly behind the first (no corrupted
render settings — Executor's FIFO drain serializes them), so this is safe,
just not concurrent.

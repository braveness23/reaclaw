# ReaClaw Agent Guide

How to operate REAPER through ReaClaw **fast** — written for AI agents, any
vendor. A running server serves this exact document at `GET /agent/guide`, so
you can re-fetch it after an upgrade and it always matches the running version.
Use it to configure yourself: persist what §7 tells you to persist in whatever
skill/memory mechanism your harness provides, and a "mute track 3" request
should cost you one HTTP call, not an exploration session.

The full endpoint reference is `docs/API.md` (in the repo) and
`GET /capabilities` (machine-readable, on the server); `skill/reaclaw/SKILL.md`
(in the repo) teaches session-building patterns and the tiered coverage model,
and `GET /recipes` serves its vetted recipes. This guide is the distilled
*operating* manual: how to connect, how to be fast, how to stay in sync with a
human editing the project at the same time, and the traps.

---

## 1. Connect

- Config: `<REAPER resource dir>/reaclaw/config.json` (Linux:
  `~/.config/REAPER/reaclaw/config.json`). Read `server.host`, `server.port`,
  and `auth.key` from it at call time — don't hardcode. When `host` is
  `0.0.0.0`, call `127.0.0.1`.
- TLS is on by default with a **self-signed** cert → skip verification
  (`curl -sk`).
- Auth: `Authorization: Bearer <key>` on every request. The only
  unauthenticated route is `GET /` (orientation page).
- Env-var convention (used by the bundled demo/CI scripts): `REACLAW_BASE` and
  `REACLAW_KEY` override discovery.
- Optional `X-Agent-Id: <you>` header tags your edits in `GET /history` and
  the learning miner.
- Server log: path in `config.json` → `logging.file`.
- Errors are always `{ "error", "code", "context" }`; the commonly-misguessed
  endpoints return a `context.schema` listing every accepted field — read it
  instead of guessing twice.

## 2. The latency contract

The point of ReaClaw is that a request→done round trip is ~100 ms. Protect
that:

- **Known verb ⇒ fire the mutation first.** No exploratory GETs. The mutating
  response returns the full updated object plus `hints[]` — that *is* your
  verification.
- **Batch independent calls** in one shell invocation / tool call.
- **Never screenshot to verify.** Screenshots are for GUI-only state (custom
  plugin GUIs) or when the user asks to see something.
- `hints[]` on mutating responses warn about consequences (`muted_track`,
  `routes_nowhere`, `near_silent_fader`, …). Read them — they replace a
  follow-up state check.
- Know the traps (§6). One wedged modal costs more than every GET you ever
  skipped.

## 3. Staying in sync with a human at the GUI

Assume a human (or another client) edits the project concurrently. Never
assume your cached picture is current — but never pay for freshness with
bulk re-reads either. Cheapest first:

1. `GET /state/changes` → `{ "change_count": N }` — one monotonic counter,
   increments on essentially any edit from any source. Cache it. Unchanged ⇒
   act on what you know. (~75 ms)
2. `GET /events?since=<cursor>` — *what* changed: `track_volume`, `track_pan`,
   `track_mute`, `track_solo`, `track_selected`, `track_recarm`,
   `track_title`, `track_list_change`, `play_state`, `repeat_state`, each with
   `source: "reaclaw" | "external"`. Keep the returned `cursor`.
3. `GET /events/stream?since=` — the same feed as SSE push. Connections cap at
   10 minutes; reconnect with the last `seq` you saw. **Recommended pattern**:
   keep a background process appending this stream to a local file, then
   "what did the user just change?" is a zero-latency file read on your side.
4. Full `/state` / `/state/tracks` re-read **only** when events show
   `track_list_change`, the counter/cursor went *backwards* (REAPER
   restarted — the event ring and counter are in-memory and session-scoped),
   or churn is too heavy to patch incrementally.

Caveats: `source` attribution is best-effort, not a guarantee (a small
fraction of your own edits can read `"external"`); FX/marker/item edits are
not in the event feed (deliberate v1 boundary) but still bump `change_count`;
`/state*` reads are cached for 1 s (your writes bust the cache, an external
UI edit can be ≤1 s stale). `GET /snapshot/diff` is the session-proof
fallback — a stored snapshot survives a REAPER restart.

## 4. Cheat sheet — the common 90%

Bodies shown are the load-bearing fields; the reference has the rest.
Indices are 0-based everywhere (track 3 in the UI = index 2).

### Mixer / tracks
```
POST /state/tracks/2            {"muted":true}                    # also: soloed, armed
POST /state/tracks/2            {"volume_db":-6.0,"pan":-0.25}
POST /state/tracks/2            {"name":"Bass","color":"#33AA55","icon":"bass.png"}
POST /state/tracks              {"create":[{"name":"Drums","folder_depth":1,"instrument":"ReaSynth"}]}
DELETE /state/tracks/2
POST /state/tracks/2/sends      {"to_track":5,"volume_db":-12.0}
POST /state/tracks/2/sends/0    {"volume_db":-9.0,"mode":2}       # 0 post-fader, 1 pre-fx, 2 pre-fader
POST /state/selection           {"tracks":[0,2],"items":"none"}
```

### Transport / tempo
```
GET  /transport                                    # live position, bypasses the 1s cache
POST /transport/play|stop|pause|record
POST /transport/cursor          {"position":4.0}
POST /transport/loop            {"start":0.0,"end":8.0,"enabled":true}
POST /state/tempo               {"bpm":95}                        # project start tempo
POST /state/tempo               {"time":32.0,"time_signature":"3/4","bpm":140}
```

### Items / editing
```
GET  /state/items
POST /state/items               {"create":[{"track":0,"position":1.0,"file":"/path/loop.wav"}]}
POST /state/items/0             {"position":0.5,"muted":true,"take":{"pitch":2,"playrate":1.0}}
POST /state/items/0/split       {"position":2.0}                  # absolute timeline seconds
DELETE /state/items/0
```
`POST /state/items` with `file` places audio/MIDI directly — no Lua needed.
Create/split/delete shifts project-wide item indices: re-read after one.

### MIDI (active take of item)
```
GET  /state/items/0/midi
POST /state/items/0/midi        {"notes":[{"pitch":60,"velocity":100,"start_ppq":0,"end_ppq":480}],
                                 "replace":false}
```
Positions: `start_ppq`/`end_ppq` (take-relative, 480 = quarter note) or
`start_time`/`end_time` (project seconds).

### FX
```
POST /state/tracks/2/fx         {"name":"ReaComp","params":[{"name":"Threshold","value":0.25}]}
GET  /state/tracks/2/fx/1?q=freq                                  # param search on big plugins
POST /state/tracks/2/fx/1       {"params":[{"index":0,"value":0.5}],"enabled":true}
GET/POST /state/tracks/2/fx/1/preset            {"name":"..."} or {"navigate":1}
POST /state/tracks/2/fx/1/copy  {"to_track":3,"to_slot":-1,"move":false}
```
Param `value` is **normalized 0..1**; the read gives `formatted` + real-unit
`raw`/`min`/`max` so you can reason in real units. `{slot}` accepts the chain
index **or** the FX `guid` (stable across reorders — prefer it for long-lived
references). Take FX mirror all of this at
`/state/items/{i}/takes/{t}/fx/...`.

### Arrange / project
```
GET/POST /state/markers         {"position":8.0,"name":"Chorus","is_region":false}
POST /undo   ·  POST /redo   ·  GET /undo        # what would be undone
POST /project/save              {"path":"/abs/path/song.rpp"}     # omit path = save in place
POST /project/open              {"path":"/abs/path/song.rpp","discard_changes":true}
POST /project/new               {"discard_changes":true}          # NOT action 40023 (modal wedge)
POST /project/reset             {"discard_changes":true}          # blank in place, headless-safe
```

### Actions (the ~6000-verb escape hatch)
```
GET  /catalog/search?q=trim+silence
POST /execute/action            {"id":40044}
POST /execute/sequence          {"steps":[{"id":40042},{"id":40044}]}
```
IDs worth memorizing: `40044` play/stop toggle · `1013` record · `40042` go
to project start · `40029` undo / `40030` redo · `40157` insert marker at
cursor. Catalog rows carry `interactive: true` when an action opens a modal —
**never fire those headless**.

### Scripts (full ReaScript/Lua escape hatch)
```
POST /execute/script            {"script":"reaper.SetTempoTimeSigMarker(0,-1,0,-1,-1,95,4,4,false)"}
POST /scripts/register          {"name":"my_tool","script":"..."}  # field is `script`, not `code`
```
No `ShowConsoleMsg` (rejected — blocks headless). Return data with
`reaper.SetProjExtState(0, sec, key, val)` and read it back via
`GET /project/extstate?section=sec` — the *global* `SetExtState` is a
different store and won't show up there.

### Perception
```
GET /state                       # project + transport + track_count digest
GET /state/tracks                # full track table incl. sends + fx
GET /state/meters                # live peaks (only meaningful during playback)
GET /analysis/item/0             # LUFS/peak/clipping/spectral of the source
GET /analysis/item/0/visualize?type=spectrum      # PNG + numeric digest
GET /analysis/item/0/probe?probes=pitch,key
GET /screenshot?target=arrange&width=800          # GUI-only state, last resort
GET /state/chunk?target=track&index=0             # raw RPP backstop, anything reachable
```

### Render (offline, the headless product)
```
POST /render                    {"output":"/tmp/mix.wav","format":"wav","async":true}
GET  /render/jobs/job_1
```

### Sync / audit
```
GET /state/changes  ·  GET /events?since=42  ·  GET /events/stream?since=42
POST /snapshot {"label":"before mix"}  ·  GET /snapshot/diff?from=3&to=current
GET /history?limit=20            # who executed what, when
```

## 5. Structural gotchas (correct, but will surprise you)

- **Slot 0 is often a ghost.** REAPER auto-adds a disabled inline ReaEQ as raw
  slot 0 on new tracks. FX lists carry `is_inline_eq` and `agent_slot`
  (numbering that skips it) — address by `agent_slot`-derived raw slot or by
  `guid`, or your "first plugin" edits hit the wrong FX.
- Undo: every structured mutation is one undoable step, labeled.
- `POST /execute/action` supports `timeout_ms` (default 15 s) and
  `async: true`; a 408 means the main thread didn't respond — check for a
  modal before retrying.

## 6. Traps (these wedge REAPER or lie to you)

- **`POST /render` to an existing path, or `format:"mp3"`** on some setups →
  a modal dialog wedges REAPER's main thread; every queued call times out.
  Always render to a fresh `.wav`/`.flac` path. Recovery: screenshot to
  confirm the dialog, dismiss it with a window tool (`xdotool`), then
  `POST /queue/flush`.
- **During any offline render REAPER's main thread is fully blocked** — even
  with `async: true` other calls queue and may 408. Async frees your HTTP
  connection, not REAPER.
- **Action `40023` (File: New project)** opens a save prompt — use
  `POST /project/new` (modal-free) instead. Same class of problem for any
  catalog row with `interactive: true`.
- **Bad Lua can wedge the main thread permanently** when `luac` isn't
  installed (syntax check silently no-ops; the broken script really runs).
  An infinite loop is unrecoverable without a REAPER restart.
- **ReaEQ via Lua `SetEQParam`**: band-type enum (`hishelf` = 4), gain is
  **linear** (1.0 = 0 dB), and gain *readback* is mis-scaled (0.5 reads as
  0 dB) — write, don't trust the read.
- **Cockos plugins expose two "Wet" params** (one from the plugin, one from
  REAPER's container). Match params by probing the param list (`?q=wet`),
  never by guessed index.
- **ReaDelay musical length** is the note fraction directly: `0.0625` = 1/16.
- **ReaVerb's impulse-response file is not settable** via params or chunk. Do
  convolution offline (e.g. ffmpeg `afir`) instead.
- **RS5K as a sampler/drum machine works well over the API** (load file, set
  note range per instance) — just mind the ghost slot 0 above.
- **Audio-quantize** has no single verb: transient-detection actions +
  `SetTakeStretchMarker` (Lua) + action `41846` (snap stretch markers to
  grid). Action `41843` only adds edge markers — it does not detect
  transients.
- **An open MIDI editor breaks arrange-view zoom/scroll actions.** Close it
  before view manipulation.

## 7. Configure yourself

Persist, in whatever memory/skill mechanism your harness provides:

1. The connection triple (config path → host/port/key) and the `rc`-style
   one-liner you'll call it with.
2. The §4 cheat sheet and §5/§6 lists.
3. This machine's specifics as you learn them: where projects and media live,
   which plugins are installed, display/audio quirks.
4. Your user's recurring patterns — the phrases they use and the edits those
   map to ("slap on the snare" = ReaDelay at 0.0625, etc.). The goal is that
   no request is ever surprising twice.
5. Re-fetch `GET /agent/guide` after a ReaClaw upgrade; check
   `GET /capabilities` → `features` before relying on optional tooling
   (sws, ffmpeg, xdotool, key/tempo detector).

If the server has `learning.enabled` on, `GET /suggestions` returns mined
"after X, agents usually do Y" patterns from this machine's own history —
advisory, local-only.

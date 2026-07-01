# Lua Scripting Cheat Sheet

Issue #73. Everything here was discovered the hard way (mostly during a single
friction-test session, 2026-06-30) building sessions via `/scripts/register` +
`/execute/action` and, since v1.11.2, the one-shot `/execute/script`. REAPER's
Lua API return-value ordering is inconsistent and undocumented in a
machine-friendly way — this page exists so an agent doesn't have to
rediscover it by trial and error.

If you only need to run something once and throw it away, use
`POST /execute/script` (see `docs/API.md`) instead of the two-step
register+execute flow below — it wraps the same rules.

---

## Rule 1 — headless safety: no `ShowConsoleMsg`, no blocking GUI calls

`ShowConsoleMsg` and any call that opens a modal dialog will **block REAPER's
main thread forever** in a headless session — there's no user to dismiss the
dialog, and every other API call queues up behind it (`/health` reports
`"status": "degraded"` once this happens). `/scripts/register` rejects
`ShowConsoleMsg` outright with a clear error. There is no static analysis
beyond that string check — anything else that opens a dialog (some
`Main_OnCommand` IDs, notably `40023` "File: New project" when a project is
already dirty — see issue #80 and `POST /project/new`) will still wedge it.

**Recovery if you do get stuck:** `POST /queue/flush` drains everything
queued behind the stuck call so new requests stop timing out; it does not
unstick the call that's actually wedged — only a REAPER restart does that.

## Rule 2 — the only safe way to return data is project ext-state

Scripts can't return values over HTTP directly; the runner only tells you
whether the script executed and whether it threw. To pass data back, write it
to the **project's** ext-state (`SetProjExtState`) and read it back with
`GET /project/extstate` — **not** `reaper.SetExtState()`/`GetExtState()`, the
*global* (non-project) ext-state store, which has no REST endpoint at all and
will silently read back as `null` if you mix the two up (confirmed live —
this is the one gotcha in this file that isn't hypothetical):

```lua
-- In the script:
reaper.SetProjExtState(0, "myagent", "result", tostring(some_value))
```

```
GET /project/extstate?section=myagent&key=result
```

## Rule 3 — runtime errors surface via `lua_error`, not an HTTP error

Every registered script is wrapped in a `pcall`. If the body throws, the
response has `"status": "lua_error"` and a `"lua_error"` field with the
message — the HTTP call itself still returns `200`. Check `status`, not just
the HTTP status code.

## Rule 4 — tricky call signatures (first return is usually a bool)

The REAPER Lua API frequently returns `(bool ok, ...)`, and it's easy to grab
the wrong slot:

```lua
-- WRONG — pname actually receives the bool
local pname, _, _ = reaper.TrackFX_GetParamName(track, fx, i, "")

-- CORRECT
local ok, pname = reaper.TrackFX_GetParamName(track, fx, i, "")
```

| Call | Returns |
|---|---|
| `TrackFX_GetParamName(track, fx_idx, param_idx, "")` | `bool ok, string name` (buffer arg is a placeholder — REAPER's Lua binding returns the string, doesn't write into it) |
| `TrackFX_GetFXName(track, fx_idx, "")` | `bool ok, string name` |
| `GetSetMediaTrackInfo_String(track, "P_NAME", "", false)` | `bool ok, string value` |
| `MIDI_GetNote(take, idx)` | `bool ok, bool selected, bool muted, number startppq, number endppq, number chan, number pitch, number vel` |
| `MIDI_GetCC(take, idx)` | `bool ok, bool selected, bool muted, number ppqpos, number chanmsg, number chan, number msg2, number msg3` |

Prefer the REST endpoints (`GET/POST /state/tracks/{n}/fx/{slot}`,
`GET/POST /state/items/{n}/midi`) over hand-rolled Lua for these — they
already handle the ordering for you and are the faster path for anything they
cover.

## Rule 5 — tempo: `SetTempoTimeSigMarker` idiom

```lua
-- Set/replace the marker AT time=0 (the common "set project tempo" case):
reaper.SetTempoTimeSigMarker(0, -1, 0, -1, -1, 95, 4, 4, false)
--                                 ^ptidx=-1 means "find-or-insert at this
--                                   time position", NOT "append a new one
--                                   regardless" — counter-intuitive but
--                                   safe to call repeatedly at the same time.

-- ptidx=0 instead modifies whatever marker is currently at index 0 by
-- position, regardless of the `time` argument — easy to get backwards.
```

Prefer `POST /state/tempo {"bpm": 95}` (issue #70) — `time` defaults to
`0.0`, so this is the one-call equivalent of the idiom above without needing
to remember the `ptidx=-1` trick.

## Rule 6 — MIDI items: don't build tracks/items via raw `Main_OnCommand`

`{"midi": true}` in a `POST /state/items` create spec makes an empty MIDI
item correctly; there's no need for `PCM_Source_CreateFromType` or similar
Lua-only construction. Insert notes via `POST /state/items/{i}/midi`, not a
per-note Lua loop — a Lua loop inserting more than ~25 notes has been
observed to reliably time out on aarch64 (Raspberry Pi); the default
main-thread timeout is 15s (`timeout_ms` on `/execute/action` can raise it,
but the REST MIDI endpoint doesn't need to queue individual calls at all).

## Worked example — build a MIDI item and return a summary

```lua
local track = reaper.GetTrack(0, 0)
local item = reaper.CreateNewMIDIItemInProj(track, 0.0, 2.0, false)
local take = reaper.GetActiveTake(item)
reaper.MIDI_InsertNote(take, false, false, 0, 480, 0, 60, 100, true)
reaper.MIDI_InsertNote(take, false, false, 480, 960, 0, 64, 100, true)
reaper.MIDI_Sort(take)
reaper.SetProjExtState(0, "myagent", "notes_built", "2")
```

Then: `GET /project/extstate?section=myagent&key=notes_built` → `{"value": "2"}`
(verified live). In practice, prefer `POST /state/items` + `POST
/state/items/{i}/midi` for this exact case — the worked example exists to
show the ext-state read-back pattern for scripts that do need custom Lua logic.

# ReaClaw Friction Test Report
**Date:** 2026-06-30  
**Session:** Agent-driven, API-only (headless Pi, aarch64)  
**Goal:** Build a 3-track C-minor arrangement (Bass/Pad/Lead, Surge XT + Dragonfly Reverb, 95 BPM) from scratch using only the ReaClaw REST API and observe every point of friction.

**Result:** Succeeded. 2 renders produced. Peak level -12.9 dBFS, real signal. Project saved to `studio/projects/active/`. Required 3 REAPER restarts due to stuck main thread.

---

## What Worked Well

- **`/health`** — reliable canary; `queue_depth` and `db_ok` are useful
- **`/state`, `/state/tracks`** — comprehensive; the `hints` array (muted-track warnings, etc.) is a genuinely good idea
- **`/transport {"action": "play|stop|pause"}`** — clean verb-body pattern once discovered
- **`/scripts/register` + `/execute/action`** — the Lua escape hatch is powerful and worked for everything: BPM, track building, MIDI creation, FX param read/write, MIDI readback
- **`/render`** — 16× offline ratio on Pi, 24-bit PCM, correct duration. Fast and reliable.
- **`/project/save`** and **`/project/new`** — work as expected
- **`/catalog/search`** — fast, `interactive: true` flag is helpful for filtering out GUI-only actions
- **`POST /state/tracks/{n}`** — setting pan, volume, mute in one call; returns full track state immediately

---

## Friction Findings (ranked by severity)

### CRITICAL — Causes REAPER Restart

**F-1: ShowConsoleMsg wedges headless REAPER permanently**  
`reaper.ShowConsoleMsg(...)` blocks the main thread waiting for a GUI dialog that will never appear on a headless / display-less session. Queue depth gets stuck, all subsequent commands time out, REAPER restart required.  
**Fix:** Document this prominently in scripting docs. ReaClaw should detect `ShowConsoleMsg` in registered scripts and either strip it or return a clear error. The correct data-return idiom is `reaper.SetExtState("namespace", "key", value, false)` — this should be in every example.

**F-2: Stuck queue has no recovery path**  
After a timeout, `queue_depth` stays elevated (1–3) and every subsequent command also times out. There is no flush/drain endpoint. The only recovery is restarting REAPER.  
**Fix:** Add `POST /queue/flush` or auto-detect stuck queue state and mark health degraded. At minimum, `"status": "degraded"` in `/health` when `queue_depth > 0` for more than 10 seconds.

**F-3: 5-second command timeout is too short for Pi on complex scripts**  
A Lua script inserting ~30 MIDI notes reliably times out on aarch64 ARM. The HTTP client gets a 408 Timeout, but REAPER may still be processing — leaving a phantom queue item.  
**Fix:** Configurable timeout per-call (e.g., `{"id": "...", "timeout_ms": 15000}`). Alternatively, async execution: fire script and return a job ID, poll `GET /jobs/{id}` for result. The async path already exists in the codebase for other actions — plumb it for scripts too.

---

### HIGH — Agent Gets Wrong Answers

**F-4: ReaEQ ghost plugin pollutes FX slot numbering**  
REAPER auto-adds a disabled ReaEQ (track inline EQ) to every new track as slot 0. An agent that calls `TrackFX_AddByName(track, "Surge XT", ...)` gets Surge XT at slot 1, not 0. The `/state/tracks` response faithfully reports this but an agent building a mental model of "I added Surge to slot 0" is wrong.  
**Fix:** Two options: (a) filter `VST: ReaEQ (Cockos)` (disabled, inline EQ) from FX lists in `/state/tracks` since it's not agent-controllable; or (b) surface an `is_inline_eq: true` flag so the agent knows to ignore it. Don't silently renumber.

**F-5: `cursor_position` in `/state` is stale during playback**  
The 1-second TTL cache means polling `/state` to track playhead position during playback returns values up to 1s old. An agent trying to "wait until bar 4" or trigger something at a time offset will be systematically wrong.  
**Fix:** Expose a lightweight `GET /transport` endpoint that bypasses the state cache and returns live position + playing status. The cache is right for tracks/items (slow-changing) but wrong for transport.

**F-6: Script registrations are volatile**  
Registered scripts don't survive REAPER restarts. An agent that registered scripts in a previous session must re-register them on every start. There's no `GET /scripts` to enumerate what's currently loaded.  
**Fix:** Persist registered scripts across restarts (the `.lua` files already exist on disk — just re-register them on boot). Add `GET /scripts` so an agent can check what's already registered.

---

### MEDIUM — Wasted Round Trips / Confusing API

**F-7: No one-shot anonymous script execution**  
The two-step register → execute pattern requires two HTTP round trips for every Lua operation. For short throw-away scripts (BPM probe, note insert, FX param read) this is heavy.  
**Fix:** `POST /execute/script {"script": "...lua..."}` — registers, executes, and optionally auto-deregisters. Action ID is returned but can be ignored if `ephemeral: true`.

**F-8: No REST path for BPM**  
Setting tempo requires knowing the Lua idiom `SetTempoTimeSigMarker(proj, -1, 0, -1, -1, bpm, 4, 4, false)`. The `ptidx=-1` (create new rather than modify) behavior is counter-intuitive. `GetSetProjectInfo(proj, "TEMPO", ...)` does not work for master tempo.  
**Fix:** `POST /state {"bpm": 95}` or add BPM to the project section of `PATCH /state`. Already reading BPM in `/state` — writing it should be symmetric.

**F-9: `/transport/play` returns 404; correct is `POST /transport {"action":"play"}`**  
The verb-body pattern for transport is good, but an agent that guesses `/transport/play` gets a 404 with no hint. Every agent I've seen will try REST-style sub-resource naming first.  
**Fix:** Either add alias routes (`POST /transport/play` → forwards to `{"action":"play"}`), or return 405 Method Not Allowed with a `hint` field pointing to the correct pattern.

**F-10: `/render` field is `output` not `output_file`**  
The error says "Missing required field: output" — but doesn't list what fields are accepted. An agent guessing `output_file` (common naming) gets a cryptic rejection.  
**Fix:** On 400 for `/render`, return the expected schema: `{"required": ["output", "start", "end"], "optional": ["sample_rate", "channels", "bit_depth"]}`.

**F-11: REAPER Lua API return-value ordering is inconsistent**  
Some functions return `(retval, name, ...)`, others `(name, ...)`. `TrackFX_GetParamName` returns `(bool, name, min, max)` — easy to mistake for `(name, ...)`. Without IDE completion or good docs, agents hit this.  
**Fix:** Include a cheat-sheet of common Lua API call signatures in `docs/SCRIPTING.md`. Especially the tricky ones: `GetFXName`, `GetParamName`, `GetSetMediaTrackInfo_String`.

---

### LOW — Missing Capabilities Worth Filing

**F-12: No FX param REST endpoints**  
Reading or writing plugin parameters requires Lua. A natural REST path would be `GET /state/tracks/{n}/fx/{slot}/params` and `POST /state/tracks/{n}/fx/{slot}/params/{idx}`. Useful for agents that want to tune reverb mix, EQ bands, synth cutoff — without registering a script.

**F-13: No MIDI readback endpoint**  
Can't enumerate notes in an existing MIDI item via REST. Requires Lua (`MIDI_CountEvts`, `MIDI_GetNote`). A `GET /state/items/{n}/midi` endpoint returning note arrays would enable a whole class of "read-analyze-modify" agent workflows.

**F-14: No track color API**  
`/state/tracks` returns `"color": null`; can't set it via `POST /state/tracks/{n}`. Track colors are useful for agent orientation ("the red track is the bass"). Easy Lua one-liner: `SetTrackColor(track, color)`.

**F-15: X-display context lost on REAPER restart from headless shell**  
When ReaClaw (or any external process) needs to restart REAPER, the new process can't connect to the original X session without the right auth cookie. Recovery requires user intervention.  
**Fix:** Expose `POST /reaper/restart` — ReaClaw can relaunch REAPER in-process (it already knows the display/env context from its own launch), which sidesteps the xauth problem entirely.

---

## Summary Table

| # | Severity | Category | One-liner |
|---|----------|----------|-----------|
| F-1 | CRITICAL | Stability | ShowConsoleMsg wedges headless main thread permanently |
| F-2 | CRITICAL | Stability | No stuck-queue recovery; must restart REAPER |
| F-3 | CRITICAL | Stability | 5s timeout too short for Pi; no async script path |
| F-4 | HIGH | Data model | ReaEQ ghost in FX slot 0 surprises agents |
| F-5 | HIGH | Data model | Transport position stale (1s cache); no live polling |
| F-6 | HIGH | Session | Scripts lost on restart; no script listing |
| F-7 | MEDIUM | DX | Two round trips for every Lua op; needs one-shot execute |
| F-8 | MEDIUM | DX | No REST BPM setter; Lua idiom is non-obvious |
| F-9 | MEDIUM | DX | /transport/play 404 with no hint |
| F-10 | MEDIUM | DX | /render error message doesn't list expected fields |
| F-11 | MEDIUM | DX | Lua API return ordering inconsistent; needs cheat-sheet |
| F-12 | LOW | Missing | No FX param REST endpoints |
| F-13 | LOW | Missing | No MIDI readback REST endpoint |
| F-14 | LOW | Missing | No track color API |
| F-15 | LOW | Environment | No /reaper/restart endpoint; xauth lost on crash |

---

## What Actually Played

```
Project:    C minor, 95 BPM, 4 bars (~10.1s), looped
Tracks:     Bass (Surge XT, center), Pad (Surge XT + Dragonfly Hall, -20% L), Lead (Surge XT, +25% R)
Harmony:    Cm7 → Gm7 → Cm7 → Abmaj7
Melody:     C minor pentatonic, climbing to C5 resolution at bar 4
Render:     /home/dave/studio/renders/drafts/friction_test_v2.wav
            pcm_s24le, 44100 Hz, stereo, peak -12.9 dBFS
```

Royalties are yours. I'll take credit in the liner notes.

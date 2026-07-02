---
name: reaclaw
description: Drive REAPER through the ReaClaw HTTP API like an expert — build and edit sessions with high-level structured commands (tracks, FX, routing, selection), run any of REAPER's ~6700 actions, and register Lua scripts for the long tail. Use whenever a task involves controlling REAPER, building a session, adding tracks/effects/sends, or automating a DAW workflow via ReaClaw.
---

## What ReaClaw is

A native REAPER extension that exposes an HTTPS REST API for controlling REAPER.
You (the agent) reason and decide; ReaClaw executes. Default base URL
`https://127.0.0.1:9091`. All requests need `Authorization: Bearer <api_key>`
(from `config.json`). TLS is a self-signed cert — pass `-k`/verify off.

**Golden rule of coverage (tiered — see `/capabilities`):**
1. **Structured verbs first** — for tracks, FX, routing, selection. One typed call.
2. **Action catalog** — for anything with a REAPER action (`/catalog/search` then
   `/execute/action`). **Search before you generate.**
3. **Lua script** — `/scripts/register` only for what neither covers (items, MIDI,
   markers/regions, tempo map, envelopes, render).

Call `GET /capabilities` once at the start of a session — it tells you exactly
which tier covers what, so you never guess.

## Build a session with structured verbs (the fast path)

Create a whole folder group with names, colors, and a mix in **one** call:

```http
POST /state/tracks
{ "create": [
  { "name": "DRUMS", "color": "#CC3333", "folder_depth": 1 },
  { "name": "Kick",  "color": "#33AA55", "volume_db": -3.0, "armed": true },
  { "name": "Snare", "color": "#3366CC", "folder_depth": -1, "pan": -0.2 }
]}
```

`folder_depth`: `1` = folder parent (children follow), `0` = normal, negative =
closes that many folder levels (last child). Build a 1-level folder by setting the
parent to `1` and its **last** child to `-1`.

| Goal | Call |
|------|------|
| Create / batch-create tracks | `POST /state/tracks {create:[...]}` |
| Batch-update tracks | `POST /state/tracks {update:[{index, ...}]}` |
| Edit one track (name/color/folder/vol/pan/mute/solo/arm) | `POST /state/tracks/{i}` |
| Delete a track | `DELETE /state/tracks/{i}` |
| Add a named effect | `POST /state/tracks/{i}/fx {name:"ReaComp"}` |
| Read FX params | `GET /state/tracks/{i}/fx/{slot}` |
| Set FX enable / params | `POST /state/tracks/{i}/fx/{slot} {enabled, params:[{name|index, value}]}` |
| Delete an FX | `DELETE /state/tracks/{i}/fx/{slot}` |
| Add a send | `POST /state/tracks/{i}/sends {to_track, volume_db, pan}` |
| Delete a send | `DELETE /state/tracks/{i}/sends/{send}` |
| Set track selection | `POST /state/selection {tracks:[..]|"all"|"none"}` |

FX param `value` is **normalized 0..1**, addressed by param `name` (exact, e.g.
`"Threshold"`) or `index`. Read the slot first to learn names/current values.

## Verify with structure first — screenshots only when asked

**Default to reading state, not looking.** `GET /state/tracks` returns each
track's `name`, `folder_depth`, `color`, `volume_db`, `pan`, mute/solo/arm,
`fx[]`, and `sends[]` (`dest_track`, `volume_db`, `pan`); `GET /state` gives
tempo/transport/project. This is enough to build and confirm almost anything —
you rarely need to see the screen. For state the JSON can't express (e.g. a
ReaEQ band's enabled flag), have a registered Lua script write a report file and
read that.

**When you DO need to see it** (the user asks "show me", or you're verifying
something inherently visual like a theme/color/layout), capture the live REAPER
window and read the image yourself — no human needed:

```bash
# X11 (DISPLAY set, e.g. :0.0). Reads the real on-screen framebuffer.
ffmpeg -y -f x11grab -i "$DISPLAY" -frames:v 1 /tmp/reaper.png
# crop to a region: crop=W:H:X:Y  (mixer = full-width bottom strip; arrange = top)
ffmpeg -y -i /tmp/reaper.png -vf "crop=1920:230:0:850" /tmp/mixer.png
```

Then `Read` the PNG. Use `ffmpeg x11grab`, **not `xwd`** — `xwd` returns blank
client areas for REAPER's GDK/SWELL windows. REAPER's window may be occluded by
other apps; raise/maximize it first or crop to the part you need. (Wayland:
`grim /tmp/reaper.png`.) A reusable, host-generic version of this lives as the
`screenshot` skill if installed.

## Actions (the middle tier)

For things without a verb, **search then run**:

```http
GET  /catalog/search?q=mute%20drums      → {actions:[{id,name,category}]}
POST /execute/action  { "id": 40702 }    → {action_name, status}
POST /execute/sequence { "steps":[{id},{id}] }
```

Responses now include `action_name`, so you get feedback on what fired. A handful
of high-value action IDs:

- `40702` insert track at end (selects it) · `40005` remove selected tracks
- `40296` select all tracks · `40913` scroll selected into view
- `41757` insert ReaEQ on selected track
- `53608` (SWS) make folder from selected tracks · `40358` random track colors
- `54964`/`54965` close floating FX / FX-chain windows

But prefer the structured verbs above — they're deterministic and don't depend on
selection state.

## DON'T (these open modal dialogs and will hang a headless agent)

- `40696` "Rename last touched track" and SWS console rename → use
  `POST /state/tracks/{i} {name}` instead.
- Custom-color **picker** actions → use `{color:"#RRGGBB"}`.
- Any action whose name contains "…" / "prompt" / "dialog" — assume it's modal.
  When unsure, prefer a structured verb or a Lua script.

## Lua escape hatch (the long tail)

For media items, MIDI notes, markers/regions, tempo map, envelopes, or render:

```http
POST /scripts/register { "name":"...", "script":"-- Lua using reaper.* API" }
→ returns a command id; run it via /execute/action {id:"_<that id>"}
```

Validation is **syntax-only** — your code is trusted. Register once, reuse by id.

## Decision guide

```
Need to change a track / FX / send / selection?  → structured verb
Is there a REAPER action for it?  → /catalog/search → /execute/action
Neither?                          → /scripts/register (Lua)
```

## Gotchas

- `GET /state*` reads are cached ~1 s; writes bust the cache, so a read right
  after your own write is fresh.
- Track indices are 0-based and shift when you insert/delete — re-read
  `/state/tracks` after structural changes.
- FX names: `"ReaComp"`, `"ReaGate"`, `"ReaEQ"` resolve; full `"VST: ..."` also
  works. If add returns 400 "FX not found", the plugin isn't installed.

## Instrument & FX plugin cheat sheet (Pi rig, linux-aarch64)

Beyond the stock ReaSynth/JSFX, these free Linux-native plugins are installed
and confirmed working via `POST /state/tracks/{i}/fx {"name": "..."}` — REAPER
matches on substring, so the short name resolves the full plugin string
(issue #62):

| API `name` | What it is |
|---|---|
| `"Surge XT"` | Full hybrid VA/wavetable synth, 1000s of presets — the default choice over ReaSynth for anything melodic |
| `"Nekobi"` | TB-303 acid bassline synth |
| `"Kars"` | Karplus-Strong plucked string / mallet |
| `"Cardinal Synth"` | VCV Rack modular synth (see below — **the default patch already makes sound**, no setup needed) |
| `"Dragonfly Hall"` / `"Dragonfly Room"` / `"Dragonfly Plate"` | Big/tight/classic reverbs — a real upgrade over ReaVerbate |
| `"MVerb"` | Studio algorithmic reverb |
| `"LSP Compressor"` / `"Parametric Equalizer"` / `"Limiter"` | Pro-grade LSP suite |
| `"sfizz"` (VST3i) / `"LV2i: sfizz"` | SFZ sample player — built from source (no aarch64 binary release exists; see `~/src/sfizz-ui`), confirmed loading real multi-sampled instruments via the CLI `sfizz_render` tool |

**Cardinal Synth is not silent by default** — it ships with a basic VCO → ADSR
→ VCA voice already patched, so `add fx` + MIDI notes + `/render` produces
real audio with zero manual setup (confirmed: peak −10 dBFS on a 4-note
render). For a richer sound, a human can open the FX floating window
(`reaper.TrackFX_Show(track, slot, 1)` via `/execute/script`) and use
**Cardinal's own `File > Open demo / example project` menu** — a dozen
bundled patches (DRMR, falkTX, SpotlightKid, etc.) ship inside the plugin
itself, no download needed. This is a GUI-only step (Cardinal's rack state
isn't reachable through REAPER's own preset API), so it's for interactive
sessions, not headless agent pipelines — the default patch is the
headless-safe option.

Cardinal requires its ~270MB resource bundle installed at
`/usr/local/share/cardinal/` (system-wide, alongside the `.clap` binaries in
`~/studio/plugins/linux-aarch64/`) — without it the plugin loads but shows a
black window and an "system directory does not exist" error.

**sfizz: loading an SFZ file into the REAPER plugin is not headless-safe on
this rig.** The plugin's own file browser (VST3) needs `zenity`, which hangs
at a 1×1 window on this Xvfb+xfwm4 test display — confirmed environment-wide
(even `zenity --info` alone hangs; no compositor owns `_NET_WM_CM_S0`), not an
sfizz bug. The LV2 variant's REAPER-native generic-param UI exposes an "SFZ
file" control, but it has no enumerated values so nothing is clickable either.
**sfizz itself is fully verified working** — built from source (`~/src/sfizz-ui`,
no aarch64 binary release exists upstream past 0.5.1), installed as both
`sfizz.lv2` and `sfizz.vst3` under `~/studio/plugins/linux-aarch64/`, and
confirmed rendering real audio from two sample libraries
(`~/studio/samples/sfz/SplendidGrandPiano/`, `~/studio/samples/sfz/Terkelsen.Marimba/`)
via the standalone `sfizz_render --sfz ... --midi ... --wav ...` CLI tool
(also built from the same source tree). On Dave's real desktop (not this
Xvfb rig) the GUI file-load path should work normally — worth a one-time
manual check there before assuming it's broken everywhere.

## Optional: smarter search via the MCP wrapper

If the ReaClaw MCP server (`mcp/`) is running, its `search_actions` tool does
**semantic** ranking (natural language → action) via local embeddings, falling
back to keyword search. Prefer it when keyword search misses (e.g. "make the
drums quieter" → volume actions).

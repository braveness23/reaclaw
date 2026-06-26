# ReaClaw: Example Agent Flows

All examples use `curl` against the default local server.
Replace `sk_change_me` with your configured API key.

```bash
BASE="https://localhost:9091"
KEY="sk_change_me"
AUTH="Authorization: Bearer $KEY"
```

Add `-k` to skip certificate verification for the default self-signed cert.

---

## Script Registration

### Register a Lua ReaScript

```bash
curl -sk -X POST "$BASE/scripts/register" \
  -H "$AUTH" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "parallel_comp_drums",
    "script": "local tr = reaper.GetSelectedTrack(0, 0)\nif not tr then return end\nreaper.SetTrackSendInfo_Value(tr, 0, 0, \"B_MUTE\", 0)\nreaper.Main_OnCommand(reaper.NamedCommandLookup(\"_S&M_SEND_SEL1\"), 0)",
    "tags": ["compression", "parallel", "drums"]
  }'
```

**Success response:**
```json
{
  "action_id": "_parallel_comp_drums_a1b2c3d4",
  "registered": true,
  "script_path": "/Users/dave/Library/Application Support/REAPER/reaclaw/scripts/_parallel_comp_drums_a1b2c3d4.lua"
}
```

**Syntax error response:**
```json
{
  "registered": false,
  "syntax_error": {
    "line": 7,
    "message": "'end' expected (to close 'function') near '<eof>'"
  }
}
```

### Register the same name again (idempotent)

```bash
curl -sk -X POST "$BASE/scripts/register" \
  -H "$AUTH" \
  -H "Content-Type: application/json" \
  -d '{ "name": "parallel_comp_drums", "script": "..." }'
# Returns the same action_id as before — nothing written to disk or REAPER
```

### Execute a registered script

```bash
curl -sk -X POST "$BASE/execute/action" \
  -H "$AUTH" \
  -H "Content-Type: application/json" \
  -H "X-Agent-Id: sparky" \
  -d '{ "id": "_parallel_comp_drums_a1b2c3d4", "feedback": true }'
```

### Check if a script is already cached (before generating)

```bash
# Search by tag
curl -sk "$BASE/scripts/cache?tags=parallel" -H "$AUTH"

# If empty, generate and register; if found, just execute:
curl -sk -X POST "$BASE/execute/action" \
  -H "$AUTH" \
  -d '{ "id": "_parallel_comp_drums_a1b2c3d4" }'
```

### List all registered scripts

```bash
curl -sk "$BASE/scripts/cache" -H "$AUTH"
```

### Read a specific script's source

```bash
curl -sk "$BASE/scripts/_parallel_comp_drums_a1b2c3d4" -H "$AUTH"
```

### Delete a registered script

```bash
curl -sk -X DELETE "$BASE/scripts/_parallel_comp_drums_a1b2c3d4" -H "$AUTH"
```

---

## Multi-Step Sequences

### Simple 3-step sequence

```bash
curl -sk -X POST "$BASE/execute/sequence" \
  -H "$AUTH" \
  -H "Content-Type: application/json" \
  -H "X-Agent-Id: sparky" \
  -d '{
    "steps": [
      { "id": 40285, "label": "mute kick" },
      { "id": 40280, "label": "select kick" },
      { "id": 1013,  "label": "start recording" }
    ],
    "feedback_between_steps": true,
    "stop_on_failure": true
  }'
```

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
      "feedback": {
        "transport": { "playing": false, "recording": false, "paused": false },
        "tracks": [{ "index": 0, "name": "Kick", "muted": true, ... }]
      }
    },
    { "label": "select kick", "action_id": 40280, "status": "success", "feedback": {...} },
    { "label": "start recording", "action_id": 1013, "status": "success", "feedback": {...} }
  ]
}
```

### Sequence with a registered script step

```bash
curl -sk -X POST "$BASE/execute/sequence" \
  -H "$AUTH" \
  -H "Content-Type: application/json" \
  -d '{
    "steps": [
      { "id": 40280,                          "label": "select kick track" },
      { "id": "_setup_sidechain_a1b2c3d4",   "label": "setup sidechain compress" },
      { "id": 1013,                           "label": "record" }
    ],
    "feedback_between_steps": true,
    "stop_on_failure": true
  }'
```

### Sequence that stops on failure

If step 2 fails (action not found, timeout, etc.) and `stop_on_failure` is true,
step 3 is marked `"skipped"`:

```json
{
  "status": "failed",
  "steps_completed": 1,
  "steps": [
    { "label": "select kick", "status": "success", "feedback": {...} },
    { "label": "bad action",  "status": "failed",  "error": "action not found or invalid id" },
    { "label": "record",      "status": "skipped" }
  ]
}
```

---

## State Queries

### Get full project state

```bash
curl -sk "$BASE/state" -H "$AUTH"
```

### List all tracks

```bash
curl -sk "$BASE/state/tracks" -H "$AUTH"
```

### Mute track 0 directly (no action lookup)

```bash
curl -sk -X POST "$BASE/state/tracks/0" \
  -H "$AUTH" \
  -H "Content-Type: application/json" \
  -d '{ "muted": true }'
```

### Set volume and pan on track 2

```bash
curl -sk -X POST "$BASE/state/tracks/2" \
  -H "$AUTH" \
  -H "Content-Type: application/json" \
  -d '{ "volume_db": -6.0, "pan": -0.5 }'
```

---

## Content: Items, Takes & Sources

### Drop an audio file onto a track as a new item

```bash
curl -sk -X POST "$BASE/state/items" \
  -H "$AUTH" \
  -H "Content-Type: application/json" \
  -d '{ "create": [ { "track": 0, "position": 0, "file": "/audio/vox.wav" } ] }'
# length defaults to the source length; pass "length" to trim
```

### Split an item at a position, then nudge the take

```bash
curl -sk -X POST "$BASE/state/items/0/split" -H "$AUTH" -d '{ "position": 2.5 }'

curl -sk -X POST "$BASE/state/items/0" \
  -H "$AUTH" \
  -d '{ "take": { "volume_db": -3.0, "pitch": -2, "preserve_pitch": true } }'
```

### Read an item with its take + source metadata

```bash
curl -sk "$BASE/state/items/0" -H "$AUTH"
# → { ..., "take": { "name": ..., "pan": 0 },
#         "source": { "file": ..., "type": "WAVE", "sample_rate": 48000, "num_channels": 2 } }
```

---

## FX: Add, Tune, Copy, Preset

### Add ReaEQ to track 0 and set a band

```bash
curl -sk -X POST "$BASE/state/tracks/0/fx" \
  -H "$AUTH" \
  -d '{ "name": "ReaEQ", "params": [ { "name": "Gain", "value": 0.6 } ] }'
```

### Copy a dialed-in FX to another track

```bash
curl -sk -X POST "$BASE/state/tracks/0/fx/0/copy" \
  -H "$AUTH" \
  -d '{ "to_track": 3, "to_slot": -1, "move": false }'
```

### Step through FX presets / take an FX offline

```bash
curl -sk -X POST "$BASE/state/tracks/0/fx/0/preset" -H "$AUTH" -d '{ "navigate": 1 }'
curl -sk -X POST "$BASE/state/tracks/0/fx/0"        -H "$AUTH" -d '{ "offline": true }'
```

---

## 👂 Perception: Let the Agent Hear Itself

### Measure the loudness & spectral balance of an item

```bash
curl -sk "$BASE/analysis/item/0?measures=loudness,spectral" -H "$AUTH"
```

```json
{
  "loudness": {
    "lufs_i": -14.2, "rms_i": -16.8, "peak_db": -1.0, "true_peak_db": -0.7,
    "clipping": { "digital": false, "inter_sample": false },
    "method": "offline_analysis", "confidence": 1.0
  },
  "spectral": {
    "low": 0.21, "mid": 0.55, "high": 0.24, "centroid_hz": 2173.4,
    "method": "estimated_dsp", "confidence": 0.6
  }
}
```

### Analyze an arbitrary file (not yet in the project)

```bash
curl -sk "$BASE/analysis/file?path=/audio/master.wav&measures=loudness" -H "$AUTH"
```

### Read live meters (per-track + master peak / peak-hold)

```bash
curl -sk "$BASE/state/meters" -H "$AUTH"
# → { "audio_running": true, "master": { "peak_db": -3.2, "peak_hold_db": -1.1 }, "tracks": [...] }
```

### Render a labelled PNG and save it

```bash
curl -sk "$BASE/analysis/item/0/visualize?type=waveform&width=640&height=200" -H "$AUTH" \
  | jq -r '.image.base64' | base64 -d > waveform.png
# types: spectrum | waveform | loudness   ·   image=none for digest-only
```

### Probe the musical attributes (key / pitch / tempo)

```bash
curl -sk "$BASE/analysis/item/0/probe" -H "$AUTH" \
  | jq '{pitch: .pitch.note, key: .key.key, tempo: .tempo.project.bpm}'
# → { "pitch": "A4", "key": "A minor", "tempo": 120 }
```

Pitch and key are built-in DSP (`estimated_dsp`); tempo is exact project
introspection plus an optional external detector that degrades gracefully when
absent. Filter with `?probes=pitch` / `key` / `tempo`.

### Screenshot a named REAPER surface (GUI-only fallback)

```bash
# Frame the mixer (or arrange / fxchain / midi / routing / master / transport),
# downscaled to bound token cost:
curl -sk "$BASE/screenshot?target=mixer&width=800" -H "$AUTH" \
  | jq -r '.image.base64' | base64 -d > mixer.png
# Structure-first stays the default — reach for this only for GUI-only state.
```

---

## 🪝 Consequence Hints (free, on every mutation)

Mutating responses carry a `hints[]` array — no extra call needed:

```bash
curl -sk -X POST "$BASE/state/tracks/3" -H "$AUTH" -d '{ "armed": true }'
```

```json
{
  "ok": true,
  "hints": [
    { "code": "recarm_no_input", "severity": "warn",
      "message": "Track armed for recording but has no record input assigned." }
  ]
}
```

---

## Project Scratchpad (ext-state, survives close/reopen)

```bash
# Stash agent context inside the .rpp itself
curl -sk -X POST "$BASE/project/extstate" -H "$AUTH" \
  -d '{ "section": "reaclaw", "key": "mix_intent", "value": "warm, vocal-forward" }'

curl -sk "$BASE/project/extstate?section=reaclaw&key=mix_intent" -H "$AUTH"
```

---

## Catalog Search

### Find mute actions

```bash
curl -sk "$BASE/catalog/search?q=mute" -H "$AUTH"
```

### Look up a specific action by ID

```bash
curl -sk "$BASE/catalog/40285" -H "$AUTH"
```

### Browse categories

```bash
curl -sk "$BASE/catalog/categories" -H "$AUTH"
```

---

## Execution History

### Last 10 executions

```bash
curl -sk "$BASE/history?limit=10" -H "$AUTH"
```

### Filter by agent

```bash
curl -sk "$BASE/history?agent_id=sparky" -H "$AUTH"
```

---

## Snapshots & State Diff (#20)

### Capture a baseline, make changes, see exactly what moved

```bash
# Baseline before an edit pass
ID=$(curl -sk -X POST "$BASE/snapshot" -H "$AUTH" -d '{"label":"before"}' | jq .id)

# ... make some edits (mute a track, add FX, etc.) ...
curl -sk -X POST "$BASE/state/tracks/0" -H "$AUTH" -d '{"muted":true}' >/dev/null

# What changed since the baseline (to defaults to a live capture)
curl -sk "$BASE/snapshot/diff?from=$ID" -H "$AUTH" | jq '.changes'
# → [ { "path": "tracks/0/muted", "op": "changed", "from": false, "to": true } ]
```

List, fetch, or delete stored snapshots:
```bash
curl -sk "$BASE/snapshot"        -H "$AUTH"   # list
curl -sk "$BASE/snapshot/$ID"    -H "$AUTH"   # full stored state
curl -sk -X DELETE "$BASE/snapshot/$ID" -H "$AUTH"
```

---

## Learned Suggestions (#20 — local-first, opt-in)

Off by default. Enable in `config.json` (`"learning": { "enabled": true }`) and
restart REAPER. Then ReaClaw mines your own edit history locally:

```bash
# After enough edits, ask what usually follows a given action:
curl -sk "$BASE/suggestions?after=track.create" -H "$AUTH" | jq '.suggestions'
# → [ { "after":"track.create", "suggest":"track.set:color",
#       "support":7, "confidence":0.64, "method":"learned" } ]

# Or omit ?after to use the calling agent's most recent edit (X-Agent-Id header).
curl -sk "$BASE/suggestions" -H "$AUTH" -H "X-Agent-Id: sparky" | jq

# What's been learned locally:
curl -sk "$BASE/learn/stats" -H "$AUTH" | jq
```

Nothing is recorded while disabled, and nothing ever leaves the machine.

---

## Track Icons (#29)

### Discover available icons, then assign one

```bash
# See what icons are installed
curl -sk "$BASE/state/track-icons" -H "$AUTH" | jq '.icons[:10]'
# → ["ac_guitar.png", "amp.png", "bass.png", "drums.png", "kick.png", ...]

# Assign a factory icon by relative name
curl -sk -X POST "$BASE/state/tracks/0" -H "$AUTH" \
  -d '{"icon": "bass.png"}'
# → { "index": 0, "name": "Bass", "icon": "bass.png", ... }

# Read it back in a tracks listing
curl -sk "$BASE/state/tracks" -H "$AUTH" | jq '.[0].icon'
# → "bass.png"

# Clear the icon
curl -sk -X POST "$BASE/state/tracks/0" -H "$AUTH" \
  -d '{"icon": null}'
# → { "index": 0, "icon": null, ... }
```

### Use an absolute path for a custom PNG

```bash
curl -sk -X POST "$BASE/state/tracks/2" -H "$AUTH" \
  -d '{"icon": "/home/user/icons/my_custom_synth.png"}'
```

### Create tracks with icons in one batch call

```bash
curl -sk -X POST "$BASE/state/tracks" -H "$AUTH" -d '{
  "create": [
    { "name": "Drums",   "color": "#CC3333", "folder_depth": 1, "icon": "drums.png" },
    { "name": "Kick",    "volume_db": -3.0,                     "icon": "kick.png"  },
    { "name": "Snare",   "volume_db": -3.0,                     "icon": "snare.png" },
    { "name": "/Drums",  "folder_depth": -1 }
  ]
}' | jq '.created[].icon'
# → "drums.png"  "kick.png"  "snare.png"  null
```

### icon_not_found hint when the name doesn't resolve

```bash
curl -sk -X POST "$BASE/state/tracks/0" -H "$AUTH" \
  -d '{"icon": "typo_icon.png"}' | jq '.hints'
# → [{ "code": "icon_not_found", "severity": "warn",
#      "message": "Icon 'typo_icon.png' was not found under Data/track_icons; ..." }]
```

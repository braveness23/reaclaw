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

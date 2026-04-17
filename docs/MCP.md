# ReaClaw MCP Integration

ReaClaw exposes a REST+JSON API. This document explains how to configure it as a set of MCP tools so AI agents (including Sparky via OpenClaw) can control REAPER natively.

ReaClaw is not an MCP server itself — it speaks plain HTTPS/JSON. The MCP layer is a thin wrapper that maps MCP tool calls to ReaClaw API requests. This can be done via:

- **OpenClaw stdio MCP server** — a small Node.js/Python script that bridges MCP ↔ ReaClaw HTTP
- **Agent-side tool definitions** — define tools in the agent's system prompt that call ReaClaw directly

---

## Tool Definitions

### `reaclawSearchCatalog`

Search REAPER's action catalog for actions matching a query.

**Maps to:** `GET /catalog/search?q={query}&limit={limit}`

```json
{
  "name": "reaclawSearchCatalog",
  "description": "Search REAPER's action catalog. Returns matching actions with their numeric IDs and names. Use this before executing an action by name.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "query": {
        "type": "string",
        "description": "Search terms (e.g. 'mute selected tracks', 'record', 'normalize')"
      },
      "limit": {
        "type": "integer",
        "description": "Maximum results to return (default 20, max 100)",
        "default": 20
      }
    },
    "required": ["query"]
  }
}
```

---

### `reaclawQueryState`

Get current REAPER project state: transport, tracks, BPM, cursor position.

**Maps to:** `GET /state` and/or `GET /state/tracks`

```json
{
  "name": "reaclawQueryState",
  "description": "Get current REAPER state. Returns transport status (playing/recording/paused), BPM, cursor position, track count, and optionally full track details.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "include_tracks": {
        "type": "boolean",
        "description": "If true, include full track list with mute/solo/armed/volume/pan/FX. Slightly slower.",
        "default": false
      }
    }
  }
}
```

---

### `reaclawExecuteAction`

Execute a single REAPER action by numeric ID or registered script ID.

**Maps to:** `POST /execute/action`

```json
{
  "name": "reaclawExecuteAction",
  "description": "Execute a REAPER action. Use numeric ID for built-in actions (from reaclawSearchCatalog) or a registered script ID string (from reaclawRegisterScript). Set feedback=true to get REAPER state after execution.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "id": {
        "oneOf": [
          { "type": "integer", "description": "Built-in action command ID (e.g. 40285)" },
          { "type": "string",  "description": "Registered script action ID (e.g. '_mute_drums_a1b2c3d4')" }
        ]
      },
      "feedback": {
        "type": "boolean",
        "description": "Return REAPER state (transport + tracks) after execution",
        "default": false
      }
    },
    "required": ["id"]
  }
}
```

---

### `reaclawExecuteSequence`

Execute multiple REAPER actions in order with optional per-step feedback.

**Maps to:** `POST /execute/sequence`

```json
{
  "name": "reaclawExecuteSequence",
  "description": "Execute a sequence of REAPER actions in order. Use for multi-step operations. Each step can have a label for clarity in results.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "steps": {
        "type": "array",
        "description": "Ordered list of actions to execute",
        "items": {
          "type": "object",
          "properties": {
            "id":    { "oneOf": [{ "type": "integer" }, { "type": "string" }] },
            "label": { "type": "string", "description": "Human-readable step name for result tracking" }
          },
          "required": ["id"]
        },
        "maxItems": 100
      },
      "feedback_between_steps": {
        "type": "boolean",
        "description": "Capture REAPER state after each step (useful for conditional logic)",
        "default": false
      },
      "stop_on_failure": {
        "type": "boolean",
        "description": "Abort sequence on first failed step",
        "default": true
      }
    },
    "required": ["steps"]
  }
}
```

---

### `reaclawRegisterScript`

Register an agent-generated Lua ReaScript as a custom REAPER action.

**Maps to:** `POST /scripts/register`

```json
{
  "name": "reaclawRegisterScript",
  "description": "Register a Lua ReaScript as a custom REAPER action. The script is validated for syntax, written to disk, and registered via AddRemoveReaScript. Returns an action_id you can pass to reaclawExecuteAction. Registration is idempotent — same name returns the existing ID.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "name": {
        "type": "string",
        "description": "Short snake_case identifier for the script (e.g. 'parallel_comp_drums')"
      },
      "script": {
        "type": "string",
        "description": "Complete Lua ReaScript source code"
      },
      "tags": {
        "type": "array",
        "items": { "type": "string" },
        "description": "Optional tags for later lookup (e.g. ['compression', 'drums'])"
      }
    },
    "required": ["name", "script"]
  }
}
```

---

## OpenClaw Configuration

To use ReaClaw with Sparky via OpenClaw, define the tools above in Sparky's context and call ReaClaw's HTTPS API directly from skill code.

### Example skill (`~/.openclaw/workspace/skills/reaclaw/SKILL.md`)

```markdown
# ReaClaw Skill

You have access to ReaClaw — a REAPER DAW controller running at https://localhost:9091.
Authentication: Authorization: Bearer {REACLAW_API_KEY}
TLS: self-signed cert, use --insecure / verify=False equivalent.

## Available operations

- Search actions: GET /catalog/search?q={query}
- Execute action: POST /execute/action {"id": <int|string>, "feedback": true}
- Execute sequence: POST /execute/sequence {"steps": [...]}
- Register script: POST /scripts/register {"name": "...", "script": "..."}
- Get state: GET /state
- Get tracks: GET /state/tracks
- View history: GET /history
```

### Environment

Store the API key in the systemd service environment:

```
Environment=REACLAW_API_KEY=sk_your_key_here
```

Reference it in OpenClaw's tool or skill config as needed.

---

## Example Agent Flows

### Find and execute an action

```
Agent: "Mute the selected tracks"
→ reaclawSearchCatalog(query="mute selected tracks")
← [{id: 40285, name: "Track: Toggle mute for selected tracks"}]
→ reaclawExecuteAction(id=40285, feedback=true)
← {status: "success", feedback: {tracks: [{muted: true, ...}]}}
```

### Register and cache a script

```
Agent: (generates Lua for parallel compression)
→ reaclawRegisterScript(name="parallel_comp_drums", script="local tr = ...")
← {action_id: "_parallel_comp_drums_a1b2c3d4", registered: true}
→ reaclawExecuteAction(id="_parallel_comp_drums_a1b2c3d4")
← {status: "success"}

Next session:
→ GET /scripts/cache?tags=compression  ← finds cached script, no regeneration needed
→ reaclawExecuteAction(id="_parallel_comp_drums_a1b2c3d4")
```

### Multi-step sequence

```
→ reaclawExecuteSequence(steps=[
    {id: 40280, label: "select kick"},
    {id: "_setup_sidechain_xyz", label: "setup sidechain"},
    {id: 1013, label: "record"}
  ], feedback_between_steps=true, stop_on_failure=true)
← {status: "success", steps_completed: 3, steps: [...]}
```

---

## Stdio MCP Server

`mcp/server.py` is a ready-to-use stdio MCP bridge. It translates MCP `tools/call` JSON-RPC messages into ReaClaw HTTPS requests and returns results.

### Install

```bash
cd mcp
pip install -r requirements.txt
```

### Run

```bash
REACLAW_API_KEY=sk_your_key_here python mcp/server.py
```

### Environment variables

| Variable | Default | Description |
|---|---|---|
| `REACLAW_BASE_URL` | `https://localhost:9091` | ReaClaw server URL |
| `REACLAW_API_KEY` | *(empty)* | Bearer API key; leave unset if `auth.type` is `"none"` |
| `REACLAW_SSL_VERIFY` | `false` | Set to `"true"` to verify TLS cert (needed for CA-signed certs) |

SSL verification is off by default because ReaClaw uses a self-signed cert in dev/home-network deployments.

### Claude Desktop / claude_desktop_config.json

```json
{
  "mcpServers": {
    "reaclaw": {
      "command": "python",
      "args": ["/path/to/reaclaw/mcp/server.py"],
      "env": {
        "REACLAW_BASE_URL": "https://localhost:9091",
        "REACLAW_API_KEY": "sk_your_key_here"
      }
    }
  }
}
```

### Available tools

| Tool | Maps to |
|---|---|
| `reaclaw_health` | `GET /health` |
| `reaclaw_search_catalog` | `GET /catalog/search` |
| `reaclaw_get_catalog` | `GET /catalog` |
| `reaclaw_get_catalog_categories` | `GET /catalog/categories` |
| `reaclaw_get_action` | `GET /catalog/{id}` |
| `reaclaw_get_state` | `GET /state` (+ optional `/state/tracks`) |
| `reaclaw_get_tracks` | `GET /state/tracks` |
| `reaclaw_get_items` | `GET /state/items` |
| `reaclaw_get_selection` | `GET /state/selection` |
| `reaclaw_get_automation` | `GET /state/automation` |
| `reaclaw_set_track` | `POST /state/tracks/{index}` |
| `reaclaw_execute_action` | `POST /execute/action` |
| `reaclaw_execute_sequence` | `POST /execute/sequence` |
| `reaclaw_register_script` | `POST /scripts/register` |
| `reaclaw_get_script_cache` | `GET /scripts/cache` |
| `reaclaw_get_script` | `GET /scripts/{id}` |
| `reaclaw_delete_script` | `DELETE /scripts/{id}` |
| `reaclaw_get_history` | `GET /history` |

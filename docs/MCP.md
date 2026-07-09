# ReaClaw MCP Integration

ReaClaw exposes a REST+JSON API. This document explains how to configure it as a set of MCP tools so AI agents (including Sparky via OpenClaw) can control REAPER natively.

ReaClaw is not an MCP server itself — it speaks plain HTTPS/JSON. The MCP layer is a thin wrapper that maps MCP tool calls to ReaClaw API requests. This can be done via:

- **OpenClaw stdio MCP server** — a small Node.js/Python script that bridges MCP ↔ ReaClaw HTTP
- **Agent-side tool definitions** — define tools in the agent's system prompt that call ReaClaw directly

---

## Reference implementation (`mcp/`)

A ready-to-run Python MCP server now ships in **[`mcp/`](../mcp/)** — see
[`mcp/README.md`](../mcp/README.md). It exposes 18 typed tools over the REST API
(tracks, FX, routing, selection, actions, scripts) plus **semantic action
search** (local Ollama embeddings, keyword fallback). Run it with
`python -m reaclaw_mcp.server` (stdio). The tool definitions below document the
underlying REST mapping; the reference server implements them and more.

There is also a bundled **agent Skill** at [`skill/reaclaw/`](../skill/reaclaw/)
that loads ReaClaw know-how (structured-verb recipes, action cheat-sheet, a
"don't call these modal actions" list, and a decision guide) into an agent's
working context — the highest-ROI, zero-dependency option.

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

The `mcp/` directory ships a ready-to-use stdio MCP server (Python package
`reaclaw_mcp`, FastMCP-based). It translates MCP `tools/call` JSON-RPC messages
into ReaClaw HTTPS requests and returns results. Full install/config/tuning
reference (including the semantic-search warm-up): [`mcp/README.md`](../mcp/README.md).

### Install

```bash
cd mcp
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt        # or: pip install -e .
```

### Run

```bash
REACLAW_API_KEY=sk_your_key_here python -m reaclaw_mcp.server
```

### Environment variables

| Variable | Default | Description |
|---|---|---|
| `REACLAW_URL` | `https://127.0.0.1:9091` | ReaClaw server URL |
| `REACLAW_API_KEY` | `sk_change_me` | Bearer API key from `config.json` |
| `OLLAMA_URL` | `http://localhost:11434` | Embeddings backend for semantic search |
| `REACLAW_EMBED_MODEL` | `nomic-embed-text` | Ollama embedding model |
| `REACLAW_CACHE_DIR` | `~/.cache/reaclaw-mcp` | Catalog-embedding cache |

TLS verification is disabled by the client (ReaClaw uses a self-signed cert in
dev/home-network deployments).

### Claude Desktop / claude_desktop_config.json

```json
{
  "mcpServers": {
    "reaclaw": {
      "command": "python",
      "args": ["-m", "reaclaw_mcp.server"],
      "env": {
        "REACLAW_URL": "https://127.0.0.1:9091",
        "REACLAW_API_KEY": "sk_your_key_here"
      }
    }
  }
}
```

### Available tools

| Tool | Maps to |
|---|---|
| `reaper_health` | `GET /health` |
| `reaper_capabilities` | `GET /capabilities` |
| `reaper_search_actions` | `GET /catalog/search` (semantic ranking with keyword fallback) |
| `reaper_get_state` | `GET /state` |
| `reaper_get_tracks` | `GET /state/tracks` |
| `reaper_create_tracks` | `POST /state/tracks` |
| `reaper_set_track` | `POST /state/tracks/{index}` |
| `reaper_delete_track` | `DELETE /state/tracks/{index}` |
| `reaper_add_fx` | `POST /state/tracks/{index}/fx` |
| `reaper_get_fx` | `GET /state/tracks/{index}/fx/{slot}` |
| `reaper_set_fx` | `POST /state/tracks/{index}/fx/{slot}` |
| `reaper_delete_fx` | `DELETE /state/tracks/{index}/fx/{slot}` |
| `reaper_add_send` | `POST /state/tracks/{index}/sends` |
| `reaper_delete_send` | `DELETE /state/tracks/{index}/sends/{send}` |
| `reaper_set_selection` | `POST /state/selection` |
| `reaper_run_action` | `POST /execute/action` |
| `reaper_run_sequence` | `POST /execute/sequence` |
| `reaper_register_script` | `POST /scripts/register` |

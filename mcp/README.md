# ReaClaw MCP server

A thin [Model Context Protocol](https://modelcontextprotocol.io) server that
exposes ReaClaw (REAPER control) as **typed tools** for any MCP-capable agent.
It's a facade over the ReaClaw REST API — self-describing tool schemas mean fewer
hallucinated request shapes — plus **semantic action search**.

## Install

```bash
cd mcp
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt        # or: pip install -e .
```

## Configure (environment)

| Var | Default | Purpose |
|-----|---------|---------|
| `REACLAW_URL` | `https://127.0.0.1:9091` | ReaClaw base URL |
| `REACLAW_API_KEY` | `sk_change_me` | bearer token from `config.json` |
| `OLLAMA_URL` | `http://localhost:11434` | embeddings for semantic search |
| `REACLAW_EMBED_MODEL` | `nomic-embed-text` | Ollama embedding model |
| `REACLAW_CACHE_DIR` | `~/.cache/reaclaw-mcp` | catalog-embedding cache |

ReaClaw uses a self-signed cert; the client disables TLS verification by default.

## Run

```bash
python -m reaclaw_mcp.server          # stdio transport
```

Register it with your MCP client (example: Claude Desktop / Code config):

```json
{
  "mcpServers": {
    "reaclaw": {
      "command": "python",
      "args": ["-m", "reaclaw_mcp.server"],
      "env": { "REACLAW_API_KEY": "sk_change_me" }
    }
  }
}
```

## Tools

Discovery: `reaper_health`, `reaper_capabilities`, `reaper_search_actions`
(semantic, with keyword fallback) · Read: `reaper_get_state`, `reaper_get_tracks`
· Tracks: `reaper_create_tracks`, `reaper_set_track`, `reaper_delete_track` · FX:
`reaper_add_fx`, `reaper_get_fx`, `reaper_set_fx`, `reaper_delete_fx` · Routing /
selection: `reaper_add_send`, `reaper_delete_send`, `reaper_set_selection` ·
Actions / scripts: `reaper_run_action`, `reaper_run_sequence`,
`reaper_register_script`.

## Semantic search

`reaper_search_actions` embeds the ~6700-action catalog once via Ollama
(`nomic-embed-text`), caches it under `REACLAW_CACHE_DIR` keyed by catalog
version, and ranks by cosine similarity — so "make the drums quieter" finds
volume actions that keyword search misses. If Ollama or numpy is unavailable it
falls back to the server's keyword search; the response `mode` field says which
path ran.

**One-time warm-up cost.** The first semantic search embeds the whole catalog.
On a fast CPU/GPU this is seconds-to-a-minute; on small hardware (e.g. a
Raspberry Pi, ~0.35 s/action) the full catalog can take ~30–40 minutes. It runs
once and is then cached forever (until the catalog version changes), and progress
is printed to stderr so it never looks hung. Tunables:

| Var | Default | Effect |
|-----|---------|--------|
| `REACLAW_EMBED_MAX` | `0` (all) | cap actions embedded — trade coverage for a fast warm-up on slow hardware |
| `REACLAW_EMBED_BATCH` | `128` | names per Ollama request |
| `REACLAW_EMBED_TIMEOUT` | `600` | seconds per embed request |

Until the cache is warm, semantic searches transparently fall back to keyword
search, so the tool is always usable.

## Without the MCP framework

`reaclaw_mcp.client.ReaClawClient` is a plain Python class (stdlib HTTP) usable
directly in scripts:

```python
from reaclaw_mcp.client import ReaClawClient
rc = ReaClawClient()
rc.create_tracks(create=[{"name": "Kick", "color": "#33AA55"}])
print(rc.search_actions("mute all drums")["actions"][:5])
```

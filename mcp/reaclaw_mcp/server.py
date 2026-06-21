"""ReaClaw MCP server — typed tools over the ReaClaw REST API.

A thin facade: every tool delegates to ReaClawClient. Configure via env:
  REACLAW_URL (default https://127.0.0.1:9091), REACLAW_API_KEY,
  OLLAMA_URL (for semantic search), REACLAW_EMBED_MODEL.

Run:  python -m reaclaw_mcp.server         (stdio transport)
"""

from __future__ import annotations

from typing import Any, Optional

from mcp.server.fastmcp import FastMCP

from .client import ReaClawClient, ReaClawError

mcp = FastMCP("reaclaw")
_client: Optional[ReaClawClient] = None


def client() -> ReaClawClient:
    global _client
    if _client is None:
        _client = ReaClawClient()
    return _client


def _safe(fn, *args, **kwargs) -> dict:
    try:
        return fn(*args, **kwargs)
    except ReaClawError as e:
        return {"error": str(e)}


# ---- discovery -------------------------------------------------------------


@mcp.tool()
def reaper_health() -> dict:
    """Server health: version, catalog size, db/server status, uptime."""
    return _safe(client().health)


@mcp.tool()
def reaper_capabilities() -> dict:
    """What the API supports directly (structured verbs) vs. via action/script."""
    return _safe(client().capabilities)


@mcp.tool()
def reaper_search_actions(query: str, limit: int = 20, semantic: bool = True) -> dict:
    """Find REAPER actions for a natural-language query. Uses local-embedding
    semantic ranking when available, else keyword search. Returns {mode, actions}."""
    return _safe(client().search_actions, query, limit=limit, semantic=semantic)


# ---- read ------------------------------------------------------------------


@mcp.tool()
def reaper_get_state() -> dict:
    """Project + transport state (tempo, time-sig, cursor, play state)."""
    return _safe(client().get_state)


@mcp.tool()
def reaper_get_tracks() -> dict:
    """All tracks with name, folder_depth, color, vol/pan, mute/solo/arm, fx[], sends[]."""
    return _safe(client().get_tracks)


# ---- tracks ----------------------------------------------------------------


@mcp.tool()
def reaper_create_tracks(create: list, update: Optional[list] = None) -> dict:
    """Create (and optionally batch-update) tracks. Each spec: name, color
    (#RRGGBB), folder_depth, volume_db, pan, muted, soloed, armed."""
    return _safe(client().create_tracks, create=create, update=update)


@mcp.tool()
def reaper_set_track(
    index: int,
    name: Optional[str] = None,
    color: Optional[str] = None,
    folder_depth: Optional[int] = None,
    volume_db: Optional[float] = None,
    pan: Optional[float] = None,
    muted: Optional[bool] = None,
    soloed: Optional[bool] = None,
    armed: Optional[bool] = None,
) -> dict:
    """Update one track's properties (only the fields you pass are changed)."""
    fields = {
        k: v
        for k, v in dict(
            name=name,
            color=color,
            folder_depth=folder_depth,
            volume_db=volume_db,
            pan=pan,
            muted=muted,
            soloed=soloed,
            armed=armed,
        ).items()
        if v is not None
    }
    return _safe(client().set_track, index, **fields)


@mcp.tool()
def reaper_delete_track(index: int) -> dict:
    """Delete the track at the given 0-based index."""
    return _safe(client().delete_track, index)


# ---- fx --------------------------------------------------------------------


@mcp.tool()
def reaper_add_fx(
    index: int, name: str, enabled: Optional[bool] = None, params: Optional[list] = None
) -> dict:
    """Add an FX by name (e.g. "ReaComp"). params: [{index|name, value(0..1)}]."""
    return _safe(client().add_fx, index, name, enabled=enabled, params=params)


@mcp.tool()
def reaper_get_fx(index: int, slot: int) -> dict:
    """Read an FX slot's parameters (index, name, normalized value, formatted)."""
    return _safe(client().get_fx, index, slot)


@mcp.tool()
def reaper_set_fx(
    index: int, slot: int, enabled: Optional[bool] = None, params: Optional[list] = None
) -> dict:
    """Set an FX slot's enabled state and/or parameters (normalized 0..1)."""
    return _safe(client().set_fx, index, slot, enabled=enabled, params=params)


@mcp.tool()
def reaper_delete_fx(index: int, slot: int) -> dict:
    """Remove an FX slot from a track."""
    return _safe(client().delete_fx, index, slot)


# ---- routing / selection ---------------------------------------------------


@mcp.tool()
def reaper_add_send(
    index: int, to_track: int, volume_db: Optional[float] = None, pan: Optional[float] = None
) -> dict:
    """Add a send from track `index` to track `to_track`."""
    return _safe(client().add_send, index, to_track, volume_db=volume_db, pan=pan)


@mcp.tool()
def reaper_delete_send(index: int, send: int) -> dict:
    """Remove a send by its index from a track."""
    return _safe(client().delete_send, index, send)


@mcp.tool()
def reaper_set_selection(tracks: Any) -> dict:
    """Set track selection: a list of indices, or "all" / "none"."""
    return _safe(client().set_selection, tracks)


# ---- actions / scripts -----------------------------------------------------


@mcp.tool()
def reaper_run_action(id: Any, feedback: bool = False) -> dict:
    """Run a REAPER action by numeric id or registered command string."""
    return _safe(client().run_action, id, feedback=feedback)


@mcp.tool()
def reaper_run_sequence(steps: list, stop_on_failure: bool = True) -> dict:
    """Run a sequence of actions: steps = [{"id": ..., "label": ...}, ...]."""
    return _safe(client().run_sequence, steps, stop_on_failure=stop_on_failure)


@mcp.tool()
def reaper_register_script(name: str, body: str) -> dict:
    """Register a Lua ReaScript as a custom action; returns its command id."""
    return _safe(client().register_script, name, body)


def main() -> None:
    mcp.run()


if __name__ == "__main__":
    main()

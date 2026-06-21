"""ReaClaw REST client + optional semantic action search.

This is the engine the MCP server wraps. It deliberately uses only the Python
standard library for HTTP (so it runs and can be tested without the MCP
framework) and numpy for vector math. Semantic search calls a local Ollama
embeddings model and degrades gracefully to the server's keyword search when
Ollama or numpy is unavailable.
"""

from __future__ import annotations

import json
import os
import ssl
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any, Optional

try:
    import numpy as np
except Exception:  # pragma: no cover - numpy is a declared dep
    np = None  # type: ignore


class ReaClawError(Exception):
    """Raised when the ReaClaw API returns an error or is unreachable."""


class ReaClawClient:
    def __init__(
        self,
        base_url: Optional[str] = None,
        api_key: Optional[str] = None,
        ollama_url: Optional[str] = None,
        embed_model: str = "nomic-embed-text",
        cache_dir: Optional[str] = None,
        verify_tls: bool = False,
        timeout: float = 30.0,
    ):
        self.base_url = (base_url or os.environ.get("REACLAW_URL", "https://127.0.0.1:9091")).rstrip(
            "/"
        )
        self.api_key = api_key or os.environ.get("REACLAW_API_KEY", "sk_change_me")
        self.ollama_url = (
            ollama_url or os.environ.get("OLLAMA_URL", "http://localhost:11434")
        ).rstrip("/")
        self.embed_model = os.environ.get("REACLAW_EMBED_MODEL", embed_model)
        self.timeout = timeout
        # Embedding the whole catalog is a one-time cost and can be slow on CPU /
        # small hardware, so give it a much longer timeout than normal requests.
        self.embed_timeout = float(os.environ.get("REACLAW_EMBED_TIMEOUT", "600"))
        self.embed_batch = int(os.environ.get("REACLAW_EMBED_BATCH", "128"))
        # Cap how many actions get embedded (0 = all). Useful on slow CPUs where
        # embedding the full ~6700-action catalog is a multi-minute one-time cost.
        self.embed_max = int(os.environ.get("REACLAW_EMBED_MAX", "0"))
        self.cache_dir = Path(
            cache_dir or os.environ.get("REACLAW_CACHE_DIR", "~/.cache/reaclaw-mcp")
        ).expanduser()
        self._ssl = ssl.create_default_context()
        if not verify_tls:
            self._ssl.check_hostname = False
            self._ssl.verify_mode = ssl.CERT_NONE

    # ---- low-level HTTP ----------------------------------------------------

    def _request(
        self,
        method: str,
        path: str,
        body: Optional[dict] = None,
        params: Optional[dict] = None,
    ) -> Any:
        url = self.base_url + path
        if params:
            url += "?" + urllib.parse.urlencode({k: v for k, v in params.items() if v is not None})
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(url, data=data, method=method)
        req.add_header("Authorization", f"Bearer {self.api_key}")
        if data is not None:
            req.add_header("Content-Type", "application/json")
        try:
            with urllib.request.urlopen(req, timeout=self.timeout, context=self._ssl) as resp:
                raw = resp.read()
        except urllib.error.HTTPError as e:
            detail = e.read().decode(errors="replace")
            raise ReaClawError(f"{method} {path} -> {e.code}: {detail}") from e
        except urllib.error.URLError as e:
            raise ReaClawError(f"{method} {path} -> unreachable: {e}") from e
        return json.loads(raw) if raw else {}

    # ---- read --------------------------------------------------------------

    def health(self) -> dict:
        return self._request("GET", "/health")

    def capabilities(self) -> dict:
        return self._request("GET", "/capabilities")

    def get_state(self) -> dict:
        return self._request("GET", "/state")

    def get_tracks(self) -> dict:
        return self._request("GET", "/state/tracks")

    def get_items(self) -> dict:
        return self._request("GET", "/state/items")

    def get_selection(self) -> dict:
        return self._request("GET", "/state/selection")

    def history(self, limit: int = 50) -> dict:
        return self._request("GET", "/history", params={"limit": limit})

    # ---- tracks ------------------------------------------------------------

    def create_tracks(
        self, create: Optional[list] = None, update: Optional[list] = None
    ) -> dict:
        body: dict = {}
        if create is not None:
            body["create"] = create
        if update is not None:
            body["update"] = update
        return self._request("POST", "/state/tracks", body=body)

    def set_track(self, index: int, **fields: Any) -> dict:
        return self._request("POST", f"/state/tracks/{index}", body=fields)

    def delete_track(self, index: int) -> dict:
        return self._request("DELETE", f"/state/tracks/{index}")

    # ---- fx ----------------------------------------------------------------

    def add_fx(
        self, index: int, name: str, enabled: Optional[bool] = None, params: Optional[list] = None
    ) -> dict:
        body: dict = {"name": name}
        if enabled is not None:
            body["enabled"] = enabled
        if params is not None:
            body["params"] = params
        return self._request("POST", f"/state/tracks/{index}/fx", body=body)

    def get_fx(self, index: int, slot: int) -> dict:
        return self._request("GET", f"/state/tracks/{index}/fx/{slot}")

    def set_fx(
        self,
        index: int,
        slot: int,
        enabled: Optional[bool] = None,
        params: Optional[list] = None,
    ) -> dict:
        body: dict = {}
        if enabled is not None:
            body["enabled"] = enabled
        if params is not None:
            body["params"] = params
        return self._request("POST", f"/state/tracks/{index}/fx/{slot}", body=body)

    def delete_fx(self, index: int, slot: int) -> dict:
        return self._request("DELETE", f"/state/tracks/{index}/fx/{slot}")

    # ---- routing / selection ----------------------------------------------

    def add_send(
        self,
        index: int,
        to_track: int,
        volume_db: Optional[float] = None,
        pan: Optional[float] = None,
    ) -> dict:
        body: dict = {"to_track": to_track}
        if volume_db is not None:
            body["volume_db"] = volume_db
        if pan is not None:
            body["pan"] = pan
        return self._request("POST", f"/state/tracks/{index}/sends", body=body)

    def delete_send(self, index: int, send: int) -> dict:
        return self._request("DELETE", f"/state/tracks/{index}/sends/{send}")

    def set_selection(self, tracks: Any) -> dict:
        return self._request("POST", "/state/selection", body={"tracks": tracks})

    # ---- actions / scripts -------------------------------------------------

    def run_action(self, id: Any, feedback: bool = False) -> dict:
        return self._request("POST", "/execute/action", body={"id": id, "feedback": feedback})

    def run_sequence(self, steps: list, stop_on_failure: bool = True) -> dict:
        return self._request(
            "POST", "/execute/sequence", body={"steps": steps, "stop_on_failure": stop_on_failure}
        )

    def register_script(self, name: str, body: str) -> dict:
        return self._request("POST", "/scripts/register", body={"name": name, "body": body})

    def keyword_search(self, query: str, limit: int = 20) -> list:
        return self._request("GET", "/catalog/search", params={"q": query, "limit": limit}).get(
            "actions", []
        )

    # ---- semantic search ---------------------------------------------------

    def search_actions(self, query: str, limit: int = 20, semantic: bool = True) -> dict:
        """Rank actions for a natural-language query.

        Tries local-embedding semantic ranking; falls back to the server's
        keyword (FTS) search when embeddings aren't available. The response
        always carries a `mode` so the caller knows which path was used.
        """
        if semantic and np is not None:
            try:
                ranked = self._semantic_search(query, limit)
                return {"mode": "semantic", "query": query, "actions": ranked}
            except ReaClawError:
                pass  # fall through to keyword
        return {"mode": "keyword", "query": query, "actions": self.keyword_search(query, limit)}

    def _ollama_embed(self, texts: list) -> "np.ndarray":
        out: list = []
        batch = self.embed_batch
        total = len(texts)
        progress = total > batch  # only narrate the big one-time catalog build
        for i in range(0, total, batch):
            if progress:
                print(
                    f"[reaclaw-mcp] embedding actions {i}/{total}...",
                    file=sys.stderr,
                    flush=True,
                )
            chunk = texts[i : i + batch]
            data = json.dumps({"model": self.embed_model, "input": chunk}).encode()
            req = urllib.request.Request(
                self.ollama_url + "/api/embed", data=data, method="POST"
            )
            req.add_header("Content-Type", "application/json")
            try:
                with urllib.request.urlopen(req, timeout=self.embed_timeout) as resp:
                    payload = json.loads(resp.read())
            except (urllib.error.URLError, OSError) as e:
                raise ReaClawError(f"Ollama embed failed: {e}") from e
            embs = payload.get("embeddings")
            if not embs:
                raise ReaClawError("Ollama returned no embeddings")
            out.extend(embs)
        arr = np.asarray(out, dtype=np.float32)
        norms = np.linalg.norm(arr, axis=1, keepdims=True)
        norms[norms == 0] = 1.0
        return arr / norms

    def _fetch_all_actions(self) -> list:
        actions: list = []
        offset = 0
        while True:
            page = self._request("GET", "/catalog", params={"limit": 1000, "offset": offset})
            rows = page.get("actions", [])
            actions.extend(rows)
            offset += len(rows)
            if not rows or offset >= page.get("total", 0):
                break
            if self.embed_max and len(actions) >= self.embed_max:
                break
        if self.embed_max:
            actions = actions[: self.embed_max]
        return actions

    def _catalog_cache(self) -> tuple:
        """Return (ids, names, categories, normalized_embeddings), building and
        caching the catalog embeddings on first use (keyed by catalog signature)."""
        h = self.health()
        cap = self.embed_max or h.get("catalog_size")
        sig = f"{h.get('catalog_size')}-{h.get('reaper_version','')}-{cap}".replace("/", "_")
        self.cache_dir.mkdir(parents=True, exist_ok=True)
        cache = self.cache_dir / f"catalog-{sig}.npz"
        if cache.exists():
            d = np.load(cache, allow_pickle=True)
            return (list(d["ids"]), list(d["names"]), list(d["categories"]), d["emb"])
        actions = self._fetch_all_actions()
        names = [a.get("name", "") for a in actions]
        ids = [a.get("id") for a in actions]
        cats = [a.get("category", "") for a in actions]
        emb = self._ollama_embed(names)
        np.savez(
            cache,
            ids=np.array(ids, dtype=object),
            names=np.array(names, dtype=object),
            categories=np.array(cats, dtype=object),
            emb=emb,
        )
        return (ids, names, cats, emb)

    def _semantic_search(self, query: str, limit: int) -> list:
        ids, names, cats, emb = self._catalog_cache()
        q = self._ollama_embed([query])[0]
        scores = emb @ q
        top = np.argsort(-scores)[:limit]
        return [
            {
                "id": ids[i],
                "name": names[i],
                "category": cats[i],
                "score": round(float(scores[i]), 4),
            }
            for i in top
        ]

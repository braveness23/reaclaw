"""ReaClaw MCP wrapper — typed tools + semantic action search over the REST API."""

from .client import ReaClawClient, ReaClawError

__all__ = ["ReaClawClient", "ReaClawError"]
__version__ = "0.1.0"

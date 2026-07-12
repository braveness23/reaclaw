#!/usr/bin/env bash
# warmup — one-shot situational awareness for an agent session.
# Prints a compact project digest, seeds the change cursors, and ensures the
# background event tail is running. Target budget: <=1s.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RC="$HERE/rc"
DIR="${REACLAW_AGENT_CACHE:-$HOME/.cache/reaclaw-agent}"
mkdir -p "$DIR"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# Parallel fetch (a plain `var=$(...) &` assigns in a subshell and is lost).
"$RC" GET /state/tracks >"$TMP/tracks" &
"$RC" GET /state >"$TMP/proj" &
"$RC" GET /transport >"$TMP/transport" &
"$RC" GET /state/changes >"$TMP/changes" &
"$RC" GET '/events?since=0&limit=1' >"$TMP/events" &
wait || true

if ! python3 -c "import json,sys; json.load(open(sys.argv[1]))['tracks']" "$TMP/tracks" 2>/dev/null; then
    echo "warmup: ReaClaw unreachable — is REAPER running? (launch recipe: LOCAL.md)" >&2
    exit 1
fi

python3 - "$TMP" "$DIR" <<'PY'
import json, os, sys

tmp, cache = sys.argv[1], sys.argv[2]

def load(name, default=None):
    # A single slow endpoint (main-thread timeout right after a REAPER
    # restart) must degrade the digest, not kill the warmup.
    try:
        j = json.load(open(os.path.join(tmp, name)))
        return default if "error" in j else j
    except (ValueError, OSError):
        return default

proj = load("proj", {"project": {}})
tracks = (load("tracks") or {}).get("tracks", [])
transport = load("transport", {})
changes = load("changes") or {}
cursor = (load("events") or {}).get("cursor", 0)
change_count = changes.get("change_count", -1)

open(os.path.join(cache, "change_count"), "w").write(str(change_count))
open(os.path.join(cache, "cursor"), "w").write(str(cursor))

p = proj.get("project", {})
t = ("playing" if transport.get("playing")
     else "recording" if transport.get("recording") else "stopped")
print(f"ReaClaw up · {p.get('bpm', 0):g} BPM {p.get('time_signature', '')} · {t}"
      f" @ {transport.get('position', 0):.2f}s"
      f" · change_count={change_count} events_cursor={cursor}"
      + ("  [some reads timed out — values may be partial]" if change_count < 0 else ""))
print(f"{len(tracks)} tracks:")
for tr in tracks:
    flags = "".join([
        "M" if tr["muted"] else "-",
        "S" if tr["soloed"] else "-",
        "R" if tr["armed"] else "-",
    ])
    fx = ", ".join(f["name"].split(": ", 1)[-1] for f in tr.get("fx", []) if not f.get("is_inline_eq"))
    print(f"  [{tr['index']}] {flags} {tr['volume_db']:+.1f}dB  {tr['name'] or '(unnamed)'}"
          + (f"  · {fx}" if fx else ""))
PY

# Event tail: start if not already alive.
PIDFILE="$DIR/events-tail.pid"
if [[ -f $PIDFILE ]] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
    echo "event tail: running (pid $(cat "$PIDFILE")) -> $DIR/events.jsonl"
else
    setsid nohup "$HERE/events-tail.sh" >/dev/null 2>&1 &
    echo "event tail: started -> $DIR/events.jsonl"
fi

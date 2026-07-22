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
load = lambda name: json.load(open(os.path.join(tmp, name)))
proj, tracks = load("proj"), load("tracks")["tracks"]
transport, changes = load("transport"), load("changes")
cursor = load("events")["cursor"]

open(os.path.join(cache, "change_count"), "w").write(str(changes["change_count"]))
open(os.path.join(cache, "cursor"), "w").write(str(cursor))

p = proj["project"]
t = "playing" if transport["playing"] else ("recording" if transport["recording"] else "stopped")
print(f"ReaClaw up · {p['bpm']:g} BPM {p.get('time_signature','')} · {t} @ {transport['position']:.2f}s"
      f" · change_count={changes['change_count']} events_cursor={cursor}")
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

# Preflight: known reaper.ini settings that gate blocking modals (issue #119).
# Best-effort only — reaper.ini's location is OS-specific and this checks the
# conventional Linux path (already assumed elsewhere in this repo, e.g. the
# CI smoke test's [verchk] pre-seed — see ReaClaw_TECH_DECISIONS.md). Missing
# file or unreadable = skip silently; never fail warmup over this.
REAPER_INI="$HOME/.config/REAPER/reaper.ini"
if [[ -r "$REAPER_INI" ]]; then
    promptendrec=$(grep -m1 "^promptendrec=" "$REAPER_INI" | cut -d= -f2)
    if [[ "${promptendrec:-1}" != "0" ]]; then
        echo "warmup: WARNING - reaper.ini promptendrec != 0 — a POST /transport/stop" \
             "after recording can hang ~15s on REAPER's \"save/delete new files\" modal." \
             "Fix once via Preferences > Audio > Recording > uncheck \"on stop\" (persists" \
             "as promptendrec=0). See LEARNED.md 2026-07-21." >&2
    fi
    verchk_lastt=$(awk '/^\[verchk\]/{f=1;next} /^\[/{f=0} f && /^lastt=/{print;exit}' "$REAPER_INI")
    if [[ -z "$verchk_lastt" ]]; then
        echo "warmup: WARNING - reaper.ini has no [verchk] lastt — a genuinely fresh config" \
             "can wedge the main thread on REAPER's update-check modal on first launch" \
             "(ReaClaw_TECH_DECISIONS.md §CI E2E smoke test). Pre-seed [verchk]/lastt=<unix-ts>" \
             "before launch if this is a new install." >&2
    fi
fi

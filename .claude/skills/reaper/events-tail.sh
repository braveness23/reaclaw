#!/usr/bin/env bash
# events-tail — keep ReaClaw's SSE event feed mirrored to a local file so an
# agent's "what did the user change?" check is a file read, not an HTTP call.
# Reconnects across the server's 10-minute SSE connection cap and across
# REAPER restarts, resuming from the last seq seen. Run detached; warmup.sh
# manages the lifecycle via the pidfile.
set -uo pipefail

DIR="${REACLAW_AGENT_CACHE:-$HOME/.cache/reaclaw-agent}"
OUT="$DIR/events.jsonl"
PIDFILE="$DIR/events-tail.pid"
RC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/rc"
mkdir -p "$DIR"
echo $$ >"$PIDFILE"

last_seq() {
    [[ -s $OUT ]] || { echo 0; return; }
    tail -n 50 "$OUT" | python3 -c "
import json, sys
seq = 0
for line in sys.stdin:
    try:
        seq = max(seq, json.loads(line).get('seq', 0))
    except (ValueError, KeyError):
        pass
print(seq)"
}

while :; do
    # Trim: keep the tail small so reading it stays cheap.
    if [[ -f $OUT ]] && (($(stat -c%s "$OUT" 2>/dev/null || echo 0) > 1048576)); then
        tail -n 200 "$OUT" >"$OUT.tmp" && mv "$OUT.tmp" "$OUT"
    fi
    since=$(last_seq)
    # Strip "data: " SSE prefixes, drop keepalives/blank lines, append pure JSONL.
    RC_STREAM=1 "$RC" GET "/events/stream?since=$since" \
        | sed -un 's/^data: //p' >>"$OUT"
    # Server closed (10-min cap), REAPER gone, or network error: back off, retry.
    sleep 2
done

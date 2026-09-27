#!/usr/bin/env bash
# Screenshots the chat panel in each of REAPER's dockers, each attached to a
# different edge. Throwaway REAPER, never touches the live rig.
#   run-positions.sh <build-dir-with-dockspike> <reaclaw.so> <screenshot-dir>
set -uo pipefail
BUILD=$1 REACLAW_SO=$2 SHOTS=$3
WORK=${WORK:-$(mktemp -d)}
PORT=${PORT:-9192}
DISP=${DISP:-:97}
KEY=sk_dockspike
H=$WORK/home
RC=$H/.config/REAPER
mkdir -p "$RC/UserPlugins" "$RC/reaclaw" "$SHOTS"
cp "$REACLAW_SO" "$RC/UserPlugins/reaper_reaclaw.so"
cp "$BUILD/reaper_dockspike.so" "$BUILD/dockspike-webhost" "$RC/UserPlugins/"
cat > "$RC/reaclaw/config.json" <<JSON
{ "server": {"host":"127.0.0.1","port":$PORT,"thread_pool_size":2},
  "tls": {"enabled":true,"generate_if_missing":true,"cert_file":"","key_file":""},
  "auth": {"type":"api_key","key":"$KEY"}, "database": {"path":""},
  "logging": {"level":"warn","file":"$WORK/reaclaw.log","format":"text"} }
JSON
# Docker N's edge is dockermodeN. Side/top sizes use the dockheight_* keys.
cat > "$RC/reaper.ini" <<INI
[verchk]
lastt=$(date +%s)
[REAPER]
wnd_x=0
wnd_y=0
wnd_w=1280
wnd_h=800
dockheight=300
dockheight_l=420
dockheight_r=420
dockheight_t=260
dockermode0=0
dockermode1=1
dockermode2=2
dockermode3=3
INI

TS=$(date +%Y%m%d-%H%M%S)
N=0
api() { curl -sk -m 10 -H "Authorization: Bearer $KEY" "$@"; }
act() { api -X POST -H 'Content-Type: application/json' -d "{\"id\":\"$1\"}" "https://127.0.0.1:$PORT/execute/action" >/dev/null; }
shot() {
    N=$((N + 1))
    ffmpeg -loglevel error -y -f x11grab -video_size 1280x800 -i "$DISP" -frames:v 1 \
        "$(printf '%s/%s-pos-%02d-%s.png' "$SHOTS" "$TS" "$N" "$1")"
    echo "shot $N $1"
}

setsid nohup Xvfb $DISP -screen 0 1280x800x24 -ac +extension RANDR > "$WORK/xvfb.log" 2>&1 < /dev/null &
XP=$!
sleep 2
HOME=$H DISPLAY=$DISP XAUTHORITY=/dev/null setsid nohup "$HOME/opt/REAPER/reaper" -nosplash -newinst \
    > "$WORK/reaper.log" 2>&1 < /dev/null &
RP=$!
for _ in $(seq 60); do api "https://127.0.0.1:$PORT/health" >/dev/null 2>&1 && break; sleep 1; done
sleep 2

act _DOCKSPIKE_TOGGLE; sleep 5;  shot docker0
act _DOCKSPIKE_NEXTDOCK; sleep 5; shot docker1
act _DOCKSPIKE_NEXTDOCK; sleep 5; shot docker2
act _DOCKSPIKE_NEXTDOCK; sleep 5; shot docker3

kill $RP 2>/dev/null; sleep 2; kill -9 $RP 2>/dev/null
pkill -f "$RC/UserPlugins/dockspike-webhost" 2>/dev/null
kill $XP 2>/dev/null
grep -i dock "$RC/reaper.ini"

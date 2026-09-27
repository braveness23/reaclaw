#!/usr/bin/env bash
# Proves keyboard/mouse input reaches the reparented web view: click the
# input box, type a message, click Send, screenshot the result. Throwaway
# REAPER, never touches the live rig.
#   run-input.sh <build-dir-with-dockspike> <reaclaw.so> <screenshot-dir>
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
printf '[verchk]\nlastt=%s\n[REAPER]\nwnd_x=0\nwnd_y=0\nwnd_w=1280\nwnd_h=800\ndockheight=280\n' "$(date +%s)" > "$RC/reaper.ini"

TS=$(date +%Y%m%d-%H%M%S)
N=0
api() { curl -sk -m 10 -H "Authorization: Bearer $KEY" "$@"; }
act() { api -X POST -H 'Content-Type: application/json' -d "{\"id\":\"$1\"}" "https://127.0.0.1:$PORT/execute/action" >/dev/null; }
shot() {
    N=$((N + 1))
    ffmpeg -loglevel error -y -f x11grab -video_size 1280x800 -i "$DISP" -frames:v 1 \
        "$(printf '%s/%s-input-%02d-%s.png' "$SHOTS" "$TS" "$N" "$1")"
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

act _DOCKSPIKE_TOGGLE
sleep 5
shot 01-before

# Layout from the docked screenshot: input box sits near the bottom of the
# 1280x280 chat panel (panel top = 800-280 = 520), input row ~38px tall.
IX=400
IY=762
SX=1249
SY=762
DISPLAY=$DISP xdotool mousemove "$IX" "$IY" click 1
sleep 1
DISPLAY=$DISP xdotool type --delay 60 "input reaches the web view"
sleep 1
shot 02-typed
DISPLAY=$DISP xdotool mousemove "$SX" "$SY" click 1
sleep 1
shot 03-after-send

kill $RP 2>/dev/null
sleep 2
kill -9 $RP 2>/dev/null
pkill -f "$RC/UserPlugins/dockspike-webhost" 2>/dev/null
kill $XP 2>/dev/null

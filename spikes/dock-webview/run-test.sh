#!/usr/bin/env bash
# Drives a throwaway REAPER (own HOME, own Xvfb display, own port) through the
# docking cases and screenshots each one. Never touches the live rig.
#   run-test.sh <build-dir-with-dockspike> <reaclaw.so> <screenshot-dir>
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
printf '[verchk]\nlastt=%s\n[REAPER]\nwnd_x=0\nwnd_y=0\nwnd_w=1280\nwnd_h=800\ndockheight=%s\n' "$(date +%s)" "${DOCKHEIGHT:-300}" > "$RC/reaper.ini"

TS=$(date +%Y%m%d-%H%M%S)
N=0
api() { curl -sk -m 10 -H "Authorization: Bearer $KEY" "$@"; }
act() { api -X POST -H 'Content-Type: application/json' -d "{\"id\":\"$1\"}" "https://127.0.0.1:$PORT/execute/action" >/dev/null; }
helpers() { pgrep -c -f "$RC/UserPlugins/dockspike-webhost" || true; }
shot() {
    N=$((N + 1))
    local f
    f=$(printf '%s/%s-%02d-%s.png' "$SHOTS" "$TS" "$N" "$1")
    ffmpeg -loglevel error -y -f x11grab -video_size 1280x800 -i "$DISP" -frames:v 1 "$f"
    printf '%02d %-28s helpers=%s\n' "$N" "$1" "$(helpers)" | tee -a "$SHOTS/$TS-log.txt"
}
start_reaper() {
    HOME=$H DISPLAY=$DISP XAUTHORITY=/dev/null setsid nohup "$HOME/opt/REAPER/reaper" -nosplash -newinst \
        > "$WORK/reaper.log" 2>&1 < /dev/null &
    echo $! > "$WORK/reaper.pid"
    for _ in $(seq 60); do api "https://127.0.0.1:$PORT/health" >/dev/null 2>&1 && break; sleep 1; done
    sleep 2
}

setsid nohup Xvfb $DISP -screen 0 1280x800x24 -ac +extension RANDR > "$WORK/xvfb.log" 2>&1 < /dev/null &
echo $! > "$WORK/xvfb.pid"
sleep 2
start_reaper
shot reaper-started

act _DOCKSPIKE_TOGGLE; sleep 5; shot docked-open
act 40078; sleep 2;            shot mixer-tab-shown
act 40078; sleep 2;            shot back-to-chat-tab
act _DOCKSPIKE_SHRINKMAIN; sleep 2; shot reaper-window-smaller
act _DOCKSPIKE_SHRINKMAIN; sleep 1
act _DOCKSPIKE_DOCK; sleep 5;  shot undocked-floating
F=$(DISPLAY=$DISP xdotool search --name "dock test" 2>/dev/null | head -1)
[ -n "$F" ] && DISPLAY=$DISP xdotool windowsize "$F" 760 300; sleep 2
shot floating-resized
act _DOCKSPIKE_DOCK; sleep 5;  shot redocked
act _DOCKSPIKE_TOGGLE; sleep 2; shot closed
act _DOCKSPIKE_TOGGLE; sleep 5; shot reopened

act 40004; sleep 4              # File: Quit REAPER (panel still open)
kill "$(cat "$WORK/reaper.pid")" 2>/dev/null; sleep 1
shot after-quit
start_reaper; sleep 5
shot restarted-panel-restored

kill "$(cat "$WORK/reaper.pid")" 2>/dev/null; sleep 2
kill -9 "$(cat "$WORK/reaper.pid")" 2>/dev/null
pkill -f "$RC/UserPlugins/dockspike-webhost" 2>/dev/null
kill "$(cat "$WORK/xvfb.pid")" 2>/dev/null
echo "screenshots: $SHOTS ($TS-*)"

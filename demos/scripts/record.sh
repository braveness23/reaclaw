#!/usr/bin/env bash
# record.sh — screen + REAL audio capture for ReaClaw trailers (Pi rig).
#
# Captures the X11 display AND the HDMI audio that REAPER is actually playing,
# muxed into one file. The audio source is the PulseAudio MONITOR of the HDMI
# sink (see pi-hdmi-audio notes) — that's what makes the trailer audible.
#
# Run this FIRST (it blocks, recording), then run show.py in another shell.
# Finalize with:  kill -INT <ffmpeg pid>   (clean MP4 trailer/moov flush).
#
# Usage:  ./record.sh [out.mp4]
set -euo pipefail

OUT="${1:-/tmp/raw.mp4}"
DISP="${DISP:-:0.0}"   # X display to grab (headless: an Xvfb display like :3)
# Audio capture source. Default = the HDMI sink monitor (normal rig, monitor ON).
# HEADLESS / monitor-off: the HDMI sink won't clock samples to its monitor, so
# route REAPER into a PulseAudio null sink and capture THAT monitor instead:
#   pactl load-module module-null-sink sink_name=reatrailer
#   pactl move-sink-input <reaper-id> reatrailer
#   MON=reatrailer.monitor ./record.sh out.mp4
MON="${MON:-alsa_output.platform-107c701400.hdmi.hdmi-stereo.monitor}"
SIZE="1920x1080"
FPS=24

# Record-start epoch -> post.py subtracts this to turn mark() epochs into
# video-time offsets for captions. Keep this in lock-step with the ffmpeg start.
date +%s.%N > /tmp/rec_start.txt

# -draw_mouse 0 hides the cursor so a parked/idle mouse isn't on screen.
exec ffmpeg -y -hide_banner \
    -draw_mouse 0 -f x11grab -framerate "$FPS" -video_size "$SIZE" -i "$DISP" \
    -f pulse -i "$MON" \
    -c:v libx264 -preset veryfast -pix_fmt yuv420p -crf 20 \
    -c:a aac -b:a 192k \
    "$OUT"

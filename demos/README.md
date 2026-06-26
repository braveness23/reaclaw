# ReaClaw — Trailers & Demos

Everything needed to produce an **audible, captioned trailer/demo video** of
ReaClaw driving REAPER, on the aarch64 Raspberry Pi test rig.

The first trailer was built **2026-06-22** (output `~/trailer.mp4`, 1:40 long).
That file and its original scratchpad scripts have since been cleared. The
scripts in `scripts/` are **faithful reconstructions** from detailed build notes
— the logic and every hard-won gotcha are preserved, but expect to tweak a pacing
constant or a path on first run.

---

## TL;DR — how to make one

```bash
cd demos/scripts

# 0. Prereqs: REAPER running with ReaClaw loaded (port 9091), audio backend =
#    PulseAudio (NOT the silent jackd-dummy), HDMI sink active. See "Rig" below.

# 1. Frame the shot: minimize the terminal, maximize REAPER, park the mouse.
xdotool getactivewindow windowminimize                 # hide this terminal
wmctrl -a REAPER && wmctrl -r REAPER -b add,maximized_vert,maximized_horz
xdotool mousemove 1910 1070                             # park cursor in a corner

# 2. Start recording (blocks). Captures screen + REAL HDMI audio together.
./record.sh /tmp/raw.mp4 &
REC=$!

# 3. Run the choreography (builds the groove, plays it, shows off verbs).
python3 show.py

# 4. Stop recording cleanly (flushes the MP4 moov atom).
kill -INT $REC

# 5. Restore the terminal, then burn captions + title/end cards.
python3 post.py        # -> ~/trailer.mp4
```

---

## The rig (preconditions)

See the repo `CLAUDE.md` and the local-test-rig notes for full detail. The bits
that matter for a trailer:

| Thing | Value |
|---|---|
| REAPER | v7.74 linux-aarch64, `~/opt/REAPER` |
| ReaClaw server | `https://127.0.0.1:9091`, Bearer `sk_change_me`, self-signed TLS |
| **Audio backend** | **Prefs → Audio → Device → Audio system = PulseAudio** (the jackd-dummy used for headless API testing is SILENT) |
| Audio sink | HDMI-A-1 → PulseAudio `alsa_output.platform-107c701400.hdmi.hdmi-stereo`, default + unmuted |
| Capture audio from | that sink's **`.monitor`** source |
| GUI tools | `xdotool`, `wmctrl`, `ffmpeg` (with x11grab, libx264, drawtext, aac) |

Launch REAPER **detached** (not `&` in a foreground shell — the harness tears
down the process group and kills it; that's the recurring exit-144).

---

## Headless / monitor-off path (Xvfb + null sink) — PROVEN 2026-06-24

The physical-display path above only works when the HDMI **monitor is powered
on**. With the monitor off (or a truly headless box), the Pi's `vc4` KMS driver
has no output to scan out, so:
- **Video:** `x11grab` of `:0` returns solid black (`YAVG≈16`). VT-switching
  (`chvt`) and DPMS-disable do NOT fix it — there is simply no active CRTC.
- **Audio:** the HDMI PulseAudio sink can't clock samples to its `.monitor`, so
  capture is silent (−91 dB) even though the sink shows `State: RUNNING`.

The fix is to depend on **neither** the physical display nor the HDMI sink:

```bash
# 1. Virtual framebuffer + a window manager (software-rendered, always readable)
setsid bash -c 'Xvfb :3 -screen 0 1920x1080x24 -ac +extension RANDR &'
setsid bash -c 'DISPLAY=:3 xfwm4 &'

# 2. PulseAudio null sink (software clock — runs regardless of hardware)
pactl load-module module-null-sink sink_name=reatrailer \
      sink_properties=device.description=ReaTrailer

# 3. Launch REAPER on the virtual display (detached)
setsid bash -c 'DISPLAY=:3 ~/opt/REAPER/reaper -nosplash &'

# 4. Route REAPER's output into the null sink
SI=$(pactl list sink-inputs | awk '/Sink Input #/{id=$3}
     /application.name = "REAPER"/{print id}' | tr -d '#' | head -1)
pactl move-sink-input "$SI" reatrailer

# 5. Record against the virtual display + null-sink monitor
DISP=:3 MON=reatrailer.monitor ./record.sh /tmp/raw.mp4 &
python3 show.py
kill -INT %1            # finalize the mp4

# 6. Caption + cards
python3 post.py         # -> ~/trailer.mp4
```

`record.sh` honours `DISP` (X display) and `MON` (Pulse source) env vars exactly
for this. `xfwm4` is the only WM installed on the rig; it provides maximize so
the REAPER window fills 1920×1080.

**Two gotchas that cost real time here:**
- **Playback runs off the end.** `GetSetRepeat(1)` behaves like a *toggle* on
  this build, so it can leave repeat OFF and the cursor rolls past the content
  into silence (master meter −150 dB). `lib.set_loop_and_repeat()` now queries
  state and only flips when needed; `lib.play_from_start()` parks the cursor at 0.
  Verify with `GET /state/meters` → `master.peak_db` should be > −150 while playing.
- **ReaClaw logs to the ReaScript console window.** Console output is gated by
  `logging.level`; at `debug`/`info` every API call pops the console over your
  shot. Set `logging.level` to `warn` in `~/.config/REAPER/reaclaw/config.json`
  (restart REAPER) for the recording, then restore.

---

## Files

| File | Role |
|---|---|
| `scripts/smf.py` | Minimal Type-0 Standard MIDI File writer (no deps). The ONLY reliable way to get audible notes into this REAPER build. |
| `scripts/gen_groove.py` | Generates 7 per-track `.mid` stems (kick/bass/hats/snare/perc/pad/lead). |
| `scripts/lib.py` | ReaClaw REST client + REAPER choreography helpers (verified endpoints). |
| `scripts/show.py` | The choreography: build groove → loop → unmute layer-by-layer → tour verbs. Logs caption marks. |
| `scripts/record.sh` | ffmpeg screen + HDMI-monitor audio capture; writes record-start epoch. |
| `scripts/post.py` | Burns lower-third captions (aligned via marks) + title/end cards; concats. |

---

## The hard-won knowledge (READ THIS before changing the scripts)

### Making REAPER actually make sound through ReaClaw
- `reaper.CreateNewMIDIItemInProject` is **nil** on this build.
- Inserting notes into a take built from `PCM_Source_CreateFromType("MIDI")`
  **fails silently**: `MIDI_GetPPQPosFromProjTime` returns `-1` (no valid PPQ
  map), `MIDI_InsertNote` returns false, notecount stays 0 → output is −91 dB.
- **Working route:** write a real **Type-0 `.mid`** file (see `smf.py`) and
  `reaper.InsertMedia(path, 0)` onto the selected track — after
  `SetOnlyTrackSelected` + `SetEditCurPos(0)`. `lib.insert_media()` does this.
- Confirmed audible: 1 ReaSynth track playing a `.mid` = −16 dB; the full
  7-track groove = −5 dB at the master.

### Instruments
- Add ReaSynth via the **structured endpoint** `POST /state/tracks/{i}/fx
  {name:"ReaSynth"}` (resolves to "VSTi: ReaSynth (Cockos)"). The Lua
  `TrackFX_AddByName("ReaSynth", …)` route did **not** add it.
- ReaSynth is the same sine for every instance — differentiate "instruments" by
  **pitch + note length** (low+short = kick/bass, high+short = hats).
- **Gain-stage every track down** (−5..−17 dB) via `volume_db` or the master
  clips at 0 dB.

### Playback / the build-up effect
- Loop: `GetSet_LoopTimeRange(true,false,0,len,false)` + `GetSetRepeat(1)`
  (`lib.set_loop_and_repeat`).
- Build-up: create all tracks **muted**, `act(1007)` to play, then unmute
  layer-by-layer on bar boundaries (`POST /state/tracks/{i} {muted:false}`).

### Automation
- The envelope must be **active first**: `act(40406)` (Volume) / `act(40407)`
  (Pan) on the *selected* track, THEN
  `POST /state/tracks/{i}/automation {envelope:"Volume", points:[…]}`.
  Otherwise you get **400 "Envelope not found"**.

### Camera moves (action ids)
- `1012` = zoom **IN** horizontal, `1011` = zoom **OUT** horizontal
  (the labels are swapped from intuition — don't trust the names).
- `40031` = zoom to time selection. `40111`/`40112` = taller/shorter tracks.
- Piano roll: select one item + `act(40153)`, then
  `wmctrl -r "MIDI take" -b add,maximized…`; close with `wmctrl -c "MIDI take"`.
- Tempo map: `POST /state/tempo {time, bpm}`.

### Capturing the audio
- `ffmpeg -f pulse -i alsa_output.platform-107c701400.hdmi.hdmi-stereo.monitor`
  muxed with `x11grab`. Measure level with `-af volumedetect`.

### Caption alignment
- `record.sh` writes the record-start epoch to `/tmp/rec_start.txt`.
- `show.py` logs `epoch <TAB> label` per step to `/tmp/marks.txt` via `lib.mark()`.
- `post.py` subtracts record-start (and any head TRIM) to get video-time offsets
  for `drawtext … enable='between(t,S,E)'` lower-thirds.
- Title/end cards are lavfi `color=` video + `anullsrc` audio, encoded with
  **identical params** to the body so the final concat can use `-c copy`.

---

## Verified ReaClaw endpoints used here

| Endpoint | Body | Notes |
|---|---|---|
| `POST /execute/action` | `{"id": <int\|str>}` | run any REAPER command / named action |
| `POST /scripts/register` | `{"name", "script"}` → `{"action_id"}` | register one-shot Lua |
| `DELETE /scripts/{action_id}` | — | unregister it |
| `POST /state/tracks` | `{"create":[{name,volume_db,muted}]}` | returns `created[].index` |
| `POST /state/tracks/{i}` | `{volume_db,pan,muted,name,…}` | per-track update |
| `POST /state/tracks/{i}/fx` | `{"name":"ReaSynth"}` | add instrument/FX |
| `POST /state/tracks/{i}/automation` | `{"envelope","points":[{time,value}]}` | arm envelope first |
| `POST /state/tempo` | `{"time","bpm"}` | tempo/timesig marker |

(Confirmed against `src/handlers/` + `src/server/router.cpp` at reconstruction
time. If an endpoint changes, update `lib.py` and this table together.)

---

## Known weak spots in the reconstruction

- **Pacing** (`SECONDS_PER_BAR`, `CAPTION_HOLD`, sleeps in `show.py`) is a guess
  tuned for 120 BPM / 4-bar loops. Watch the first cut and adjust.
- **Window-title strings** for `wmctrl` (piano-roll "MIDI take") vary by REAPER
  build/locale — verify with `wmctrl -l` before scripting window moves.
- **Font path** in `post.py` assumes DejaVu; change `FONT` if missing.
- `post.py`'s `-c copy` concat needs all three clips at the exact same
  resolution/fps/codec — keep the `VENC`/`AENC`/`SIZE` constants in sync with
  `record.sh`.

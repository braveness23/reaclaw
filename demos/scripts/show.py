#!/usr/bin/env python3
"""show.py — the trailer choreography. Drives REAPER entirely through ReaClaw.

Run this AFTER record.sh has started capturing (so every move is on tape). It:
  1. builds a 7-track groove (each track muted, ReaSynth instrument, gain-staged)
  2. loops playback and unmutes layer-by-layer on bar boundaries (the build-up)
  3. shows off a few API verbs on camera: tempo change, volume automation,
     zoom/track-height camera moves, piano-roll open
Each step logs its epoch via lib.mark() so post.py can drop a caption on it.

The wall-clock pacing here is deliberate: the camera needs time to read each move.
Tune SECONDS_PER_BAR to your tempo (120 BPM, 4/4 -> 2.0 s/bar).
"""
import time
import lib
from gen_groove import main as gen_groove, BPM, BARS

SECONDS_PER_BAR = 4 * 60.0 / BPM       # 4 beats/bar at BPM
LOOP_LEN = BARS * SECONDS_PER_BAR


def beat(n=1):
    time.sleep(n * 60.0 / BPM)


def setup_tracks(manifest):
    """Create one muted track per groove stem, add ReaSynth, gain-stage, insert .mid."""
    indices = {}
    for i, (name, path, gain) in enumerate(manifest):
        idx = lib.add_track(name=name, volume_db=gain, muted=True)
        lib.add_fx(idx, "ReaSynth")
        lib.insert_media(idx, path)
        indices[name] = idx
        lib.mark(f"+ {name}  ({gain} dB)")
    return indices


def build_up(manifest, indices):
    """Start playback looping, then unmute one stem per bar."""
    lib.set_loop_and_repeat(LOOP_LEN)
    lib.mark("loop set / play")
    lib.play_from_start()

    for name, _path, _gain in manifest:
        lib.set_track(indices[name], muted=False)
        lib.mark(f"unmute {name}")
        time.sleep(SECONDS_PER_BAR)    # one stem per bar


def show_verbs(indices):
    """A short tour of other ReaClaw verbs, on camera."""
    # tempo lift
    lib.set_tempo(0.0, BPM)
    lib.set_tempo(LOOP_LEN, BPM + 12)
    lib.mark("tempo automation 120 -> 132")
    time.sleep(SECONDS_PER_BAR)

    # volume automation on the Lead (must arm the envelope first)
    lead = indices["Lead"]
    lib.set_track(lead)                                   # ensure it's the selected track
    lib.run_lua(f'reaper.SetOnlyTrackSelected(reaper.GetTrack(0, {lead}))')
    lib.act(lib.ARM_VOL_ENV)                              # activate Volume envelope
    lib.write_automation(lead, "Volume", [
        {"time": 0.0,            "value": 0.0},
        {"time": LOOP_LEN / 2,   "value": 1.0},
        {"time": LOOP_LEN,       "value": 0.5},
    ])
    lib.mark("volume automation drawn (Lead)")
    time.sleep(SECONDS_PER_BAR)

    # camera moves
    lib.act(lib.TRACKS_TALLER); lib.act(lib.TRACKS_TALLER)
    lib.mark("tracks taller")
    time.sleep(1.5)
    lib.act(lib.ZOOM_IN_HORIZ); lib.act(lib.ZOOM_IN_HORIZ)
    lib.mark("zoom in")
    time.sleep(1.5)
    lib.act(lib.ZOOM_TO_TIMESEL)
    lib.mark("zoom to loop")
    time.sleep(2.0)


def main():
    lib.reset_marks()
    lib.act(lib.STOP)
    lib.clear_project()           # clean slate
    lib.mark("START")
    manifest = gen_groove()
    indices = setup_tracks(manifest)
    build_up(manifest, indices)
    show_verbs(indices)
    time.sleep(2 * SECONDS_PER_BAR)    # let the full groove ride before we cut
    lib.act(lib.STOP)
    lib.mark("END")
    print("choreography done; marks in /tmp/marks.txt")


if __name__ == "__main__":
    main()

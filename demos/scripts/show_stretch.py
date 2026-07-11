#!/usr/bin/env python3
"""show_stretch.py — the STRETCH-MARKER AUDIO QUANTIZE trailer. A "live" drum
take (real one-shot kick/snare samples from a Sony ACID sample library,
Groove Spectrum, arranged with deliberately loose timing against a tight
hi-hat reference) gets corrected onto the grid using REAPER's stretch
markers — SetTakeStretchMarker + the native "snap to grid" action — with no
pitch shift and no re-recording.

Two phases so the recording has no dead setup time:
    python3 show_stretch.py prepare   # build the take (NOT recorded)
    python3 show_stretch.py perform   # the recorded performance

Marks (lib.mark) drop caption cues for post_stretch.py.
"""
import os
import sys
import time

import lib

BPM = 96
PACE = float(os.environ.get("PACE", "1.0"))
SPB = 60.0 / BPM
BARS = int(os.environ.get("STRETCH_BARS", "4"))
BEATS = BARS * 4
LOOP_LEN = BEATS * SPB                # 10.0 s @ 96 BPM / 4 bars

SAMPLES = "/tmp/stretchdemo"
KICK = os.path.join(SAMPLES, "kick.wav")
SNARE = os.path.join(SAMPLES, "snare.wav")
HIHAT = os.path.join(SAMPLES, "hihat.wav")

HATS, DRUMS = range(2)
WHOLE = (-0.4, LOOP_LEN + 0.4)

# One "human" onset per beat (kick on evens, snare on odds), off the grid by
# a plausible amount (seconds, +late/-early). Beat 0 is left dead-on so the
# glued take's own start position stays at t=0 (see lib.glue_track_items).
JITTER = {
    0: 0.000, 1: 0.055, 2: -0.045, 3: 0.035, 4: 0.070, 5: -0.050,
    6: -0.030, 7: 0.060, 8: 0.040, 9: -0.065, 10: -0.050, 11: 0.045,
    12: 0.080, 13: -0.040, 14: -0.060, 15: 0.050,
}
HERO_BEATS = (1, 2)                   # snare (late) + kick (early) — placed live, on camera


def hit_time(beat):
    """The TRUE (jittered) onset for this beat — where the audio actually
    sits before correction."""
    return beat * SPB + JITTER.get(beat, 0.0)


def hold(sec):
    time.sleep(sec * PACE)


def anim(a1, b1, dur):
    lib.animate_view_to(a1, b1, dur * PACE)


# ---- PREPARE (not recorded) ------------------------------------------------

def prepare():
    lib.act(lib.STOP)
    lib.clear_project()
    lib.run_lua('for i = reaper.CountTempoTimeSigMarkers(0) - 1, 0, -1 do '
                'reaper.DeleteTempoTimeSigMarker(0, i) end\n'
                f'reaper.SetCurrentBPM(0, {BPM}, false)', name="reaclaw_flat_tempo")
    lib.disable_follow()
    lib.set_project_grid(0.25)
    lib._ensure_view_applier()
    lib.set_view(*WHOLE)

    hats_i = lib.add_track(name="Hats (reference)", volume_db=-11, muted=True)
    drums_i = lib.add_track(name="Kick+Snare (uncorrected take)", volume_db=-6, muted=True)
    assert (hats_i, drums_i) == (HATS, DRUMS), f"track order drifted: {(hats_i, drums_i)}"

    # Tight 8th-note hats -- the fixed reference the drift is heard against.
    hat_specs = [{"track": HATS, "position": i * 0.5 * SPB, "file": HIHAT}
                 for i in range(BEATS * 2)]
    lib.create_items(hat_specs)

    # Loose kick/snare -- real one-shots, each nudged off its beat.
    drum_specs = []
    for b in range(BEATS):
        f = KICK if b % 2 == 0 else SNARE
        drum_specs.append({"track": DRUMS, "position": hit_time(b), "file": f})
    lib.create_items(drum_specs)
    lib.glue_track_items(DRUMS)       # -> one continuous "recorded" take

    lib.set_loop_and_repeat(LOOP_LEN)
    lib.act(lib.TRACKS_TALLER)
    lib.act(lib.TRACKS_TALLER)
    lib.run_lua('reaper.SetEditCurPos(0, false, false)', name="reaclaw_curzero")
    lib.set_view(*WHOLE)
    print(f"prepared: {BARS} bars @ {BPM} BPM, {BEATS} hits, loop {LOOP_LEN:.1f}s")


# ---- PERFORM (recorded) -----------------------------------------------------

def establish():
    lib.run_lua('reaper.SetEditCurPos(0, false, false)', name="reaclaw_curzero")
    lib.set_view(*WHOLE)
    lib.mark("ReaClaw -- Stretch Markers: fixing a drummer's timing")
    lib.play_from_start()
    hold(2.0)
    lib.set_track(HATS, muted=False)
    lib.mark("POST /state/tracks/{i}  muted:false -- tight 8th-note hats, the reference")
    hold(LOOP_LEN * 1.0)
    lib.set_track(DRUMS, muted=False)
    lib.mark("real one-shot kick + snare (Groove Spectrum) -- listen to the drift")
    hold(LOOP_LEN * 1.6)


def reveal_the_problem():
    anim(0.0, 2 * SPB + 0.3, 1.5)
    lib.mark("zoomed in -- the kick rushes, the snare drags")
    hold(2.5)
    lib.act(lib.STOP)


def mark_the_hits():
    lib.select_track_item(DRUMS, 0)
    lib.mark("40836 -- Item navigation: move cursor to nearest transient (live)")
    for b in HERO_BEATS:
        found = lib.nearest_transient(b * SPB - 0.15)
        lib.mark(f"real onset at {found:.3f}s -- grid says {b * SPB:.3f}s")
        hold(1.4)
        lib.add_stretch_marker_at_cursor()
        lib.mark("41842 -- Item: Add stretch marker at cursor")
        hold(1.1)

    remaining = [hit_time(b) for b in range(BEATS) if b not in HERO_BEATS]
    lib.set_stretch_markers(DRUMS, remaining)
    lib.mark(f"...and the other {len(remaining)}, in one call -- SetTakeStretchMarker x{len(remaining)}")
    hold(2.0)


def snap_and_compare():
    lib.select_track_item(DRUMS, 0)
    lib.mark("41846 -- Item: Snap stretch markers to grid -- one action, every hit")
    lib.snap_stretch_markers()
    hold(2.0)
    anim(0.0, 2 * SPB + 0.3, 1.2)
    lib.mark("same zoom -- locked to the grid, no pitch shift")
    hold(2.5)

    anim(*WHOLE, 1.5)
    lib.play_from_start()
    lib.set_track(HATS, muted=False)
    lib.set_track(DRUMS, muted=False)
    lib.mark("play it back -- corrected")
    hold(LOOP_LEN * 2.2)


def finale():
    lib.mark("no re-recording. no pitch shift. one drum take, fixed in place.")
    hold(3.0)


def perform():
    lib.reset_marks()
    lib.mark("START")
    establish()
    reveal_the_problem()
    mark_the_hits()
    snap_and_compare()
    finale()
    lib.act(lib.STOP)
    lib.mark("END")
    print("perform done; marks in /tmp/marks.txt")


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "perform"
    if mode == "prepare":
        prepare()
    elif mode == "perform":
        perform()
    elif mode == "all":
        prepare(); time.sleep(1); perform()
    else:
        sys.exit(f"unknown mode {mode!r} (prepare|perform|all)")


if __name__ == "__main__":
    main()

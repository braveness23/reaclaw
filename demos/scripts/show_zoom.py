#!/usr/bin/env python3
"""show_zoom.py — the ZOOM trailer choreography. Zoom is the theme: the whole
trailer is one long flight of REAPER's camera, every move driven by ReaClaw.

The camera (the arrange's visible time window) is animated frame-by-frame from
Python via lib.animate_view (GetSet_ArrangeView2). See lib.py for why a blocking
Lua loop can't do this on the SWELL build.

Two phases so the recording has no dead setup time:
    python3 show_zoom.py prepare   # build the 16-bar groove (NOT recorded)
    python3 show_zoom.py perform   # the recorded performance

`perform` assumes the deterministic track order from `prepare` (clean project,
tracks created Kick..Lead -> indices 0..6).

Marks (lib.mark) drop caption cues for post_zoom.py.
"""
import os
import subprocess
import sys
import time

os.environ.setdefault("GROOVE_BARS", "16")     # long canvas to fly across
import lib
from gen_groove import main as gen_groove, BPM, BARS

PACE = float(os.environ.get("PACE", "1.35"))    # global tempo of the choreography
SPB = 4 * 60.0 / BPM                            # seconds per bar (2.0 @ 120)
LOOP_LEN = BARS * SPB                           # 32.0 s
DISP = os.environ.get("REA_DISPLAY", ":3")

ORDER = ["Kick", "Bass", "Hats", "Snare", "Perc", "Pad", "Lead"]
GAIN = {"Kick": -8, "Bass": -7, "Hats": -16, "Snare": -10,
        "Perc": -15, "Pad": -13, "Lead": -11}

WHOLE = (-0.4, LOOP_LEN + 0.4)                  # establishing shot: the lot


def bar(n):
    return n * SPB


def hold(sec):
    time.sleep(sec * PACE)


def anim(a1, b1, dur):
    lib.animate_view_to(a1, b1, dur * PACE)


# ---- window helpers (MIDI editor) --------------------------------------------

def _wmctrl_list():
    try:
        out = subprocess.check_output(["wmctrl", "-l"], env={**os.environ, "DISPLAY": DISP})
        return out.decode().splitlines()
    except Exception:
        return []


def open_midi_editor():
    """Open the selected item in the MIDI editor and, if it floats as its own
    window, maximize it so the piano roll fills the screen. Returns the window id
    (or None if it docked into the main window)."""
    before = {ln.split()[0] for ln in _wmctrl_list()}
    lib.act(lib.OPEN_MIDI_EDIT)            # 40153
    time.sleep(1.0)
    after = _wmctrl_list()
    for ln in after:
        wid = ln.split()[0]
        if wid in before:
            continue
        low = ln.lower()
        if "reaper" in low and ("midi" in low or "take" in low or ".mid" in low or "edit" in low):
            subprocess.run(["wmctrl", "-i", "-r", wid, "-b",
                            "add,maximized_vert,maximized_horz"],
                           env={**os.environ, "DISPLAY": DISP})
            return wid
    return None


def close_window(wid):
    if wid:
        subprocess.run(["wmctrl", "-i", "-c", wid],
                       env={**os.environ, "DISPLAY": DISP})


def close_midi_editors():
    """Close every floating MIDI editor window. CRITICAL: an open MIDI editor
    breaks GetSet_ArrangeView2 arrange control, so every set_view after the
    piano-roll segment (the whole finale) depends on this actually succeeding.
    Retries because the editor can take a beat to tear down."""
    for _ in range(4):
        wins = [ln.split()[0] for ln in _wmctrl_list()
                if any(k in ln.lower()
                       for k in ("midi take", "midi editor", ".mid"))]
        if not wins:
            return
        for wid in wins:
            subprocess.run(["wmctrl", "-i", "-c", wid],
                           env={**os.environ, "DISPLAY": DISP})
        time.sleep(0.6)


# ---- small Lua gestures -------------------------------------------------------

def select_item(track_index):
    """Select ONLY the first media item on the given track (and that track)."""
    lib.run_lua(
        'reaper.Main_OnCommand(40289, 0)\n'                       # unselect all items
        f'local tr = reaper.GetTrack(0, {track_index})\n'
        'if tr then\n'
        '  local it = reaper.GetTrackMediaItem(tr, 0)\n'
        '  if it then reaper.SetMediaItemSelected(it, true) end\n'
        '  reaper.SetOnlyTrackSelected(tr)\n'
        '  reaper.UpdateArrange()\n'
        'end\n', name="reaclaw_selitem")


def select_track(track_index):
    lib.run_lua(f'reaper.SetOnlyTrackSelected(reaper.GetTrack(0, {track_index}))',
                name="reaclaw_seltrk")


def select_all_tracks():
    lib.run_lua('reaper.Main_OnCommand(40296, 0)', name="reaclaw_selall")


def fit_all_tracks_vertically():
    """Make all 7 tracks fit the arrange height. The toggle actions (40110 min /
    42697 default) do NOT undo an SWS *vertical zoom*, so the reliable reset is
    to select every track and run SWS 'vertical zoom to selected tracks'."""
    select_all_tracks()
    lib.act(53787)


def time_selection(a, b):
    lib.run_lua(f'reaper.GetSet_LoopTimeRange(true, false, {a}, {b}, false)',
                name="reaclaw_timesel")


def set_mixer(visible):
    want = 1 if visible else 0
    lib.run_lua(f'if reaper.GetToggleCommandState(40078) ~= {want} then '
                'reaper.Main_OnCommand(40078, 0) end', name="reaclaw_mixer")


# ---- PREPARE (not recorded) ---------------------------------------------------

def prepare():
    lib.act(lib.STOP)
    lib.clear_project()
    # clear_project only deletes tracks — wipe the tempo map too so the whole
    # project is a flat 120 BPM and bar(n) == n*2.0s holds everywhere.
    # BOUNDED for-loop (delete high index first): a `while count>0` loop can spin
    # forever if a marker won't delete, freezing REAPER's main thread.
    lib.run_lua('for i = reaper.CountTempoTimeSigMarkers(0) - 1, 0, -1 do '
                'reaper.DeleteTempoTimeSigMarker(0, i) end\n'
                'reaper.SetCurrentBPM(0, 120, false)', name="reaclaw_flat_tempo")
    lib.disable_follow()                  # so view commands aren't yanked
    set_mixer(False)                      # full-height arrange for big zooms
    lib._ensure_view_applier()
    lib.set_view(*WHOLE)

    manifest = gen_groove()               # writes 7 .mid stems
    by_name = {name: (path, gain) for name, path, gain in manifest}
    for i, name in enumerate(ORDER):
        path, gain = by_name[name]
        idx = lib.add_track(name=name, volume_db=gain, muted=True)
        lib.add_fx(idx, "ReaSynth")
        lib.insert_media(idx, path)

    lib.set_loop_and_repeat(LOOP_LEN)
    close_midi_editors()                  # clear any stray editor from a prior run
    fit_all_tracks_vertically()           # all 7 tracks fit the arrange height
    # park the cursor at 0 first, else a cursor at content-end (32 s) auto-scrolls
    # the WHOLE shot to the tail (the same gotcha establish_and_build handles).
    lib.run_lua('reaper.SetEditCurPos(0, false, false)', name="reaclaw_curzero")
    lib.set_view(*WHOLE)
    print(f"prepared: {BARS} bars, {len(ORDER)} tracks, loop {LOOP_LEN:.0f}s")


# ---- PERFORM (recorded) -------------------------------------------------------

def establish_and_build():
    # Park the edit cursor at 0 first: a cursor parked at content-end (32 s)
    # makes REAPER auto-scroll the arrange to keep it visible, which fights the
    # establishing WHOLE shot. With the cursor at 0 the WHOLE view holds.
    lib.run_lua('reaper.SetEditCurPos(0, false, false)', name="reaclaw_curzero")
    lib.set_view(*WHOLE)
    lib.mark("ReaClaw — drive REAPER's camera with one API")
    lib.play_from_start()
    hold(3.5)
    for i, name in enumerate(ORDER):
        lib.set_track(i, muted=False)
        if i == 0:
            lib.mark("POST /state/tracks/{i}  muted:false — building the groove")
        hold(1.25)
    lib.mark("the full mix — now let's fly the camera")
    hold(3.0)


def descent():
    lib.mark("GetSet_ArrangeView2 — the whole view is one call")
    anim(bar(6), bar(10), 3.6); hold(1.2)
    lib.mark("from sixteen bars...")
    anim(bar(8), bar(9), 3.0); hold(1.3)
    lib.mark("...to one bar...")
    anim(bar(8), bar(8) + 0.5, 2.6); hold(1.4)
    lib.mark("...to a single beat...")
    anim(bar(8), bar(8) + 0.16, 2.2); hold(2.6)
    lib.mark("...to one note. One call.")
    hold(1.0)


def camera_pan():
    # A dolly move: slide a fixed-width window across the whole timeline. Both
    # endpoints shift by the same amount, so the zoom level stays constant while
    # the camera glides — a distinct gesture from zooming in/out.
    lib.mark("pan the camera — same zoom, gliding across the timeline")
    w = bar(4)
    lib.set_view(0, w); hold(0.5)
    anim(LOOP_LEN - w, LOOP_LEN, 4.2); hold(0.5)
    anim(0, w, 3.4); hold(0.6)


def punch_zoom():
    wide = (bar(4), bar(12))
    tight = (bar(8), bar(9))
    anim(*wide, 1.6)
    lib.mark("zoom on the downbeat — 120 BPM, fully scripted")
    # rhythmic snap punches, one per beat. Locked to the musical beat (0.5 s at
    # 120 BPM) and NOT scaled by PACE, so the snaps stay on the tempo grid.
    beat = SPB / 4.0
    for k in range(8):
        lib.set_view(*(tight if k % 2 == 0 else wide))
        time.sleep(beat)
    # then a few quick smooth dives back to the groove
    for _ in range(3):
        anim(bar(8), bar(8) + 0.5, 1.0)
        hold(0.3)
        anim(*wide, 1.0)
        hold(0.3)
    hold(1.0)


def stem_tour():
    # Rapid-fire zoom-to-item through every stem: the camera frames each track's
    # clip in turn, naming it. Shows zoom as a tool for *navigation*.
    anim(bar(2), bar(14), 1.0); hold(0.3)
    lib.mark("zoom to each stem in turn — navigation by camera")
    for name in ORDER:
        select_item(ORDER.index(name))
        try:
            lib.act(53796)               # SWS: zoom to selected items
        except Exception:
            pass
        lib.mark(name)
        hold(1.4)
    lib.act(40289)                       # unselect all items
    anim(bar(4), bar(12), 1.0); hold(0.4)


def vertical_axis():
    anim(bar(6), bar(14), 1.4)            # give tracks some width
    lib.mark("the OTHER axis — vertical zoom")
    fit_all_tracks_vertically()
    hold(1.4)
    # the payoff: fill the whole screen with ONE track, hop to another, pull
    # back to the full kit. Uses only SWS vertical-zoom-to-selected + fit-all,
    # which are deterministic and reversible (no sticky toggle state).
    select_track(ORDER.index("Lead"))
    lib.act(53787)
    lib.mark("fill the screen with ONE track")
    hold(3.0)
    select_track(ORDER.index("Bass"))
    lib.act(53787)
    lib.mark("...or zoom straight to another")
    hold(2.6)
    fit_all_tracks_vertically()
    lib.mark("...back to the whole kit")
    hold(1.6)


def semantic_zoom():
    lib.mark("semantic zoom — frame exactly what matters")
    anim(bar(2), bar(10), 1.2); hold(0.6)
    select_item(ORDER.index("Lead"))
    try:
        lib.act(53796)                    # SWS: zoom to selected items
        lib.mark("zoom to selected items")
        hold(3.0)
    except Exception:
        pass
    select_item(ORDER.index("Bass"))
    try:
        lib.act(53796); hold(2.6)
    except Exception:
        pass
    time_selection(bar(4), bar(8))
    lib.act(lib.ZOOM_TO_TIMESEL)          # 40031
    lib.mark("zoom to time selection")
    hold(3.0)
    time_selection(0, LOOP_LEN)           # restore full loop region
    hold(0.4)


def piano_roll():
    anim(bar(8), bar(12), 1.2); hold(0.4)
    select_item(ORDER.index("Lead"))
    lib.mark("all the way in — the piano roll")
    open_midi_editor()
    hold(5.5)
    close_midi_editors()                  # MUST fully close: an open editor
    time.sleep(0.6)                       # breaks set_view for the finale
    lib.set_view(bar(6), bar(14))
    hold(1.0)


def finale():
    # reset BOTH axes: semantic_zoom/piano_roll left the arrange vertically zoomed
    # to one track, so fit all seven back before the big zoom-out.
    fit_all_tracks_vertically()
    lib.mark("...and rocket back out")
    anim(*WHOLE, 3.8)
    lib.mark("one API. any altitude.")
    hold(3.0)
    # one last breath: punch in and out
    anim(bar(7), bar(9), 1.4); hold(0.6)
    anim(*WHOLE, 1.6)
    hold(2.6)


def perform():
    lib.reset_marks()
    lib.mark("START")
    establish_and_build()
    descent()
    camera_pan()
    punch_zoom()
    stem_tour()
    vertical_axis()
    semantic_zoom()
    piano_roll()
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

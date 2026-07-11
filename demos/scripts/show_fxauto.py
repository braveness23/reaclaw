#!/usr/bin/env python3
"""show_fxauto.py — the FX & AUTOMATION trailer. Four MIDI tracks, four
different stock effects, and every automation flavor ReaClaw exposes: preset
loading, an FX-parameter envelope (the compressor's own Threshold), a
built-in track envelope (Pan), and a MIDI-editing beat — all driven over
HTTP.

Two phases so the recording has no dead setup time:
    python3 show_fxauto.py prepare   # build the four tracks (NOT recorded)
    python3 show_fxauto.py perform   # the recorded performance

`perform` assumes the deterministic track/FX order from `prepare`: tracks
Drums..Lead -> indices 0..3, and per the documented "agent_slot" invariant
(every new track gets a disabled inline ReaEQ at raw slot 0), ReaSynth always
lands at slot 1 and the track's one effect always lands at slot 2.

Marks (lib.mark) drop caption cues for post_fxauto.py.
"""
import os
import subprocess
import sys
import time

os.environ.setdefault("GROOVE_BARS", "8")
import lib
from gen_groove import kick, snare, hats, bass as gen_bass, pad as gen_pad, lead as gen_lead
from smf import write_smf

BPM = 120
PACE = float(os.environ.get("PACE", "1.0"))
SPB = 60.0 / BPM
SPbar = 4 * SPB                       # 2.0 s @ 120 BPM
BARS = int(os.environ.get("GROOVE_BARS", "8"))
LOOP_LEN = BARS * SPbar               # 16.0 s
DISP = os.environ.get("REA_DISPLAY", ":3")

REASYNTH_SLOT = 1                     # slot 0 = auto inline-disabled ReaEQ
FX_SLOT = 2                           # the track's one real effect

TRACKS = [
    # name,    effect,     gain_db, note-generator
    ("Drums",  "ReaComp",  -8,  None),   # combined kick+snare+hats
    ("Bass",   "ReaXComp", -9,  gen_bass),
    ("Pad",    "ReaDelay", -13, gen_pad),
    ("Lead",   "ReaEQ",    -11, gen_lead),
]
DRUMS, BASS, PAD, LEAD = range(4)

WHOLE = (-0.4, LOOP_LEN + 0.4)


def hold(sec):
    time.sleep(sec * PACE)


def anim(a1, b1, dur):
    lib.animate_view_to(a1, b1, dur * PACE)


# ---- window helpers (MIDI editor / floating FX), same technique as show_zoom.py --

def _wmctrl_list():
    try:
        out = subprocess.check_output(["wmctrl", "-l"], env={**os.environ, "DISPLAY": DISP})
        return out.decode().splitlines()
    except Exception:
        return []


def open_midi_editor():
    before = {ln.split()[0] for ln in _wmctrl_list()}
    lib.act(lib.OPEN_MIDI_EDIT)
    time.sleep(1.0)
    for ln in _wmctrl_list():
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


def close_midi_editors():
    """An open MIDI editor breaks GetSet_ArrangeView2 arrange control (see
    reaclaw-zoom-trailer-gotchas memory) — must fully close before any later
    set_view/anim call, with retries since teardown takes a beat."""
    for _ in range(4):
        wins = [ln.split()[0] for ln in _wmctrl_list()
                if any(k in ln.lower() for k in ("midi take", "midi editor", ".mid"))]
        if not wins:
            return
        for wid in wins:
            subprocess.run(["wmctrl", "-i", "-c", wid], env={**os.environ, "DISPLAY": DISP})
        time.sleep(0.6)


def close_fx_windows():
    """Close any floating plugin GUIs (ReaComp/ReaEQ/etc.) left open from a
    prior segment, so they don't stack up on screen."""
    for _ in range(3):
        wins = [ln.split()[0] for ln in _wmctrl_list()
                if any(k in ln for k in ("ReaComp", "ReaEQ", "ReaXcomp", "ReaXComp", "ReaDelay"))]
        if not wins:
            return
        for wid in wins:
            subprocess.run(["wmctrl", "-i", "-c", wid], env={**os.environ, "DISPLAY": DISP})
        time.sleep(0.4)


def select_track(i):
    lib.run_lua(f'reaper.SetOnlyTrackSelected(reaper.GetTrack(0, {i}))', name="reaclaw_seltrk")


def select_item(track_i):
    lib.run_lua(
        'reaper.Main_OnCommand(40289, 0)\n'
        f'local tr = reaper.GetTrack(0, {track_i})\n'
        'if tr then\n'
        '  local it = reaper.GetTrackMediaItem(tr, 0)\n'
        '  if it then reaper.SetMediaItemSelected(it, true) end\n'
        '  reaper.SetOnlyTrackSelected(tr)\n'
        '  reaper.UpdateArrange()\n'
        'end\n', name="reaclaw_selitem")


def select_all_tracks():
    lib.run_lua('reaper.Main_OnCommand(40296, 0)', name="reaclaw_selall")


def fit_all_tracks_vertically():
    select_all_tracks()
    lib.act(53787)   # SWS: vertical zoom to selected tracks


def set_mixer(visible):
    want = 1 if visible else 0
    lib.run_lua(f'if reaper.GetToggleCommandState(40078) ~= {want} then '
                'reaper.Main_OnCommand(40078, 0) end', name="reaclaw_mixer")


# ---- PREPARE (not recorded) ----------------------------------------------------

def prepare():
    lib.act(lib.STOP)
    lib.clear_project()
    lib.run_lua('for i = reaper.CountTempoTimeSigMarkers(0) - 1, 0, -1 do '
                'reaper.DeleteTempoTimeSigMarker(0, i) end\n'
                f'reaper.SetCurrentBPM(0, {BPM}, false)', name="reaclaw_flat_tempo")
    lib.disable_follow()
    set_mixer(False)
    lib._ensure_view_applier()
    lib.set_view(*WHOLE)

    outdir = "/tmp/fxauto"
    os.makedirs(outdir, exist_ok=True)

    for i, (name, fx, gain, gen) in enumerate(TRACKS):
        idx = lib.add_track(name=name, volume_db=gain, muted=True)
        assert idx == i, f"track order drifted: {name} landed at {idx}, expected {i}"
        r = lib.add_fx(idx, "ReaSynth")
        assert r["slot"] == REASYNTH_SLOT, f"ReaSynth landed at slot {r['slot']}, expected {REASYNTH_SLOT}"
        r = lib.add_fx(idx, fx)
        assert r["slot"] == FX_SLOT, f"{fx} landed at slot {r['slot']}, expected {FX_SLOT}"

        if name == "Drums":
            notes = kick() + snare() + hats()
        else:
            notes = gen()
        path = os.path.join(outdir, f"{name.lower()}.mid")
        write_smf(path, notes, bpm=BPM)
        lib.insert_media(idx, path)

    lib.set_loop_and_repeat(LOOP_LEN)
    close_midi_editors()
    close_fx_windows()
    fit_all_tracks_vertically()
    lib.run_lua('reaper.SetEditCurPos(0, false, false)', name="reaclaw_curzero")
    lib.set_view(*WHOLE)
    print(f"prepared: {BARS} bars, {len(TRACKS)} tracks + FX, loop {LOOP_LEN:.0f}s")


# ---- PERFORM (recorded) ---------------------------------------------------------

def establish_and_build():
    lib.run_lua('reaper.SetEditCurPos(0, false, false)', name="reaclaw_curzero")
    lib.set_view(*WHOLE)
    lib.mark("ReaClaw — FX & Automation")
    lib.play_from_start()
    hold(3.5)
    for i, (name, fx, _gain, _gen) in enumerate(TRACKS):
        lib.set_track(i, muted=False)
        if i == 0:
            lib.mark(f"POST /state/tracks/{{i}}  muted:false — {name} ({fx})")
        else:
            lib.mark(f"{name} — {fx}")
        hold(2.75)
    lib.mark("four tracks, four effects — one loop")
    hold(3.75)


def fx_reveal_and_param_automation():
    """Open the Drums compressor's floating GUI, then arm + write its
    Threshold envelope LIVE while it's on screen — the fader visibly sweeps
    in sync with the audible pumping as the loop plays."""
    anim(0, LOOP_LEN * 0.35, 2.0)
    select_track(DRUMS)
    lib.mark("GET /state/tracks/{i}/fx/{slot} — ReaComp on Drums")
    lib.show_fx(DRUMS, FX_SLOT, True)
    hold(2.5)

    points = [(0.0, 0.85), (LOOP_LEN * 0.5, 0.15), (LOOP_LEN, 0.85)]
    lib.write_fx_param_envelope(DRUMS, FX_SLOT, 0, points)   # param 0 = Threshold
    lib.mark("FX-parameter envelope — Threshold, written while it plays")
    # LOOP_LEN is a fixed musical duration (BPM/BARS), not scaled by PACE —
    # hold several full loop cycles so the fader visibly sweeps more than once.
    time.sleep(2.75 * LOOP_LEN)

    lib.show_fx(DRUMS, FX_SLOT, False)


def control_lane_pan():
    """A second flavor of control lane: the built-in track Pan envelope on
    Pad, ping-ponging across the loop."""
    select_track(PAD)
    lib.act(lib.ARM_PAN_ENV)
    points = [{"time": 0.0, "value": -0.8},
              {"time": LOOP_LEN * 0.5, "value": 0.8},
              {"time": LOOP_LEN, "value": -0.8}]
    lib.write_automation(PAD, "Pan", points)
    lib.mark("POST /state/tracks/{i}/automation — Pan envelope on Pad")
    lib.act(lib.TRACKS_TALLER)
    anim(LOOP_LEN * 0.1, LOOP_LEN * 0.6, 2.0)
    time.sleep(2.25 * LOOP_LEN)   # fixed musical duration, see fx_reveal note above
    lib.act(lib.TRACKS_SHORTER)


def preset_tour():
    """Step the Lead ReaEQ through a few of its 24 factory presets, GUI open
    so the curve visibly redraws each step."""
    select_track(LEAD)
    lib.show_fx(LEAD, FX_SLOT, True)
    anim(0, LOOP_LEN * 0.3, 1.5)
    lib.mark("POST /state/tracks/{i}/fx/{slot}/preset  {navigate:1}")
    for _ in range(6):
        r = lib.fx_preset(LEAD, FX_SLOT, navigate=1)
        lib.mark(r.get("preset", "?"))
        hold(3.5)
    lib.show_fx(LEAD, FX_SLOT, False)


def midi_edit_beat():
    """Fold in the proven MIDI read/write/transform sequence (see
    show_midi.py) as one beat of this trailer rather than a standalone demo."""
    select_item(LEAD)
    lib.mark("all the way in — the piano roll")
    open_midi_editor()
    hold(1.5)

    midi = lib.get_midi(LEAD)
    lib.mark(f"GET /state/items/{{i}}/midi — {midi['note_count']} notes read back")
    hold(2.0)

    shifted = [{**n, "pitch": n["pitch"] + 7} for n in midi["notes"]]
    lib.post_midi(LEAD, notes=shifted, replace=True)
    lib.mark("read → transform → write — transposed +7 semitones")
    hold(3.5)

    steps = 10
    cc_sweep = [{"number": 11, "value": int(127 * (i / (steps - 1))),
                 "channel": 0, "ppq": int(960 * LOOP_LEN / SPB * i / (steps - 1))}
                for i in range(steps)]
    lib.post_midi(LEAD, cc=cc_sweep)
    lib.mark(f"CC11 expression curve — {steps} points via POST")
    hold(2.5)

    close_midi_editors()
    time.sleep(0.6)


def finale():
    fit_all_tracks_vertically()
    lib.mark("...and back out")
    anim(*WHOLE, 3.0)
    lib.mark("every FX chain. every preset. every curve. one API.")
    hold(3.5)
    anim(LOOP_LEN * 0.35, LOOP_LEN * 0.65, 1.2)
    hold(1.0)
    anim(*WHOLE, 1.2)
    hold(2.5)


def perform():
    lib.reset_marks()
    lib.mark("START")
    establish_and_build()
    fx_reveal_and_param_automation()
    control_lane_pan()
    preset_tour()
    midi_edit_beat()
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

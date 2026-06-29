#!/usr/bin/env python3
"""show_midi.py — MIDI editing showcase choreography.

Story arc:
  Empty REAPER → two tracks appear → piano roll opens → 16 notes write
  themselves via POST → GET reads them back as JSON → all pitches shift
  (+5 semitones) via read→transform→write → bass line written on track 2 →
  camera pulls back showing both tracks playing.

  Caption at the end: "No plugins. No GUI. Just HTTP."

Run AFTER record.sh has started. It logs epoch marks to /tmp/marks.txt for
post.py to align lower-third captions.
"""
import time
import subprocess
import lib
from smf import Note, write_smf

BPM   = 110
TPQN  = 960          # REAPER default PPQ resolution
SPB   = 60.0 / BPM  # seconds per beat
SPbar = 4 * SPB      # seconds per bar (4/4)


# ---------------------------------------------------------------------------
# Music: D natural minor, 2-bar melodic phrase
# ---------------------------------------------------------------------------
#  D  E  F  G  A  Bb C  D
# 62 64 65 67 69 70  72 74

MELODY = [
    # (start_beat, dur_beats, pitch, vel)
    (0.0,  0.25, 62,  90),   # D4
    (0.25, 0.25, 64,  75),   # E4
    (0.5,  0.5,  65,  85),   # F4
    (1.0,  0.5,  67,  95),   # G4
    (1.5,  0.25, 69,  80),   # A4
    (1.75, 0.25, 70,  70),   # Bb4
    (2.0,  1.0,  72, 100),   # C5 (hold)
    (3.0,  0.5,  74,  95),   # D5
    # bar 2 — descending answer
    (4.0,  0.25, 72,  85),   # C5
    (4.25, 0.25, 70,  75),   # Bb4
    (4.5,  0.5,  69,  90),   # A4
    (5.0,  0.5,  67,  80),   # G4
    (5.5,  0.25, 65,  75),   # F4
    (5.75, 0.25, 64,  70),   # E4
    (6.0,  1.5,  62, 100),   # D4 (long landing)
    (7.5,  0.5,  55,  60),   # G3 (low pickup)
]

BASS_LINE = [
    # root / fifth movement, 2 bars, whole and half notes
    (0.0, 1.9, 38, 85),   # D2 (root)
    (2.0, 1.9, 45, 80),   # A2 (fifth)
    (4.0, 1.9, 36, 90),   # C2 (VII)
    (6.0, 1.9, 38, 85),   # D2 (back to root)
]

LOOP_BEATS = 8.0
LOOP_SEC   = LOOP_BEATS * SPB


def b2p(beats):
    """Beats → PPQ ticks."""
    return beats * TPQN


def melody_notes():
    return [{"pitch": p, "velocity": v,
             "start_ppq": b2p(s), "end_ppq": b2p(s + d)}
            for s, d, p, v in MELODY]


def bass_notes():
    return [{"pitch": p, "velocity": v,
             "start_ppq": b2p(s), "end_ppq": b2p(s + d)}
            for s, d, p, v in BASS_LINE]


def transpose(notes, semitones):
    return [{**n, "pitch": n["pitch"] + semitones} for n in notes]


def open_piano_roll(item_index):
    lib.run_lua(
        f'reaper.SetMediaItemSelected(reaper.GetMediaItem(0, {item_index}), true)\n'
        f'reaper.Main_OnCommand(40153, 0)\n'
    )
    time.sleep(0.6)
    subprocess.run(["wmctrl", "-a", "MIDI"], capture_output=True)


def scroll_piano_roll_to_notes():
    """After inserting notes, scroll the MIDI editor to show them.
    Selects all notes then uses the MIDI editor zoom-to-selected command."""
    lib.run_lua(
        'local hwnd = reaper.MIDIEditor_GetActive()\n'
        'if hwnd then\n'
        # Select all notes so "zoom to selected" works
        '  reaper.MIDIEditor_OnCommand(hwnd, 40003)\n'
        # Zoom vertically to fit all selected notes in view
        '  reaper.MIDIEditor_OnCommand(hwnd, 40454)\n'
        'end\n'
    )


def close_piano_roll():
    subprocess.run(["wmctrl", "-c", "MIDI"], capture_output=True)
    time.sleep(0.3)


def seed_midi_item(track_index, path):
    """Write a silent full-length .mid and InsertMedia to create the MIDI take.
    The seed note spans the full loop so the item is long enough for all notes
    we will POST via the MIDI API (items clip notes beyond their length)."""
    write_smf(path, [Note(0, LOOP_BEATS - 0.01, 62, 1)], bpm=BPM)
    lib.insert_media(track_index, path)


# ---------------------------------------------------------------------------

def main():
    lib.reset_marks()
    lib.act(lib.STOP)
    lib.clear_project()
    # Set a clean project name in the title bar (no dialog needed)
    lib.run_lua('reaper.GetSetProjectInfo_String(0, "PROJECT_NAME", "ReaClaw MIDI Demo", true)')
    lib.mark("START")

    # ------------------------------------------------------------------
    # Act 1 — create two tracks in one call
    # ------------------------------------------------------------------
    r = lib.post("/state/tracks", {"create": [
        {"name": "Lead",  "volume_db": -9,  "muted": False},
        {"name": "Bass",  "volume_db": -12, "muted": True},
    ]})
    lead_idx = r["created"][0]["index"]
    bass_idx  = r["created"][1]["index"]

    lib.add_fx(lead_idx, "ReaSynth")
    lib.add_fx(bass_idx,  "ReaSynth")
    lib.mark("2 tracks, 2 ReaSynth instances — one call each")
    time.sleep(0.5)

    # Seed MIDI items (minimal .mid → InsertMedia creates the MIDI take)
    seed_midi_item(lead_idx, "/tmp/lead_seed.mid")
    seed_midi_item(bass_idx,  "/tmp/bass_seed.mid")

    # ------------------------------------------------------------------
    # Act 2 — write the melody via POST /state/items/0/midi
    # ------------------------------------------------------------------
    open_piano_roll(0)   # piano roll open so audience sees notes land
    time.sleep(0.3)
    lib.mark("Piano roll open — writing melody via POST /state/items/0/midi")

    notes = melody_notes()
    lib.post_midi(0, notes=notes, replace=True)
    scroll_piano_roll_to_notes()    # zoom piano roll to show the notes
    lib.mark(f"{len(notes)} notes inserted — D minor phrase")
    time.sleep(SPbar)    # let the audience read the piano roll

    # Start looping playback
    lib.set_loop_and_repeat(LOOP_SEC)
    lib.disable_follow()
    lib.play_from_start()
    time.sleep(2 * SPbar)

    # ------------------------------------------------------------------
    # Act 3 — GET reads every note back as JSON
    # ------------------------------------------------------------------
    midi = lib.get_midi(0)
    n = midi["note_count"]
    lib.mark(f"GET /state/items/0/midi → {n} notes, pitch + time + velocity + PPQ")
    time.sleep(SPbar)

    # ------------------------------------------------------------------
    # Act 4 — transform: transpose +5 semitones via read → modify → write
    # ------------------------------------------------------------------
    current = midi["notes"]
    shifted = transpose(current, 5)   # D minor → G minor
    lib.post_midi(0, notes=shifted, replace=True)
    lib.mark(f"Transposed +5 st (D min → G min) — read → transform → write")
    time.sleep(2 * SPbar)

    # ------------------------------------------------------------------
    # Act 5 — add CC11 expression curve on the lead
    # ------------------------------------------------------------------
    steps = 12
    cc_swell = [
        {"number": 11, "value": int(127 * (i / (steps - 1)) ** 0.5),
         "channel": 0, "ppq": b2p(LOOP_BEATS * i / (steps - 1))}
        for i in range(steps)
    ]
    lib.post_midi(0, cc=cc_swell)
    lib.mark(f"CC11 expression sweep — {steps} points via POST")
    time.sleep(SPbar)

    close_piano_roll()

    # ------------------------------------------------------------------
    # Act 6 — unmute bass and write its line
    # ------------------------------------------------------------------
    lib.set_track(bass_idx, muted=False)
    lib.post_midi(1, notes=bass_notes(), replace=True)
    lib.mark("Bass line written via POST /state/items/1/midi")
    time.sleep(2 * SPbar)

    # ------------------------------------------------------------------
    # Act 7 — zoom out, show the full picture
    # ------------------------------------------------------------------
    lib.act(lib.ZOOM_OUT_HORIZ)
    lib.act(lib.ZOOM_OUT_HORIZ)
    lib.act(lib.ZOOM_TO_TIMESEL)
    lib.act(lib.TRACKS_TALLER)
    lib.mark("No plugins. No GUI. Just HTTP.")
    time.sleep(2 * SPbar)

    lib.act(lib.STOP)
    lib.mark("END")
    print("choreography done — marks in /tmp/marks.txt")


if __name__ == "__main__":
    main()

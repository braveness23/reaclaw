#!/usr/bin/env python3
"""smf.py — minimal Type-0 Standard MIDI File writer (no dependencies).

WHY THIS EXISTS:
  On the Pi test rig (REAPER v7.74, linux-aarch64) the in-process MIDI builders
  are broken:
    * reaper.CreateNewMIDIItemInProject is nil.
    * Inserting notes into a take built from PCM_Source_CreateFromType("MIDI")
      fails silently: MIDI_GetPPQPosFromProjTime returns -1 (no valid PPQ map),
      so MIDI_InsertNote returns false and notecount stays 0 -> silence (-91 dB).
  The reliable route to get audible notes into REAPER is to write a real .mid
  file on disk and reaper.InsertMedia() it onto a selected track. This module is
  that writer. A Type-0 (single-track) SMF is all REAPER needs.

USAGE:
  from smf import Note, write_smf
  notes = [Note(start_beats=0, dur_beats=0.5, pitch=36, vel=110)]
  write_smf("/tmp/kick.mid", notes, bpm=120)
"""
import struct

PPQ = 480  # ticks per quarter note (division written into the header)


def _vlq(value: int) -> bytes:
    """Encode an int as a MIDI variable-length quantity."""
    if value < 0:
        raise ValueError("VLQ cannot encode negative values")
    out = bytearray([value & 0x7F])
    value >>= 7
    while value:
        out.insert(0, (value & 0x7F) | 0x80)
        value >>= 7
    return bytes(out)


class Note:
    """A single note. Times are in beats (quarter notes); converted to ticks."""

    def __init__(self, start_beats, dur_beats, pitch, vel=100, chan=0):
        self.start = int(round(start_beats * PPQ))
        self.dur = max(1, int(round(dur_beats * PPQ)))
        self.pitch = int(pitch) & 0x7F
        self.vel = int(vel) & 0x7F
        self.chan = int(chan) & 0x0F


def write_smf(path, notes, bpm=120):
    """Write `notes` to `path` as a Type-0 SMF at the given tempo."""
    # (abs_tick, order, event_bytes) — order 1 (note-off) sorts before
    # order 0 (note-on) at the same tick so zero-gap notes retrigger cleanly.
    events = []
    for n in notes:
        events.append((n.start, 0, bytes([0x90 | n.chan, n.pitch, n.vel])))
        events.append((n.start + n.dur, -1, bytes([0x80 | n.chan, n.pitch, 0])))
    events.sort(key=lambda e: (e[0], e[1]))

    track = bytearray()
    mpqn = int(round(60_000_000 / bpm))           # microseconds per quarter note
    track += _vlq(0) + b"\xFF\x51\x03" + mpqn.to_bytes(3, "big")  # set-tempo meta

    prev = 0
    for tick, _order, data in events:
        track += _vlq(tick - prev) + data
        prev = tick
    track += _vlq(0) + b"\xFF\x2F\x00"             # end-of-track meta

    header = b"MThd" + struct.pack(">IHHH", 6, 0, 1, PPQ)  # fmt 0, 1 track
    chunk = b"MTrk" + struct.pack(">I", len(track)) + bytes(track)
    with open(path, "wb") as f:
        f.write(header + chunk)
    return path


if __name__ == "__main__":
    # smoke test: a one-bar four-on-the-floor kick
    write_smf("/tmp/kick_test.mid",
              [Note(b, 0.25, 36, 110) for b in range(4)], bpm=120)
    print("wrote /tmp/kick_test.mid")

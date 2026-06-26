#!/usr/bin/env python3
"""gen_groove.py — generate the 7 per-track .mid files for the trailer groove.

Every track gets its OWN .mid + its OWN ReaSynth instance. ReaSynth is a single
sine for every instance, so the only way to differentiate "instruments" is by
PITCH + NOTE LENGTH:
    low  + short  -> kick / bass
    mid  + short  -> snare-ish body
    high + short  -> hats / perc
    sustained     -> pad / lead
Gain-staging happens later in show.py (-5..-17 dB/track) so the master doesn't
clip at 0 dB.

Writes 7 files into OUTDIR (default /tmp/groove) and prints the manifest the
choreography consumes.
"""
import os
from smf import Note, write_smf

BPM = 120
# BARS is env-configurable so a trailer can ask for a long canvas to fly the
# camera across (the zoom trailer uses 16). Defaults to 4 for the original demo.
BARS = int(os.environ.get("GROOVE_BARS", "4"))
BEATS = BARS * 4  # 4/4

OUTDIR = os.environ.get("GROOVE_DIR", "/tmp/groove")
os.makedirs(OUTDIR, exist_ok=True)


def kick():
    return [Note(b, 0.22, 36, 115) for b in range(BEATS)]            # four-on-floor


def bass():
    pat = [0, 0.75, 2, 2.5, 3]
    out = []
    for bar in range(BARS):
        for p in pat:
            out.append(Note(bar * 4 + p, 0.4, 40 + (0 if p < 2 else 3), 95))
        out[-1].pitch += 2
    return out


def snare():
    # backbeat on beats 2 & 4 of every bar (fills the whole canvas, any BARS)
    return [Note(b, 0.18, 50, 100) for b in range(1, BEATS, 2)]


def hats():
    out = []
    for i in range(BEATS * 2):           # eighths
        vel = 80 if i % 2 == 0 else 55
        out.append(Note(i * 0.5, 0.1, 78, vel))
    return out


def perc():
    return [Note(b + 0.5, 0.12, 84, 70) for b in range(0, BEATS, 2)]


def pad():
    chord = [55, 59, 62]                 # sustained triad, one bar each
    out = []
    for bar in range(BARS):
        shift = 0 if bar % 2 == 0 else 3
        for p in chord:
            out.append(Note(bar * 4, 3.8, p + shift, 60))
    return out


def lead():
    motif = [(0, 67), (1, 70), (1.5, 72), (2, 74), (3, 71)]
    out = []
    for bar in range(BARS):
        for t, p in motif:
            out.append(Note(bar * 4 + t, 0.45, p + (0 if bar < 2 else 5), 85))
    return out


# Order = unmute order in the build-up. (name, generator, gain_db)
TRACKS = [
    ("Kick",  kick,  -8),
    ("Bass",  bass,  -7),
    ("Hats",  hats,  -16),
    ("Snare", snare, -10),
    ("Perc",  perc,  -15),
    ("Pad",   pad,   -13),
    ("Lead",  lead,  -11),
]


def main():
    manifest = []
    for name, gen, gain in TRACKS:
        path = os.path.join(OUTDIR, f"{name.lower()}.mid")
        write_smf(path, gen(), bpm=BPM)
        manifest.append((name, path, gain))
        print(f"{name:6s} {gain:>4d} dB  {path}")
    return manifest


if __name__ == "__main__":
    main()

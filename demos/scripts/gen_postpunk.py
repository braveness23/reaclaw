#!/usr/bin/env python3
"""gen_postpunk.py — synthesize the pitched material for the post-punk trailer.

Two voices, both generated offline as one WAV per pitch/chord and played back
as note-filtered RS5K banks / audio items (same approach as the industrial
trailer's FM bass — there is no synth plugin scripting needed this way):

  * "hook bass"  — Karplus-Strong plucked string, two voices detuned ±0.12%
    (a poor man's chorus: the Peter Hook tone is a picked, chorused bass
    playing melody up high). 3 s of ring so RS5K note-offs do the gating.
  * "cold pad"   — 4 detuned saws through a one-pole lowpass, slow attack,
    one WAV per chord (Em / C / D). Placed as audio items, not a bank.

Writes into ~/studio/projects/active/PostPunkTrailer/Media/synth.
"""
import os
import wave

import numpy as np

SR = 48000
OUT = os.path.expanduser("~/studio/projects/active/PostPunkTrailer/Media/synth")
os.makedirs(OUT, exist_ok=True)

# MIDI note -> name used in filenames; the show script mirrors this table.
BASS_NOTES = {
    "E1": 28, "A1": 33, "B1": 35,
    "E2": 40, "G2": 43, "A2": 45, "B2": 47, "C3": 48, "D3": 50,
    "E3": 52, "Fs3": 54, "G3": 55,
}

PAD_CHORDS = {
    "Em": [52, 55, 59, 64],   # E3 G3 B3 E4
    "C":  [48, 52, 55, 60],   # C3 E3 G3 C4
    "D":  [50, 54, 57, 62],   # D3 Fs3 A3 D4
}


def f_of(midi):
    return 440.0 * 2 ** ((midi - 69) / 12)


def write_wav(path, x):
    x = np.asarray(x, dtype=np.float64)
    peak = np.abs(x).max()
    if peak > 0:
        x = x / peak * 0.89
    pcm = (x * 32767).astype(np.int16)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())
    return path


def ks_pluck(freq, dur=3.0, damp=0.9990, bright=0.55):
    """Karplus-Strong: noise burst through a damped averaging loop."""
    n = int(SR * dur)
    period = max(2, int(round(SR / freq)))
    rng = np.random.default_rng(int(freq * 1000) & 0xFFFF)
    buf = rng.uniform(-1, 1, period)
    # pick position flavor: pre-filter the excitation a little
    buf = bright * buf + (1 - bright) * np.convolve(buf, [0.5, 0.5], "same")
    out = np.empty(n)
    prev = 0.0
    for i in range(n):
        v = buf[i % period]
        nxt = damp * 0.5 * (v + prev)
        out[i] = v
        buf[i % period] = nxt
        prev = v
    return out


def hook_bass(midi, dur=3.0):
    """Two detuned KS voices + a quiet octave-down sine for body."""
    f = f_of(midi)
    a = ks_pluck(f * 1.0012, dur)
    b = ks_pluck(f * 0.9988, dur)
    t = np.arange(int(SR * dur)) / SR
    sub = 0.16 * np.sin(2 * np.pi * (f / 2) * t) * np.exp(-t * 2.2)
    x = 0.5 * a + 0.5 * b + sub
    # gentle high shelf via one-pole to tame KS fizz
    y = np.empty_like(x)
    acc = 0.0
    for i, s in enumerate(x):
        acc += 0.35 * (s - acc)
        y[i] = 0.55 * s + 0.45 * acc
    env = np.minimum(1.0, np.arange(len(y)) / (0.002 * SR))
    return y * env


def cold_pad(midis, dur=9.0, attack=1.2, cutoff=1400.0):
    n = int(SR * dur)
    t = np.arange(n) / SR
    x = np.zeros(n)
    rng = np.random.default_rng(7)
    for m in midis:
        f = f_of(m)
        for det in (-0.19, -0.07, 0.08, 0.21):
            ph = rng.uniform(0, 1)
            x += 0.25 * (2 * ((t * f * (1 + det / 100) + ph) % 1.0) - 1)
    # one-pole lowpass — icy but not buzzy
    a = 1 - np.exp(-2 * np.pi * cutoff / SR)
    y = np.empty(n)
    acc = 0.0
    for i in range(n):
        acc += a * (x[i] - acc)
        y[i] = acc
    env = np.minimum(1.0, t / attack) * np.minimum(1.0, (dur - t) / 1.5)
    return y * env


def main():
    for name, midi in BASS_NOTES.items():
        p = write_wav(f"{OUT}/bass_{name}.wav", hook_bass(midi))
        print("wrote", p)
    for name, midis in PAD_CHORDS.items():
        p = write_wav(f"{OUT}/pad_{name}.wav", cold_pad(midis))
        print("wrote", p)


if __name__ == "__main__":
    main()

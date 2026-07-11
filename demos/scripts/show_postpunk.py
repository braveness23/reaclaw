#!/usr/bin/env python3
"""show_postpunk.py — THE POST-PUNK trailer: drums in the school of Stephen
Morris (Joy Division / New Order) and Kevin Haskins (Bauhaus / Tones on Tail),
built from nothing, live, over the ReaClaw API. The brief: drums that make
music instead of keeping time; repetition as bliss; a bass that dances with
the kick.

The instruments (all sampled hardware, kb6.de archive via Dave's library):
  * Oberheim DMX      — New Order's drum machine (kick/snare/hats/clap/crash)
  * Simmons SDS5      — the hexagon electronic toms, tuned like a melody
  * Star Inst. Synare — the electric 'pew' that answers the snare
  * hook bass         — Karplus-Strong plucked string, detuned pair (chorus),
                        one WAV per pitch in a note-filtered RS5K bank
  * cold pad          — detuned saws, placed as audio items

Song: 48 bars, 138 BPM, E minor.
  S0  0-4   the machine wakes: 16th-note DMX kick, velocity ramp, claps join
  S1  4-12  drums only — displaced kick, Synare answers, the Simmons tom hook
  S2 12-20  bass enters driving 8ths; open hats; tom cascades close phrases
  S3 20-28  tribal turn (Haskins): floor tom + rim clave + splash; bass drone
  S4 28-36  rebuild: the bass hook, pad cycle Em-C-D, hook doubled, cascades
  S5 36-40  peak: motorik 8th kick, ride, everything on
  S5b40-44  the machine returns: 16th kick + octave-jump 16th bass sequence
  S6 44-48  strip, final crash, let it ring

Modes:
    python3 show_postpunk.py probe    # dump FX param maps (auto-runs if absent)
    python3 show_postpunk.py build    # fast full build, no camera, no holds
    python3 show_postpunk.py render   # build + offline render + analysis
    python3 show_postpunk.py perform  # the recorded performance
"""
import json
import os
import sys
import time

import lib
from smf import Note, write_smf

# ---- musical constants ------------------------------------------------------

BPM = 138.0
SPB = 60.0 / BPM
BAR = SPB * 4                 # 1.7391s
TOTAL_BARS = 48
PACE = float(os.environ.get("PACE", "1.0"))

HOME = os.path.expanduser("~")
SMP_DMX = f"{HOME}/studio/samples/dmx"
SMP_SDS = f"{HOME}/studio/samples/sds5"
SMP_SYN = f"{HOME}/studio/samples/synare"
M = f"{HOME}/studio/projects/active/PostPunkTrailer/Media"
RPP = f"{HOME}/studio/projects/active/PostPunkTrailer/PostPunkTrailer.rpp"
TMP = "/tmp/postpunk"
os.makedirs(TMP, exist_ok=True)

(T_KICK, T_SNARE, T_HATS, T_TOMS, T_PERC, T_BASS, T_PAD) = range(7)

TRACK_NAMES = ["DMX Kick", "DMX Snare", "DMX Hats", "Simmons Toms",
               "Synare/Perc", "Hook Bass", "Cold Pad"]

# cold factory palette: steel blues + the red hexagon pads
TRACK_COLORS = ["#33507A", "#7A8CA8", "#5A6A78", "#B03A3A",
                "#C08030", "#3AA06A", "#7A5AA0"]

HAT = {"closed": 60, "open": 62}
TOM = {"hi": 72, "mid": 67, "lo": 62, "fl": 57}
PERC = {"pew": 60, "sweep": 62, "clap": 64, "rim": 65, "ride": 67, "crash": 69}
BASS = {"E1": 28, "A1": 33, "B1": 35,
        "E2": 40, "G2": 43, "A2": 45, "B2": 47, "C3": 48, "D3": 50,
        "E3": 52, "Fs3": 54, "G3": 55}

# ---- helpers ----------------------------------------------------------------


def hold(sec):
    time.sleep(sec * PACE)


def anim(a1, b1, dur):
    lib.animate_view_to(a1, b1, dur * PACE)


def insert_media_at(track, path, pos):
    lua = (
        f'reaper.SetOnlyTrackSelected(reaper.GetTrack(0, {track}))\n'
        f'reaper.SetEditCurPos({float(pos):.6f}, false, false)\n'
        f'reaper.InsertMedia({json.dumps(path)}, 0)\n'
    )
    return lib.run_lua(lua, name=f"pp_ins_{track}")


def rs5k(track, file, note=None, obey_noteoffs=False, release=None):
    """Add an RS5K on `track`, load `file`, optionally filter to one MIDI
    note. Returns the fx slot."""
    slot = lib.add_fx(track, "ReaSamplOmatic5000")["slot"]
    lua = (
        f'local tr = reaper.GetTrack(0, {track})\n'
        f'reaper.TrackFX_SetNamedConfigParm(tr, {slot}, "FILE0", {json.dumps(file)})\n'
        f'reaper.TrackFX_SetNamedConfigParm(tr, {slot}, "DONE", "")\n'
    )
    lib.run_lua(lua, name=f"pp_rs5k_{track}_{slot}")
    params = []
    if note is not None:
        params += [{"index": 3, "value": note / 127.0},
                   {"index": 4, "value": note / 127.0}]
    if obey_noteoffs:
        params.append({"index": 11, "value": 1.0})
    if release is not None:
        params.append({"index": 10, "value": release})
    if params:
        lib.post(f"/state/tracks/{track}/fx/{slot}", {"params": params})
    return slot


def set_fx(track, slot, mapping):
    lib.post(f"/state/tracks/{track}/fx/{slot}",
             {"params": [{"index": i, "value": v} for i, v in mapping.items()]})


_mid_seq = 0


def put_midi(track, notes, at_bar, bars):
    """Write `notes` (beat offsets relative to the phrase) as a .mid and insert
    it at bar `at_bar`. Pads to `bars` with a silent end marker note."""
    global _mid_seq
    _mid_seq += 1
    path = f"{TMP}/p{_mid_seq:03d}_t{track}.mid"
    pad = [Note(bars * 4 - 0.01, 0.01, 127, 1)]
    write_smf(path, notes + pad, bpm=BPM)
    insert_media_at(track, path, at_bar * BAR)


# ---- step-pattern language ---------------------------------------------------
# 16 chars per bar. 'X' accent 127, 'x' normal 104, 'o' soft 84, 'g' ghost 64.

VEL = {"X": 127, "x": 104, "o": 84, "g": 64}


def steps(pattern, pitch, gate=0.12):
    out = []
    for i, ch in enumerate(pattern.replace(" ", "").replace("|", "")):
        if ch in VEL:
            out.append(Note(i * 0.25, gate, pitch, VEL[ch]))
    return out


def drum_bars(pattern, pitch, bars, gate=0.1):
    """Tile a 1-bar pattern across `bars`."""
    out = []
    one = steps(pattern, pitch, gate)
    for b in range(bars):
        out += [Note(n.start / 480 + b * 4, n.dur / 480, n.pitch, n.vel)
                for n in one]
    return out


def shift(notes, bars):
    return [Note(n.start / 480 + bars * 4, n.dur / 480, n.pitch, n.vel)
            for n in notes]


# the patterns ----------------------------------------------------------------

MACH = "XxxxXxxxXxxxXxxx"          # the machine: every 16th, quarters accented
MACHB = "XxxxXxxxXxxxXxXx"
MACHBRK = "X.xxX.xxX.x.Xx.x"       # the lurch
K_A = "X...X..xX...X..x"           # displaced drive kick
K_MOTORIK = "X.x.X.x.X.x.X.x."     # relentless 8ths
K_TRIB = "X..............."
SN24 = "....X.......X..."
HAT16 = "xgogxgogxgogxgog"
OPEN8 = "..o...o...o...o."
RIDE4 = "x...x...x...x..."
RIM_CLAVE = "..X..X....X....."     # Bela Lugosi rim feel
RIM_CLAVE2 = "..X..X....X...X."
FLOOR8 = "X.x.x.x.o.x.x.x."


def tom_hook(at_bar_parity=0):
    """The 2-bar Simmons hook — three pads answering the snare like a vocal.
    bar A: hi hi -> mid ; bar B: mid mid -> lo ... floor pickup."""
    a = (steps("......xX........", TOM["hi"], gate=0.2)
         + steps("..........X.....", TOM["mid"], gate=0.25))
    b = (steps("......xX........", TOM["mid"], gate=0.2)
         + steps("..........X.....", TOM["lo"], gate=0.25)
         + steps("..............x.", TOM["fl"], gate=0.25))
    return a + shift(b, 1)


def cascade_half():
    """Half-bar descend across the four pads — the Morris fill."""
    return (steps("........Xx......", TOM["hi"], gate=0.15)
            + steps("..........Xx....", TOM["mid"], gate=0.15)
            + steps("............Xx..", TOM["lo"], gate=0.15)
            + steps("..............Xx", TOM["fl"], gate=0.15))


def cascade_full():
    """Full-bar fall, velocity climbing into the downbeat."""
    out = []
    for i, (name, v0) in enumerate((("hi", 96), ("mid", 104),
                                    ("lo", 112), ("fl", 120))):
        for k in range(4):
            out.append(Note((i * 4 + k) * 0.25, 0.15, TOM[name],
                            min(127, v0 + k * 3)))
    return out


def bass_drive(bars):
    """Driving 8ths on E2 (the Age of Consent engine); every 4th bar walks
    up G2 -> A2 into the next downbeat."""
    out = []
    for b in range(bars):
        vel = [118, 96, 110, 96, 118, 96, 110, 96]
        pits = [BASS["E2"]] * 8
        if b % 4 == 3:
            pits[6], pits[7] = BASS["G2"], BASS["A2"]
            vel[6], vel[7] = 112, 118
        for i in range(8):
            out.append(Note(b * 4 + i * 0.5, 0.42, pits[i], vel[i]))
    return out


def bass_hook(times=1):
    """The 2-bar melodic hook, played up high where Hooky lives."""
    line1 = ["E3", "E3", "D3", "E3", "B2", "B2", "C3", "D3"]
    line2 = ["E3", "E3", "D3", "C3", "B2", "A2", "G2", "A2"]
    out = []
    for t in range(times):
        base = t * 8
        for i, n in enumerate(line1):
            out.append(Note(base + i * 0.5, 0.42, BASS[n], 116 if i % 2 == 0 else 98))
        for i, n in enumerate(line2):
            out.append(Note(base + 4 + i * 0.5, 0.42, BASS[n], 116 if i % 2 == 0 else 98))
    return out


def bass_octaves(bars):
    """16th-note octave-jump sequence against the machine kick."""
    pat = ["E2", "E2", "E3", "E2", "E2", "E3", "E2", "E3",
           "E2", "E2", "E3", "E2", "E2", "E3", "E2", "E2"]
    out = []
    for b in range(bars):
        for i, n in enumerate(pat):
            v = 120 if i % 4 == 0 else (108 if n == "E3" else 96)
            out.append(Note(b * 4 + i * 0.25, 0.2, BASS[n], v))
    return out


def bass_drone(bars):
    """Long low E every two bars (the tribal bed)."""
    out = []
    for b in range(0, bars, 2):
        out.append(Note(b * 4, 7.6, BASS["E2"], 102))
    return out


# ---- FX param maps -----------------------------------------------------------

FXMAP_PATH = f"{TMP}/fx_map.json"
FXMAP = json.load(open(FXMAP_PATH)) if os.path.exists(FXMAP_PATH) else {}


def set_fx_named(track, slot, fxname, wanted):
    idx = FXMAP.get(fxname, {})
    mapping = {idx[k]: v for k, v in wanted.items() if k in idx}
    if mapping:
        set_fx(track, slot, mapping)


# ---- project setup -----------------------------------------------------------


def setup():
    lib.act(lib.STOP)
    lib.post("/project/reset", {"discard_changes": True})
    for _ in range(50):
        if lib.get("/state")["track_count"] == 0:
            break
        time.sleep(0.2)
    lib.post("/project/save", {"path": RPP})
    lib.set_tempo(0, BPM)
    lib.disable_follow()
    lib._ensure_view_applier()
    lib.set_view(-0.5, 16.0)


def build_kit(cam=False):
    for i, n in enumerate(TRACK_NAMES):
        lib.add_track(name=n)
        lib.set_track(i, color=TRACK_COLORS[i])
    if cam:
        lib.mark("7 tracks. two drum machines, a synth drum, a bass made of math")
        hold(1.6)

    slots = {}

    # KICK / SNARE / HATS: the Oberheim DMX
    rs5k(T_KICK, f"{SMP_DMX}/DMXKick01.wav", note=60)
    rs5k(T_SNARE, f"{SMP_DMX}/DMXSnare01.wav", note=60)
    slots["sn_dly"] = lib.add_fx(T_SNARE, "ReaDelay")["slot"]
    slots["sn_verb"] = lib.add_fx(T_SNARE, "ReaVerbate")["slot"]
    rs5k(T_HATS, f"{SMP_DMX}/DMXHat_C.wav", note=HAT["closed"])
    rs5k(T_HATS, f"{SMP_DMX}/DMXHat_O.wav", note=HAT["open"])
    if cam:
        lib.mark("Oberheim DMX -- the Blue Monday machine, sampled (kb6.de archive)")
        hold(1.8)

    # TOMS: four Simmons SDS5 pads, tuned hi -> floor like a melody
    rs5k(T_TOMS, f"{SMP_SDS}/TOM4.WAV", note=TOM["hi"])
    rs5k(T_TOMS, f"{SMP_SDS}/TOM5.WAV", note=TOM["mid"])
    rs5k(T_TOMS, f"{SMP_SDS}/TOM9.WAV", note=TOM["lo"])
    rs5k(T_TOMS, f"{SMP_SDS}/TOM17.WAV", note=TOM["fl"])
    slots["tom_verb"] = lib.add_fx(T_TOMS, "ReaVerbate")["slot"]
    if cam:
        lib.mark("Simmons SDS5 -- the hexagon toms. four pads, tuned like a voice")
        hold(1.8)

    # PERC: Synare 3 + DMX color
    rs5k(T_PERC, f"{SMP_SYN}/Synare_Filter.30.wav", note=PERC["pew"])
    rs5k(T_PERC, f"{SMP_SYN}/Synare_Filter.10.wav", note=PERC["sweep"])
    rs5k(T_PERC, f"{SMP_DMX}/DMXClap.wav", note=PERC["clap"])
    rs5k(T_PERC, f"{SMP_DMX}/DMXRim.wav", note=PERC["rim"])
    rs5k(T_PERC, f"{SMP_DMX}/DMXRide.wav", note=PERC["ride"])
    rs5k(T_PERC, f"{SMP_DMX}/DMXCrash.wav", note=PERC["crash"])
    if cam:
        lib.mark("Star Instruments Synare 3 -- the electric 'pew' that answers a snare")
        hold(1.8)

    # BASS: Karplus-Strong plucked string, one sample per pitch
    for name, midi_note in BASS.items():
        rs5k(T_BASS, f"{M}/synth/bass_{name}.wav", note=midi_note,
             obey_noteoffs=True, release=0.01)
    slots["bass_comp"] = lib.add_fx(T_BASS, "ReaComp")["slot"]
    if cam:
        lib.mark("the bass: a plucked string synthesized from noise, detuned like a chorus pedal")
        hold(2.0)

    # master glue
    lib.run_lua(
        'local m = reaper.GetMasterTrack(0)\n'
        'reaper.TrackFX_AddByName(m, "ReaComp", false, -1)\n'
        'reaper.TrackFX_AddByName(m, "ReaLimit", false, -1)\n',
        name="pp_master")

    apply_mix(slots)
    return slots


def apply_mix(slots):
    lib.set_track(T_KICK, volume_db=-4.5)
    lib.set_track(T_SNARE, volume_db=-5.0)
    lib.set_track(T_HATS, volume_db=-9.5, pan=0.12)
    lib.set_track(T_TOMS, volume_db=-6.5)
    lib.set_track(T_PERC, volume_db=-7.5, pan=-0.10)
    lib.set_track(T_BASS, volume_db=-6.5)
    lib.set_track(T_PAD, volume_db=-12.0)

    if not FXMAP:
        return
    # Hannett-school snare: one 16th-note slap, dark, plus a small cold room
    set_fx_named(T_SNARE, slots["sn_dly"], "ReaDelay",
                 {"1: Length (musical)": 0.0625, "1: Length (time)": 0.0,
                  "1: Feedback": 0.14, "1: Lowpass": 0.65, "Wet": 0.18})
    set_fx_named(T_SNARE, slots["sn_verb"], "ReaVerbate",
                 {"Wet": 0.16, "Dry": 0.5, "Room size": 0.32, "Dampening": 0.5})
    set_fx_named(T_TOMS, slots["tom_verb"], "ReaVerbate",
                 {"Wet": 0.12, "Dry": 0.5, "Room size": 0.45, "Dampening": 0.45})
    set_fx_named(T_BASS, slots["bass_comp"], "ReaComp",
                 {"Threshold": 0.36, "Ratio": 0.06, "Attack": 0.004,
                  "Release": 0.03})
    lib.run_lua(
        'local m = reaper.GetMasterTrack(0)\n'
        'reaper.TrackFX_SetParamNormalized(m, 0, 0, 0.42)\n'
        'reaper.TrackFX_SetParamNormalized(m, 0, 1, 0.045)\n'
        'reaper.TrackFX_SetParamNormalized(m, 0, 2, 0.012)\n'
        'reaper.TrackFX_SetParamNormalized(m, 0, 3, 0.05)\n'
        'reaper.TrackFX_SetParamNormalized(m, 1, 0, 0.75)\n'
        'reaper.TrackFX_SetParamNormalized(m, 1, 1, 0.93)\n',
        name="pp_mfx")


# ---- the arrangement ---------------------------------------------------------


def build_song(cam=False):
    def mk(label, sec=1.5, view=None):
        if cam:
            lib.mark(label)
            hold(sec)
            if view:
                anim(view[0], view[1], 1.2)

    # ---------- S0 THE MACHINE WAKES (bars 0-4) ----------
    ramp = [Note(i * 0.25, 0.12, 60, min(125, 52 + i * 5)) for i in range(16)]
    put_midi(T_KICK, ramp
             + shift(steps(MACH, 60), 1)
             + shift(steps(MACHB, 60), 2)
             + shift(steps(MACHBRK, 60), 3), at_bar=0, bars=4)
    put_midi(T_PERC, shift(drum_bars(SN24, PERC["clap"], 2), 2), at_bar=0, bars=4)
    mk("bar 1: a sixteenth-note kick, velocity climbing -- the machine wakes")

    # ---------- S1 DRUMS ONLY (bars 4-12) ----------
    put_midi(T_KICK, drum_bars(K_A, 60, 8), at_bar=4, bars=8)
    put_midi(T_SNARE, drum_bars(SN24, 60, 8), at_bar=4, bars=8)
    put_midi(T_HATS, drum_bars(HAT16, HAT["closed"], 8), at_bar=4, bars=8)
    pews = []
    for b in range(1, 8, 2):
        pews.append(Note(b * 4 + 1.75, 0.2, PERC["pew"], 104))
        if b % 4 == 3:
            pews.append(Note(b * 4 + 3.75, 0.2, PERC["pew"], 96))
    put_midi(T_PERC, pews, at_bar=4, bars=8)
    put_midi(T_TOMS, tom_hook() + shift(tom_hook(), 2), at_bar=8, bars=4)
    mk("drums only. the kick is displaced, the Synare answers -- this IS the song")

    # ---------- S2 BASS ENTERS (bars 12-20) ----------
    put_midi(T_KICK, drum_bars(K_A, 60, 8), at_bar=12, bars=8)
    put_midi(T_SNARE, drum_bars(SN24, 60, 8), at_bar=12, bars=8)
    put_midi(T_HATS, drum_bars(HAT16, HAT["closed"], 8)
             + drum_bars(OPEN8, HAT["open"], 8), at_bar=12, bars=8)
    put_midi(T_BASS, bass_drive(8), at_bar=12, bars=8)
    put_midi(T_PERC, [Note(0, 0.3, PERC["crash"], 118)]
             + [Note(b * 4 + 1.75, 0.2, PERC["pew"], 100) for b in range(1, 8, 2)],
             at_bar=12, bars=8)
    put_midi(T_TOMS, tom_hook() + shift(cascade_half(), 3)
             + shift(tom_hook(), 4) + shift(cascade_full(), 7),
             at_bar=12, bars=8)
    mk("the bass enters: driving 8ths that dance around the kick, never on top of it")

    # ---------- S3 TRIBAL TURN (bars 20-28) ----------
    put_midi(T_KICK, drum_bars(K_TRIB, 60, 8), at_bar=20, bars=8)
    put_midi(T_TOMS, drum_bars(FLOOR8, TOM["fl"], 8)
             + shift(steps("......xX........", TOM["lo"], gate=0.2), 3)
             + shift(steps("......xX........", TOM["lo"], gate=0.2), 7),
             at_bar=20, bars=8)
    put_midi(T_PERC, drum_bars(RIM_CLAVE, PERC["rim"], 4)
             + shift(drum_bars(RIM_CLAVE2, PERC["rim"], 4), 4)
             + [Note(b * 4 + 2.0, 0.4, PERC["sweep"], 100) for b in (2, 6)],
             at_bar=20, bars=8)
    put_midi(T_HATS, [Note(b * 4 + 3.5, 0.3, HAT["open"], 88) for b in range(1, 8, 2)],
             at_bar=20, bars=8)
    put_midi(T_BASS, bass_drone(8), at_bar=20, bars=8)
    lib.create_items([
        {"track": T_PAD, "position": BAR * 24, "file": f"{M}/synth/pad_Em.wav",
         "volume_db": -3, "fade_in": 1.5, "fade_out": 1.0, "length": BAR * 4},
    ])
    mk("the tribal turn: floor tom, rim clicks, one splash. repetition is bliss")

    # ---------- S4 REBUILD (bars 28-36) ----------
    put_midi(T_KICK, drum_bars(K_A, 60, 8), at_bar=28, bars=8)
    put_midi(T_SNARE, drum_bars(SN24, 60, 8), at_bar=28, bars=8)
    put_midi(T_HATS, drum_bars(HAT16, HAT["closed"], 8)
             + drum_bars(OPEN8, HAT["open"], 8), at_bar=28, bars=8)
    put_midi(T_BASS, bass_hook(4), at_bar=28, bars=8)
    put_midi(T_TOMS, tom_hook() + shift(tom_hook(), 2)
             + shift(cascade_half(), 3)
             + shift(tom_hook(), 4) + shift(tom_hook(), 6)
             + shift(cascade_full(), 7), at_bar=28, bars=8)
    put_midi(T_PERC, [Note(0, 0.3, PERC["crash"], 118)]
             + [Note(b * 4 + 1.75, 0.2, PERC["pew"], 100) for b in range(1, 8, 2)],
             at_bar=28, bars=8)
    lib.create_items([
        {"track": T_PAD, "position": BAR * 28, "file": f"{M}/synth/pad_Em.wav",
         "volume_db": -4, "fade_in": 0.5, "fade_out": 0.4, "length": BAR * 2},
        {"track": T_PAD, "position": BAR * 30, "file": f"{M}/synth/pad_C.wav",
         "volume_db": -4, "fade_in": 0.4, "fade_out": 0.4, "length": BAR * 2},
        {"track": T_PAD, "position": BAR * 32, "file": f"{M}/synth/pad_D.wav",
         "volume_db": -4, "fade_in": 0.4, "fade_out": 0.4, "length": BAR * 2},
        {"track": T_PAD, "position": BAR * 34, "file": f"{M}/synth/pad_Em.wav",
         "volume_db": -4, "fade_in": 0.4, "fade_out": 0.4, "length": BAR * 2},
    ])
    mk("rebuild: the bass hook up high where Hooky lives, cold pads underneath")

    # ---------- S5 PEAK (bars 36-40) ----------
    put_midi(T_KICK, drum_bars(K_MOTORIK, 60, 4), at_bar=36, bars=4)
    put_midi(T_SNARE, drum_bars(SN24, 60, 4), at_bar=36, bars=4)
    put_midi(T_HATS, drum_bars(HAT16, HAT["closed"], 4), at_bar=36, bars=4)
    put_midi(T_PERC, [Note(0, 0.3, PERC["crash"], 122)]
             + drum_bars(RIDE4, PERC["ride"], 4)
             + [Note(b * 4 + 1.75, 0.2, PERC["pew"], 104) for b in (1, 3)],
             at_bar=36, bars=4)
    put_midi(T_BASS, bass_hook(2), at_bar=36, bars=4)
    put_midi(T_TOMS, tom_hook() + shift(tom_hook(), 2)[:-1]
             + shift(cascade_full(), 3), at_bar=36, bars=4)
    lib.create_items([
        {"track": T_PAD, "position": BAR * 36, "file": f"{M}/synth/pad_C.wav",
         "volume_db": -4, "fade_in": 0.4, "fade_out": 0.4, "length": BAR * 2},
        {"track": T_PAD, "position": BAR * 38, "file": f"{M}/synth/pad_D.wav",
         "volume_db": -4, "fade_in": 0.4, "fade_out": 0.4, "length": BAR * 2},
    ])
    mk("peak: motorik 8ths on the kick, ride on top, cascades every fourth bar")

    # ---------- S5b THE MACHINE RETURNS (bars 40-44) ----------
    put_midi(T_KICK, drum_bars(MACH, 60, 4), at_bar=40, bars=4)
    put_midi(T_SNARE, drum_bars(SN24, 60, 4), at_bar=40, bars=4)
    put_midi(T_PERC, [Note(0, 0.3, PERC["crash"], 124)]
             + drum_bars(SN24, PERC["clap"], 4)
             + [Note(b * 4 + 1.75, 0.2, PERC["pew"], 106) for b in range(4)],
             at_bar=40, bars=4)
    put_midi(T_BASS, bass_octaves(4), at_bar=40, bars=4)
    put_midi(T_TOMS, shift(cascade_full(), 3), at_bar=40, bars=4)
    mk("the machine returns -- 16th kick and an octave-jumping bass sequence")

    # ---------- S6 OUT (bars 44-48) ----------
    put_midi(T_KICK, drum_bars(MACH, 60, 1), at_bar=44, bars=1)
    put_midi(T_PERC, drum_bars(SN24, PERC["clap"], 1)
             + shift([Note(0, 0.4, PERC["crash"], 127)], 1), at_bar=44, bars=4)
    put_midi(T_TOMS, shift([Note(0, 0.5, TOM["fl"], 127)], 1), at_bar=44, bars=4)
    put_midi(T_BASS, shift([Note(0, 8.0, BASS["E1"], 118)], 1), at_bar=44, bars=4)
    lib.create_items([
        {"track": T_PAD, "position": BAR * 45, "file": f"{M}/synth/pad_Em.wav",
         "volume_db": -5, "fade_in": 0.1, "fade_out": 2.5, "length": BAR * 3},
    ])
    mk("one bar of machine, one last crash -- let it ring")


# ---- modes -------------------------------------------------------------------


def probe():
    """Dump the param name->index map for every FX we drive."""
    setup()
    ti = lib.add_track(name="probe")
    out = {}
    for name in ["ReaDelay", "ReaVerbate", "ReaComp", "ReaLimit"]:
        slot = lib.add_fx(ti, name)["slot"]
        f = lib.get(f"/state/tracks/{ti}/fx/{slot}?limit=40")
        out[name] = {p["name"]: p["index"] for p in f.get("params", [])}
        print(f"\n== {f.get('name')} ==")
        for p in f.get("params", []):
            print(f"  {p['index']:3d}  {p['name']:<30s} = {p['value']:.4f}")
    with open(FXMAP_PATH, "w") as fh:
        json.dump(out, fh, indent=1)
    print(f"\nwrote {FXMAP_PATH}")


def build(cam=False):
    setup()
    build_kit(cam=cam)
    build_song(cam=cam)


def render():
    build(cam=False)
    mix = f"{TMP}/mix.wav"
    if os.path.exists(mix):
        os.unlink(mix)  # /render onto an existing path raises a modal -> wedge
    r = lib.post("/render", {"output": mix, "bounds": "custom",
                             "start": 0.0, "end": TOTAL_BARS * BAR + 2.0})
    print("render:", r.get("render_seconds"), "s,", r.get("offline_ratio"), "x")
    a = lib.get(f"/analysis/file?path={mix}")
    lo, sp = a.get("loudness", {}), a.get("spectral", {})
    print(f"LUFS {lo.get('lufs_i'):.1f}  peak {lo.get('true_peak_db'):.2f}  "
          f"low {sp.get('low'):.2f} mid {sp.get('mid'):.2f} high {sp.get('high'):.2f}  "
          f"centroid {sp.get('centroid_hz'):.0f}Hz")


def perform():
    lib.reset_marks()
    lib.mark("START")
    setup()
    lib.mark("ReaClaw -- building a post-punk drum anthem, live, over HTTPS")
    hold(1.8)
    build_kit(cam=True)
    build_song(cam=True)

    total = TOTAL_BARS * BAR
    anim(-1.0, total + 2.0, 2.2)
    lib.mark("48 bars, 138 BPM, E minor. drums first; everything else is a guest")
    hold(1.4)
    lib.play_from_start()
    lib.mark("start")
    t0 = time.time()

    def at(bar, label=None):
        target = bar * BAR
        dt = target - (time.time() - t0)
        if dt > 0:
            time.sleep(dt)
        if label:
            lib.mark(label)

    at(0.1, "the machine wakes -- DMX kick, every sixteenth")
    at(4.0, "drums only: displaced kick, Synare answers. the drums ARE the song")
    at(8.0, "the Simmons hook -- three pads singing the vocal line")
    at(12.0, "bass enters: 8ths dancing with the kick")
    at(19.4)
    anim(18.5 * BAR, 30.5 * BAR, 1.0)
    at(20.0, "tribal turn -- floor tom + rim clave. repetition is bliss")
    at(27.4)
    anim(-0.5, total + 2.0, 1.0)
    at(28.0, "rebuild -- the bass hook, cold pads, cascades")
    at(36.0, "peak: motorik kick, ride, everything")
    at(40.0, "the machine returns -- octave bass vs sixteenth kick")
    at(45.0, "let it ring")
    time.sleep(max(0.0, total + 3.5 - (time.time() - t0)))
    lib.act(lib.STOP)
    lib.mark("in the school of Stephen Morris and Kevin Haskins -- drums that make music")
    hold(2.5)
    lib.mark("END")
    print(f"perform done; song {total:.1f}s; marks in /tmp/marks.txt")


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "build"
    if mode != "probe" and not FXMAP:
        print("no fx map yet -- run probe first: python3 show_postpunk.py probe")
        sys.exit(1)
    {"probe": probe, "build": build, "render": render,
     "perform": perform}[mode]()


if __name__ == "__main__":
    main()

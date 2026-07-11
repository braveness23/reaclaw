#!/usr/bin/env python3
"""show_industrial.py — THE INDUSTRIAL trailer: a late-80s electro-industrial
banger (Ministry / Skinny Puppy / KMFDM lineage) built from nothing, live, via
the ReaClaw API:

  * real drum machines: Roland TR-909 + Alesis HR-16B one-shots, loaded into
    note-filtered ReaSamplOmatic5000 banks and step-sequenced over MIDI
  * a genuine 2-operator FM bass (synthesized offline, one sample per pitch,
    played as a note-filtered RS5K bank) driven through JS distortion
  * FM power-chord stabs, machine-noise beds, voice barks, tempo-locked risers
  * gated-reverb snare, delay throws, a distortion ramp automated across the
    final wall, ReaComp+ReaLimit glue on the master

Modes:
    python3 show_industrial.py probe    # dump FX param maps (one-time recon)
    python3 show_industrial.py build    # fast full build, no camera, no holds
    python3 show_industrial.py render   # build + offline render + analysis
    python3 show_industrial.py perform  # the recorded performance
"""
import json
import os
import sys
import time

import lib
from smf import Note, write_smf

# ---- musical constants ------------------------------------------------------

BPM = 136.0
SPB = 60.0 / BPM            # seconds per beat
BAR = SPB * 4               # 1.7647s
STEP = SPB / 4              # 16th note
TOTAL_BARS = 44
PACE = float(os.environ.get("PACE", "1.0"))

M = os.path.expanduser("~/studio/projects/active/IndustrialTrailer/Media")
RPP = os.path.expanduser("~/studio/projects/active/IndustrialTrailer/IndustrialTrailer.rpp")
TMP = "/tmp/industrial"
os.makedirs(TMP, exist_ok=True)

(T_KICK, T_SNARE, T_HATS, T_METAL, T_CLAP,
 T_BASS, T_STAB, T_NOISE, T_SFX) = range(9)

TRACK_NAMES = ["Kick", "Snare", "Hats", "Metal", "Clap",
               "FM Bass", "Stabs", "Noise/Drone", "SFX/Vox"]

# rust / steel / hazard palette
TRACK_COLORS = ["#CC2222", "#EE4422", "#998877", "#AA9988", "#DD6644",
                "#66CC22", "#EEAA22", "#555555", "#AA3388"]

# MIDI pitch map (A4=440: MIDI 28 = 41.2 Hz = the E1 sample)
BASS_NOTES = {"E1": 28, "Fs1": 30, "G1": 31, "A1": 33,
              "B1": 35, "C2": 36, "D2": 38, "E2": 40}
STAB_NOTES = {"E": 52, "C": 48, "D": 50}
MET = {"pipe": 60, "wrench": 62, "trash": 64, "glass": 65, "crash": 67}
HAT = {"closed": 60, "open": 62}

# ---- helpers ---------------------------------------------------------------


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
    return lib.run_lua(lua, name=f"ind_ins_{track}")


def rs5k(track, file, note=None, obey_noteoffs=False, release=None):
    """Add an RS5K on `track`, load `file`, optionally filter to one MIDI
    note. Returns the fx slot."""
    slot = lib.add_fx(track, "ReaSamplOmatic5000")["slot"]
    lua = (
        f'local tr = reaper.GetTrack(0, {track})\n'
        f'reaper.TrackFX_SetNamedConfigParm(tr, {slot}, "FILE0", {json.dumps(file)})\n'
        f'reaper.TrackFX_SetNamedConfigParm(tr, {slot}, "DONE", "")\n'
    )
    lib.run_lua(lua, name=f"ind_rs5k_{track}_{slot}")
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
    it at bar `at_bar`. Pads to `bars` with a silent end marker note-off."""
    global _mid_seq
    _mid_seq += 1
    path = f"{TMP}/p{_mid_seq:03d}_t{track}.mid"
    # a zero-velocity pad note at the very end pins the item length to `bars`
    pad = [Note(bars * 4 - 0.01, 0.01, 127, 1)]
    write_smf(path, notes + pad, bpm=BPM)
    insert_media_at(track, path, at_bar * BAR)


# ---- step-pattern language --------------------------------------------------
# 16 chars per bar. 'X' accent 127, 'x' normal 104, 'o' soft 84, 'g' ghost 64.

VEL = {"X": 127, "x": 104, "o": 84, "g": 64}


def steps(pattern, pitch, gate=0.12):
    """pattern: string of 16*n step chars -> list of Notes (beats)."""
    out = []
    for i, ch in enumerate(pattern.replace(" ", "").replace("|", "")):
        if ch in VEL:
            out.append(Note(i * 0.25, gate, pitch, VEL[ch]))
    return out


def bass_riff(root_name, fifth_name, oct_name, bars=1):
    """The driving riff: 16ths on the root with octave bounces; last 16th
    walks on the fifth. R R O R  R O R R  R R O R  R O R 5"""
    r, o, f = BASS_NOTES[root_name], BASS_NOTES[oct_name], BASS_NOTES[fifth_name]
    seq = [r, r, o, r, r, o, r, r, r, r, o, r, r, o, r, f]
    vel = [127, 96, 110, 96, 104, 110, 96, 104, 127, 96, 110, 96, 104, 110, 96, 118]
    out = []
    for b in range(bars):
        for i in range(16):
            out.append(Note(b * 4 + i * 0.25, 0.16, seq[i], vel[i]))
    return out


# ---- the patterns -----------------------------------------------------------

K4 = "X...x...X...x..."                    # four on the floor
K4A = "X...x...X...x.x."                   # + and-of-4 push
SN = "....X.......X..."                    # 2 and 4
SNRL = "....X.......X...|ggggooooxxxxXXXX"  # bar + roll build (2 bars)
HC8 = "x.o.x.o.x.o.x.o."                   # closed 8ths, offbeat softer
HCO = "x.g.o.g.x.g.o.g."
OPEN = "..x...x...x...x."                  # open on the off-8ths
CLP = "....X.......X..."
PIPE = "......x........."                  # and-of-2
WRFILL = "..........xxxXXX"                # torque-wrench fill, back half


def drum_bars(pattern, pitch, bars, gate=0.1):
    """Tile a 1-bar pattern across `bars`."""
    out = []
    one = steps(pattern, pitch, gate)
    for b in range(bars):
        out += [Note(n.start / 480 + b * 4, n.dur / 480, n.pitch, n.vel)
                for n in one]
    return out


def shift(notes, bars):
    """Move a note list later by `bars` (for appending phrase tails)."""
    return [Note(n.start / 480 + bars * 4, n.dur / 480, n.pitch, n.vel)
            for n in notes]


def prog_bass(bars8=1):
    """One 8-bar pass of the progression: Em x4, C x2, D x2."""
    out = []
    for k in range(bars8):
        base = k * 8
        out += shift(bass_riff("E1", "B1", "E2", 4), base)
        out += shift(bass_riff("C2", "G1", "C2", 2), base + 4)
        out += shift(bass_riff("D2", "A1", "D2", 2), base + 6)
    return out


# ---- FX param maps (normalized values; validated via `probe` + renders) -----

# JS: Waveshaping Distortion — 0: amount
# JS: Distortion            — 0: gain, 1: hardness
# ReaComp — 0: thresh, 1: ratio, 2: attack, 3: release, 13: wet(dry?) ...
# ReaDelay — per-tap; 4: length(ms-ish), 9: feedback, 0: wet
# ReaVerbate — 0: wet, 1: dry, 2: room size, 3: dampening
# ReaGate — 0: thresh, 1: attack, 2: release ...
# (indices confirmed by `probe` mode output before first build)
FXP = json.load(open(f"{TMP}/fx_map.json")) if os.path.exists(f"{TMP}/fx_map.json") else {}


# ---- project setup ----------------------------------------------------------


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
    """Tracks, samplers, FX chains. Returns dict of fx slots for automation."""
    for i, n in enumerate(TRACK_NAMES):
        lib.add_track(name=n)
        lib.set_track(i, color=TRACK_COLORS[i])
    if cam:
        lib.mark("POST /state/tracks -- 9 tracks: a drum machine, an FM bass, and trouble")
        hold(1.6)

    slots = {}

    # KICK: 909 (clicky attack, tight decay) + HR-16B Techno Kick body
    rs5k(T_KICK, f"{M}/drums/tr909/BT0AAD0.WAV", note=60)
    rs5k(T_KICK, f"{M}/drums/hr16b/HR16B 005 Techno Kick.wav", note=60)
    slots["kick_dist"] = lib.add_fx(T_KICK, "waveShapingDstr")["slot"]
    if cam:
        lib.mark("Roland TR-909 + Alesis HR-16B kick, layered -- the real machines, sampled")
        hold(1.6)

    # SNARE: HR-16B Aggressive + 909 snap, distorted, gated reverb
    rs5k(T_SNARE, f"{M}/drums/hr16b/HR16B 017 Aggressive Snare.wav", note=60)
    rs5k(T_SNARE, f"{M}/drums/tr909/ST0T0SA.WAV", note=60)
    slots["sn_dist"] = lib.add_fx(T_SNARE, "waveShapingDstr")["slot"]
    slots["sn_verb"] = lib.add_fx(T_SNARE, "ReaVerbate")["slot"]
    slots["sn_gate"] = lib.add_fx(T_SNARE, "ReaGate")["slot"]
    if cam:
        lib.mark("HR-16B 'Aggressive Snare' through distortion + gated reverb -- 1989 concrete")
        hold(1.6)

    # HATS: 909 closed / open, note-split
    rs5k(T_HATS, f"{M}/drums/tr909/HHCD0.WAV", note=HAT["closed"])
    rs5k(T_HATS, f"{M}/drums/tr909/HHOD4.WAV", note=HAT["open"])

    # METAL: junkyard percussion bank
    rs5k(T_METAL, f"{M}/drums/hr16b/HR16B 039 Pipe.wav", note=MET["pipe"])
    rs5k(T_METAL, f"{M}/drums/hr16b/HR16B 044 Torque Wrench.wav", note=MET["wrench"])
    rs5k(T_METAL, f"{M}/drums/hr16b/HR16B 045 Trash Can.wav", note=MET["trash"])
    rs5k(T_METAL, f"{M}/drums/hr16b/HR16B 046 Glass Break.wav", note=MET["glass"])
    rs5k(T_METAL, f"{M}/drums/tr909/CSHD2.WAV", note=MET["crash"])
    slots["met_dly"] = lib.add_fx(T_METAL, "ReaDelay")["slot"]
    if cam:
        lib.mark("a junkyard sampler bank: pipe, torque wrench, trash can, glass -- hit metal, not toms")
        hold(1.8)

    # CLAP: HR-16B gated claps + 909 handclap
    rs5k(T_CLAP, f"{M}/drums/hr16b/HR16B 036 Gated Claps.wav", note=60)
    rs5k(T_CLAP, f"{M}/drums/tr909/HANDCLP2.WAV", note=60)

    # FM BASS: one RS5K per pitch, note-filtered -- a sampler built like a synth
    for name, midi_note in BASS_NOTES.items():
        rs5k(T_BASS, f"{M}/synth/fm_bass_{name}.wav", note=midi_note,
             obey_noteoffs=True, release=0.002)
    slots["bass_dist"] = lib.add_fx(T_BASS, "Distortion")["slot"]
    slots["bass_comp"] = lib.add_fx(T_BASS, "ReaComp")["slot"]
    if cam:
        lib.mark("the FM bass: 2-operator FM synthesis, one sample per pitch, note-filtered RS5K bank")
        hold(2.0)

    # STABS: FM power chords + delay
    for name, midi_note in STAB_NOTES.items():
        rs5k(T_STAB, f"{M}/synth/fm_stab_{name}.wav", note=midi_note)
    slots["stab_dly"] = lib.add_fx(T_STAB, "ReaDelay")["slot"]

    # master glue: ReaComp + ReaLimit via Lua (no REST verb for master FX)
    lib.run_lua(
        'local m = reaper.GetMasterTrack(0)\n'
        'reaper.TrackFX_AddByName(m, "ReaComp", false, -1)\n'
        'reaper.TrackFX_AddByName(m, "ReaLimit", false, -1)\n',
        name="ind_master")
    if cam:
        lib.mark("ReaComp + ReaLimit on the master -- glue it, then slam it")
        hold(1.6)

    apply_mix(slots)
    return slots


def apply_mix(slots):
    """Track levels + FX parameter settings (iterated against /render analysis)."""
    lib.set_track(T_KICK, volume_db=-3.5)
    lib.set_track(T_SNARE, volume_db=-6.0)
    lib.set_track(T_HATS, volume_db=-11.5)
    lib.set_track(T_METAL, volume_db=-10.0)
    lib.set_track(T_CLAP, volume_db=-9.0)
    lib.set_track(T_BASS, volume_db=-7.0)
    lib.set_track(T_STAB, volume_db=-10.0)
    lib.set_track(T_NOISE, volume_db=-12.0)
    lib.set_track(T_SFX, volume_db=-8.0)

    fxp = FXP  # measured param maps; empty until probe has run
    if not fxp:
        return

    def m(track, slot_key, js_name, wanted):
        """Set params by name using the probed map for `js_name`."""
        name_to_idx = fxp.get(js_name, {})
        mapping = {}
        for pname, val in wanted.items():
            if pname in name_to_idx:
                mapping[name_to_idx[pname]] = val
        if mapping:
            set_fx(track, slots[slot_key], mapping)

    # values chosen per render iteration -- see fx_targets in probe/render runs
    tgt = json.load(open(f"{TMP}/fx_targets.json")) if os.path.exists(f"{TMP}/fx_targets.json") else {}
    for entry in tgt.get("track_fx", []):
        set_fx(entry["track"], slots[entry["slot_key"]],
               {int(k): v for k, v in entry["params"].items()})
    for entry in tgt.get("master_fx", []):
        sets = "\n".join(
            f'reaper.TrackFX_SetParamNormalized(m, {entry["slot"]}, {int(i)}, {v})'
            for i, v in entry["params"].items())
        lib.run_lua(f'local m = reaper.GetMasterTrack(0)\n{sets}\n', name="ind_mfx")


# ---- the arrangement --------------------------------------------------------


def build_song(slots, cam=False):
    def mk(label, sec=1.5, view=None):
        if cam:
            lib.mark(label)
            hold(sec)
            if view:
                anim(view[0], view[1], 1.2)

    # ---------- S0 INTRO (bars 0-4) ----------
    lib.create_items([
        {"track": T_NOISE, "position": 0.0, "file": f"{M}/synth/fm_drone_E.wav",
         "volume_db": -6, "fade_in": 0.8, "fade_out": 1.5, "length": BAR * 4},
        {"track": T_NOISE, "position": 0.0, "file": f"{M}/sfx/noise/Noise02_m.WAV",
         "volume_db": -10, "fade_in": 2.0, "fade_out": 0.3},
        {"track": T_SFX, "position": BAR * 1.5, "file": f"{M}/sfx/voice/Voice_A02_s.WAV",
         "volume_db": -6, "fade_out": 0.2},
        {"track": T_NOISE, "position": BAR * 2, "file": f"{M}/synth/riser_2bar.wav",
         "volume_db": -8, "fade_out": 0.05},
        {"track": T_SFX, "position": BAR * 3, "file": f"{M}/synth/revswell_1bar.wav",
         "volume_db": -9},
    ])
    put_midi(T_KICK, drum_bars(K4, 60, 2), at_bar=2, bars=2)
    put_midi(T_HATS, drum_bars(HCO, HAT["closed"], 1), at_bar=3, bars=1)
    mk("an FM drone, a machine-room noise bed, a growl -- something is coming")

    # ---------- S1 DROP A (bars 4-12) ----------
    put_midi(T_KICK, drum_bars(K4, 60, 3) + shift(steps(K4A, 60), 3), at_bar=4, bars=4)
    put_midi(T_KICK, drum_bars(K4, 60, 3) + shift(steps(K4A, 60), 3), at_bar=8, bars=4)
    put_midi(T_SNARE, drum_bars(SN, 60, 8), at_bar=4, bars=8)
    put_midi(T_HATS, drum_bars(HC8, HAT["closed"], 8)
             + drum_bars(OPEN, HAT["open"], 8), at_bar=4, bars=8)
    put_midi(T_BASS, prog_bass(), at_bar=4, bars=8)
    put_midi(T_METAL, drum_bars(PIPE, MET["pipe"], 8)
             + steps("X" + "." * 15, MET["crash"])
             + shift(steps(WRFILL, MET["wrench"]), 7), at_bar=4, bars=8)
    lib.create_items([
        {"track": T_SFX, "position": BAR * 4, "file": f"{M}/synth/impact.wav",
         "volume_db": -5},
        {"track": T_SFX, "position": BAR * 6 + SPB * 3.5, "file": f"{M}/sfx/voice/Voice_A01_m.WAV",
         "volume_db": -7},
    ])
    mk("BAR 5: the beat drops -- 909 four-on-the-floor, FM bass driving 16ths, E minor like a fist",
       view=(-0.5, 24.0))

    # ---------- S2 VERSE (bars 12-20) ----------
    put_midi(T_KICK, drum_bars(K4, 60, 3) + shift(steps(K4A, 60), 3), at_bar=12, bars=4)
    put_midi(T_KICK, drum_bars(K4, 60, 3) + shift(steps(K4A, 60), 3), at_bar=16, bars=4)
    put_midi(T_SNARE, drum_bars(SN, 60, 8), at_bar=12, bars=8)
    put_midi(T_CLAP, drum_bars(CLP, 60, 8), at_bar=12, bars=8)
    put_midi(T_HATS, drum_bars(HC8, HAT["closed"], 8)
             + drum_bars(OPEN, HAT["open"], 8), at_bar=12, bars=8)
    put_midi(T_BASS, prog_bass(), at_bar=12, bars=8)
    # stabs: & of 3 each bar, chord follows the progression
    stab = []
    for b in range(8):
        chord = "E" if b < 4 else ("C" if b < 6 else "D")
        stab.append(Note(b * 4 + 2.5, 0.5, STAB_NOTES[chord], 112))
    put_midi(T_STAB, stab, at_bar=12, bars=8)
    put_midi(T_METAL, drum_bars(PIPE, MET["pipe"], 8)
             + steps("X" + "." * 15, MET["trash"]), at_bar=12, bars=8)
    lib.create_items([
        {"track": T_SFX, "position": BAR * 13 + SPB * 3.5,
         "file": f"{M}/sfx/voice/Voice_A03_m.WAV", "volume_db": -8},
        {"track": T_SFX, "position": BAR * 17 + SPB * 3.5,
         "file": f"{M}/sfx/voice/Voice_A01_m.WAV", "volume_db": -7},
    ])
    mk("verse: FM power-chord stabs on the off of 3, claps join, Em -> C -> D -- predictable on purpose",
       view=(-0.5, 44.0))

    # ---------- S3 BREAKDOWN (bars 20-24) ----------
    put_midi(T_KICK, drum_bars(K4, 60, 2), at_bar=20, bars=2)
    put_midi(T_BASS, [Note(0, 2.0, BASS_NOTES["E1"], 127),
                      Note(2, 2.0, BASS_NOTES["E1"], 110),
                      Note(4, 4.0, BASS_NOTES["E1"], 127)], at_bar=22, bars=2)
    put_midi(T_SNARE, steps("ggggoooo" + "xxxxXXXX", 60, gate=0.08), at_bar=23, bars=1)
    put_midi(T_METAL, steps("X" + "." * 15, MET["glass"]), at_bar=20, bars=1)
    lib.create_items([
        {"track": T_NOISE, "position": BAR * 20, "file": f"{M}/sfx/noise/Noise01_s.WAV",
         "volume_db": -13, "fade_in": 0.5, "fade_out": 0.5},
        {"track": T_NOISE, "position": BAR * 20, "file": f"{M}/synth/fm_drone_E.wav",
         "volume_db": -12, "fade_in": 0.3, "fade_out": 0.4, "length": BAR * 4},
        {"track": T_SFX, "position": BAR * 20 + SPB * 2, "file": f"{M}/sfx/voice/Voice_A02_s.WAV",
         "volume_db": -7},
        {"track": T_NOISE, "position": BAR * 22, "file": f"{M}/synth/riser_2bar.wav",
         "volume_db": -8},
        {"track": T_SFX, "position": BAR * 23, "file": f"{M}/synth/revswell_1bar.wav",
         "volume_db": -7},
    ])
    mk("breakdown: glass breaks, the floor drops out -- just the machines breathing")

    # ---------- S4 THE WALL (bars 24-40) ----------
    for blk in (24, 28, 32, 36):
        put_midi(T_KICK, drum_bars(K4A, 60, 4), at_bar=blk, bars=4)
        put_midi(T_HATS, drum_bars(HC8, HAT["closed"], 4)
                 + drum_bars(OPEN, HAT["open"], 4), at_bar=blk, bars=4)
    put_midi(T_SNARE, drum_bars(SN, 60, 3)
             + shift(steps("....X.....xxXXXX", 60, gate=0.08), 3), at_bar=24, bars=4)
    put_midi(T_SNARE, drum_bars(SN, 60, 4), at_bar=28, bars=4)
    put_midi(T_SNARE, drum_bars(SN, 60, 3)
             + shift(steps("....X.....xxXXXX", 60, gate=0.08), 3), at_bar=32, bars=4)
    put_midi(T_SNARE, drum_bars(SN, 60, 3)
             + shift(steps("ggggooooxxxxXXXX", 60, gate=0.08), 3), at_bar=36, bars=4)
    put_midi(T_CLAP, drum_bars(CLP, 60, 16), at_bar=24, bars=16)
    put_midi(T_BASS, prog_bass(2), at_bar=24, bars=16)
    stab = []
    for b in range(16):
        chord = "E" if (b % 8) < 4 else ("C" if (b % 8) < 6 else "D")
        stab.append(Note(b * 4, 0.6, STAB_NOTES[chord], 120))
        stab.append(Note(b * 4 + 2.5, 0.4, STAB_NOTES[chord], 104))
    put_midi(T_STAB, stab, at_bar=24, bars=16)
    put_midi(T_METAL, drum_bars(PIPE, MET["pipe"], 16)
             + steps("X" + "." * 15, MET["crash"])
             + [Note(8 * 4, 0.1, MET["crash"], 127)]
             + shift(steps(WRFILL, MET["wrench"]), 7)
             + shift(steps(WRFILL, MET["wrench"]), 15), at_bar=24, bars=16)
    lib.create_items([
        {"track": T_SFX, "position": BAR * 24, "file": f"{M}/synth/impact.wav", "volume_db": -4},
        {"track": T_SFX, "position": BAR * 27 + SPB * 3.5,
         "file": f"{M}/sfx/voice/Voice_A01_m.WAV", "volume_db": -6},
        {"track": T_SFX, "position": BAR * 31 + SPB * 3.5,
         "file": f"{M}/sfx/voice/Voice_A03_m.WAV", "volume_db": -7},
        {"track": T_SFX, "position": BAR * 35 + SPB * 3.5,
         "file": f"{M}/sfx/voice/Voice_A01_m.WAV", "volume_db": -6},
        {"track": T_NOISE, "position": BAR * 38, "file": f"{M}/synth/riser_2bar.wav",
         "volume_db": -7},
    ])
    # the distortion ramp: JS Distortion gain on the bass climbs across the wall
    lib.write_fx_param_envelope(T_BASS, slots["bass_dist"], 0,
                                [(0.0, 0.30), (BAR * 24, 0.30), (BAR * 32, 0.45),
                                 (BAR * 39, 0.62), (BAR * 40, 0.30)])
    mk("THE WALL: 16 bars, everything on -- and the bass distortion is automated to keep climbing",
       view=(-0.5, TOTAL_BARS * BAR + 2.0))

    # ---------- S5 OUT (bars 40-44) ----------
    put_midi(T_KICK, drum_bars(K4A, 60, 2)
             + [Note(8, 0.5, 60, 127), Note(9, 0.5, 60, 118),
                Note(10, 0.5, 60, 122), Note(11, 0.5, 60, 127),
                Note(12, 0.5, 60, 127)], at_bar=40, bars=4)
    put_midi(T_SNARE, drum_bars(SN, 60, 2), at_bar=40, bars=2)
    put_midi(T_HATS, drum_bars(HC8, HAT["closed"], 2)
             + drum_bars(OPEN, HAT["open"], 2), at_bar=40, bars=2)
    put_midi(T_CLAP, drum_bars(CLP, 60, 2), at_bar=40, bars=2)
    put_midi(T_BASS, bass_riff("E1", "B1", "E2", 2)
             + [Note(8, 1.0, BASS_NOTES["E1"], 127), Note(9, 1.0, BASS_NOTES["E1"], 120),
                Note(10, 1.0, BASS_NOTES["E1"], 124), Note(11, 1.0, BASS_NOTES["E1"], 127),
                Note(12, 3.5, BASS_NOTES["E1"], 127)], at_bar=40, bars=4)
    put_midi(T_STAB, [Note(8, 4.0, STAB_NOTES["E"], 127),
                      Note(12, 3.5, STAB_NOTES["E"], 127)], at_bar=40, bars=4)
    put_midi(T_METAL, [Note(12, 0.1, MET["crash"], 127)], at_bar=40, bars=4)
    lib.create_items([
        {"track": T_SFX, "position": BAR * 43, "file": f"{M}/sfx/bigbang/Hit21_s.WAV",
         "volume_db": -4, "fade_out": 0.8},
        {"track": T_SFX, "position": BAR * 43, "file": f"{M}/synth/impact.wav",
         "volume_db": -4},
    ])
    mk("out: quarter-note unison slams, one last crash, and the machines power down")


# ---- modes ------------------------------------------------------------------


def probe():
    """One-time: dump the param name->index map for every FX we drive."""
    setup()
    ti = lib.add_track(name="probe")
    out = {}
    for name in ["waveShapingDstr", "Distortion", "ReaComp", "ReaDelay",
                 "ReaVerbate", "ReaGate", "ReaLimit"]:
        slot = lib.add_fx(ti, name)["slot"]
        f = lib.get(f"/state/tracks/{ti}/fx/{slot}?limit=40")
        out[name] = {p["name"]: p["index"] for p in f.get("params", [])}
        print(f"\n== {f.get('name')} ==")
        for p in f.get("params", []):
            print(f"  {p['index']:3d}  {p['name']:<30s} = {p['value']:.4f}")
    with open(f"{TMP}/fx_map.json", "w") as fh:
        json.dump(out, fh, indent=1)
    print(f"\nwrote {TMP}/fx_map.json")


def build(cam=False):
    setup()
    slots = build_kit(cam=cam)
    build_song(slots, cam=cam)
    return slots


def render():
    build(cam=False)
    r = lib.post("/render", {"output": f"{TMP}/mix.wav", "bounds": "custom",
                             "start": 0.0, "end": TOTAL_BARS * BAR + 3.0})
    print("render:", r.get("render_seconds"), "s,", r.get("offline_ratio"), "x")
    a = lib.get(f"/analysis/file?path={TMP}/mix.wav")
    lo, sp = a.get("loudness", {}), a.get("spectral", {})
    print(f"LUFS {lo.get('lufs_i'):.1f}  peak {lo.get('true_peak_db'):.2f}  "
          f"low {sp.get('low'):.2f} mid {sp.get('mid'):.2f} high {sp.get('high'):.2f}  "
          f"centroid {sp.get('centroid_hz'):.0f}Hz")


def perform():
    lib.reset_marks()
    lib.mark("START")
    setup()
    lib.mark("ReaClaw -- building an industrial banger, live, over HTTPS")
    hold(1.8)
    slots = build_kit(cam=True)
    build_song(slots, cam=True)

    total = TOTAL_BARS * BAR
    anim(-1.0, total + 3.0, 2.2)
    lib.mark("44 bars, 136 BPM, E minor. your mother will hate this.")
    hold(1.4)
    lib.play_from_start()
    lib.mark("start")
    # section captions during playback (timed against real playback)
    t0 = time.time()

    def at(bar, label):
        target = bar * BAR
        dt = target - (time.time() - t0)
        if dt > 0:
            time.sleep(dt)
        lib.mark(label)

    at(0.2, "intro -- drone + machine noise")
    at(4.0, "DROP -- TR-909 + HR-16B + FM bass")
    at(12.0, "verse -- stabs on the off-beat")
    at(20.0, "breakdown -- the floor drops out")
    at(24.0, "THE WALL -- distortion ramp engaged")
    at(40.0, "unison slams -- out")
    time.sleep(max(0.0, total + 4.0 - (time.time() - t0)))
    lib.act(lib.STOP)
    lib.mark("built by an AI agent over a REST API. REAPER did the rest.")
    hold(2.5)
    lib.mark("END")
    print(f"perform done; song {total:.1f}s; marks in /tmp/marks.txt")


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "build"
    {"probe": probe, "build": build, "render": render,
     "perform": perform}[mode]()


if __name__ == "__main__":
    main()

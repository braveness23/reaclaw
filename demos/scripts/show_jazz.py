#!/usr/bin/env python3
"""show_jazz.py — the CHILL JAZZ trailer: a full band assembled live from
loop libraries (Sony ACID "On The Jazz Tip", Vince Andrews horns), a BBC
Sound Effects Library jazz-club atmosphere bed, and light audience applause
after each "solo" -- built track by track on camera via POST /state/items,
then played back in real time so it's heard building from nothing.

Two phases so the recording has no dead setup time:
    python3 show_jazz.py prepare   # sanity check only (NOT recorded)
    python3 show_jazz.py perform   # the recorded performance: live build + playback

Marks (lib.mark) drop caption cues for post_jazz.py.
"""
import os
import sys
import time

import lib

BPM = 100.0
SPB = 60.0 / BPM
BAR = SPB * 4
PHRASE = BAR * 4  # 9.6s @ 100bpm 4/4

PACE = float(os.environ.get("PACE", "1.0"))

M = os.path.expanduser("~/studio/projects/active/ChillJazzTrailer/Media")

(T_AMB, T_BRUSH, T_BASS, T_DRUMS, T_KEYS,
 T_HSTAB, T_HCOMP, T_HSOLO, T_APPL) = range(9)

TRACK_NAMES = [
    "Ambience - Jazz Club", "Brushes", "Bass", "Drums", "Keys - Chord Stabs",
    "Horns - Stabs", "Horns - Comping", "Horns - Solo", "Applause",
]


def hold(sec):
    time.sleep(sec * PACE)


def anim(a1, b1, dur):
    lib.animate_view_to(a1, b1, dur * PACE)


# ---- item helper (mirrors the one-shot build script) -----------------------

def add(track, file, position, length=None, volume_db=0.0,
        fade_in=0.02, fade_out=0.02):
    d = {"track": track, "position": round(position, 3), "file": file,
         "volume_db": volume_db, "fade_in": fade_in, "fade_out": fade_out}
    if length is not None:
        d["length"] = round(length, 3)
    return d


def create(specs):
    return lib.create_items(specs)


# ---- PREPARE (not recorded) -------------------------------------------------

def prepare():
    needed = [
        f"{M}/ambience/PublicHouse_Atmosphere.mp3",
        f"{M}/drums/Drumbeat_32-01.wav", f"{M}/bass/Bass_29-01.wav",
        f"{M}/horns/Tenor_Solo_03.wav", f"{M}/horns/Trumpet_Solo_05.wav",
        f"{M}/applause/Applause_Light_A.wav", f"{M}/applause/Applause_Light_B.wav",
        f"{M}/applause/Applause_Finale_Trim.wav",
    ]
    missing = [p for p in needed if not os.path.exists(p)]
    if missing:
        sys.exit(f"missing media: {missing}")
    lib.get("/capabilities")  # connectivity check
    print("prepare: media present, ReaClaw reachable")


# ---- PERFORM (recorded) -----------------------------------------------------

def setup():
    lib.act(lib.STOP)
    lib.clear_project()
    lib.set_tempo(0, BPM)
    lib.disable_follow()
    lib._ensure_view_applier()
    lib.set_view(-0.5, 14.0)


def build_tracks():
    lib.mark("ReaClaw -- building a chill jazz trailer, live")
    hold(1.0)
    idxs = [lib.add_track(name=n) for n in TRACK_NAMES]
    assert idxs == list(range(9)), f"track order drifted: {idxs}"
    lib.mark("POST /state/tracks -- 9 tracks: ambience, rhythm section, horns, applause")
    hold(1.5)


def build_arrangement():
    t = 0.0

    def adv(n=1.0):
        nonlocal t
        t += PHRASE * n

    # P0 ambience alone
    p0 = t
    create([add(T_AMB, f"{M}/ambience/PublicHouse_Atmosphere.mp3", p0,
                length=165.0, volume_db=-14, fade_in=6.0, fade_out=10.0)])
    lib.mark("BBC Sound Effects Library -- jazz-club room tone, starting from nothing")
    hold(1.8)
    adv()

    # P1 brushes enter
    p1 = t
    create([add(T_BRUSH, f"{M}/perc/Brush_74-0{1 if k % 2 == 0 else 3}.wav", p1 + off,
                volume_db=-9, fade_in=0.005, fade_out=0.05)
            for k, off in enumerate([0.6, 2.4, 4.2, 6.6, 8.4])])
    lib.mark("brushes enter -- sparse, off the top")
    hold(1.2)
    anim(-0.5, 22.0, 1.2)
    adv()

    # P2 bass enters
    p2 = t
    create([add(T_BASS, f"{M}/bass/Bass_29-01.wav", p2, length=PHRASE, volume_db=-6)])
    create([add(T_BRUSH, f"{M}/perc/Brush_74-0{1 if k % 2 == 0 else 3}.wav", p2 + off,
                volume_db=-9, fade_in=0.005, fade_out=0.05)
            for k, off in enumerate([0.6, 2.4, 4.2, 6.6, 8.4])])
    lib.mark("Sony ACID 'On The Jazz Tip' -- walking bass loop, tempo-matched (99.4bpm ~ 100bpm)")
    hold(1.5)
    adv()

    # P3 drums enter -> full rhythm section
    p3 = t
    create([add(T_DRUMS, f"{M}/drums/Drumbeat_32-01.wav", p3, length=PHRASE, volume_db=-4)])
    create([add(T_BASS, f"{M}/bass/Bass_29-01.wav", p3, length=PHRASE, volume_db=-4)])
    create([add(T_BRUSH, f"{M}/perc/Brush_74-0{1 if k % 2 == 0 else 3}.wav", p3 + off,
                volume_db=-10, fade_in=0.005, fade_out=0.05)
            for k, off in enumerate([0.6, 2.4, 4.2, 6.6, 8.4])])
    lib.mark("drums lock in -- groove established, 4-on-the-floor bar-length loops tiled by hand")
    hold(1.6)
    anim(-1.0, 45.0, 1.4)
    adv()

    # P4 chord stab + horn stabs begin
    p4 = t
    create([add(T_KEYS, f"{M}/keys/KeyChordsMajor_60-01.wav", p4, volume_db=-7, fade_out=1.2)])
    create([add(T_DRUMS, f"{M}/drums/Drumbeat_32-01.wav", p4, length=PHRASE, volume_db=-4)])
    create([add(T_BASS, f"{M}/bass/Bass_29-01.wav", p4, length=PHRASE, volume_db=-4)])
    create([add(T_HSTAB, f"{M}/horns/SaxSection_C_01.wav", p4 + 4.8, volume_db=-6)])
    lib.mark("chord stab announces the head -- horn section punctuation begins")
    hold(1.4)
    adv()

    # P5-6 HEAD: horn comping over full band
    p5 = t
    for i in range(2):
        create([add(T_DRUMS, f"{M}/drums/Drumbeat_32-01.wav", p5 + i * PHRASE, length=PHRASE, volume_db=-4)])
        create([add(T_BASS, f"{M}/bass/Bass_29-01.wav", p5 + i * PHRASE, length=PHRASE, volume_db=-4)])
    create([add(T_HCOMP, f"{M}/horns/Tenor_RnB_Cmaj_01.wav", p5, volume_db=-5, fade_out=0.3)])
    create([add(T_HCOMP, f"{M}/horns/Alto_RnB_Cmaj_01.wav", p5 + PHRASE, volume_db=-6, fade_out=0.3)])
    create([add(T_HSTAB, f"{M}/horns/SaxSection_C7_01.wav", p5 + PHRASE + 4.8, volume_db=-6)])
    lib.mark("Vince Andrews horns -- tenor then alto comping, the tune states itself")
    hold(1.8)
    anim(-1.0, 90.0, 1.6)
    adv(2)

    # P7 thin-down into solo space
    p7 = t
    create([add(T_DRUMS, f"{M}/drums/Drumbeat_32-01.wav", p7, length=PHRASE, volume_db=-7, fade_out=1.5)])
    create([add(T_BASS, f"{M}/bass/Bass_29-01.wav", p7, length=PHRASE, volume_db=-5)])
    create([add(T_HSTAB, f"{M}/horns/SaxSection_C_01.wav", p7, volume_db=-9, fade_out=0.5)])
    lib.mark("thinning the backing -- making room for a solo")
    hold(1.2)
    adv()

    # SOLO 1 - TENOR
    solo1 = t
    create([add(T_BASS, f"{M}/bass/Bass_29-01.wav", solo1, length=PHRASE * 2, volume_db=-8)])
    create([add(T_BRUSH, f"{M}/perc/Brush_74-0{1 if k % 2 == 0 else 3}.wav", solo1 + off,
                volume_db=-11, fade_in=0.005, fade_out=0.05)
            for k, off in enumerate([0.6, 2.4, 4.2, 6.6, 8.4, 10.8, 12.6, 14.4, 16.8, 18.6])])
    create([add(T_HSOLO, f"{M}/horns/Tenor_Solo_03.wav", solo1 + 0.4, volume_db=-3, fade_in=0.05, fade_out=0.4)])
    lib.mark("SOLO 1 -- tenor sax steps out, backing drops to bass + brushes")
    hold(1.8)
    adv(2)

    # APPLAUSE 1
    appl1 = t - 1.0
    create([add(T_APPL, f"{M}/applause/Applause_Light_A.wav", appl1, volume_db=-10, fade_in=0.3, fade_out=1.8)])
    lib.mark("BBC Sound Effects Library -- light applause after the solo")
    hold(1.2)

    # TRANSITION 2
    tr2 = t
    create([add(T_BASS, f"{M}/bass/Bass_29-01.wav", tr2, length=PHRASE, volume_db=-8)])
    create([add(T_HSTAB, f"{M}/horns/SaxSection_C7_01.wav", tr2 + 0.6, volume_db=-8)])
    create([add(T_BRUSH, f"{M}/perc/Brush_74-0{1 if k % 2 == 0 else 3}.wav", tr2 + off,
                volume_db=-11, fade_in=0.005, fade_out=0.05)
            for k, off in enumerate([0.6, 2.4, 4.2, 6.6, 8.4])])
    lib.mark("back to the pocket for one more turn")
    hold(1.0)
    anim(-1.0, 160.0, 1.6)
    adv()

    # SOLO 2 - TRUMPET
    solo2 = t
    create([add(T_BASS, f"{M}/bass/Bass_29-01.wav", solo2, length=PHRASE * 2, volume_db=-8)])
    create([add(T_BRUSH, f"{M}/perc/Brush_74-0{1 if k % 2 == 0 else 3}.wav", solo2 + off,
                volume_db=-11, fade_in=0.005, fade_out=0.05)
            for k, off in enumerate([0.6, 2.4, 4.2, 6.6, 8.4, 10.8, 12.6, 14.4, 16.8, 18.6])])
    create([add(T_HSOLO, f"{M}/horns/Trumpet_Solo_05.wav", solo2 + 0.4, volume_db=-3, fade_in=0.05, fade_out=0.4)])
    lib.mark("SOLO 2 -- trumpet takes the second chorus")
    hold(1.6)
    adv(2)

    # APPLAUSE 2
    appl2 = t - 1.0
    create([add(T_APPL, f"{M}/applause/Applause_Light_B.wav", appl2, volume_db=-10, fade_in=0.3, fade_out=1.8)])
    lib.mark("applause again -- the room is with the band")
    hold(1.0)

    # OUT HEAD
    outh = t
    create([add(T_DRUMS, f"{M}/drums/Drumbeat_32-01.wav", outh, length=PHRASE, volume_db=-4)])
    create([add(T_BASS, f"{M}/bass/Bass_29-01.wav", outh, length=PHRASE, volume_db=-4)])
    create([add(T_HSTAB, f"{M}/horns/SaxSection_C_01.wav", outh, volume_db=-6)])
    create([add(T_HSTAB, f"{M}/horns/SaxSection_C7_01.wav", outh + 4.8, volume_db=-6)])
    create([add(T_HCOMP, f"{M}/horns/Alto_RnB_Cmaj_01.wav", outh, volume_db=-6, fade_out=0.3)])
    lib.mark("full band restates the head -- heading out")
    hold(1.2)
    adv()

    # OUTRO thin-down
    outr = t
    create([add(T_DRUMS, f"{M}/drums/Drumbeat_32-01.wav", outr, length=PHRASE * 0.5, volume_db=-6, fade_out=1.5)])
    create([add(T_BASS, f"{M}/bass/Bass_29-01.wav", outr, length=PHRASE, volume_db=-6, fade_out=3.0)])
    create([add(T_KEYS, f"{M}/keys/KeyChordsMinor_60-02.wav", outr + 3.6, volume_db=-8, fade_out=2.5)])
    lib.mark("thinning back down -- mirrors the way in")
    hold(1.2)
    adv()

    # FINALE
    finale = t
    create([add(T_APPL, f"{M}/applause/Applause_Finale_Trim.wav", finale, volume_db=-6, fade_in=2.0, fade_out=3.5)])
    lib.mark("the jazz club erupts -- big finale applause")
    hold(1.0)
    adv()

    return t  # total length


def stereo_width():
    lib.add_fx(T_AMB, "JS: Stereo Width")
    lib.post(f"/state/tracks/{T_AMB}/fx/1",
              {"enabled": True, "params": [{"index": 0, "value": 0.625}, {"index": 1, "value": 0.425}]})
    lib.mark("JS: Stereo Width on the ambience bed -- true stereo jazz-club room")
    hold(1.2)


def playback(total):
    anim(-1.0, total + 2.0, 2.0)
    lib.mark("full band built from nothing, live -- now let it play")
    hold(1.0)
    lib.play_from_start()
    lib.mark("start")
    hold(total * PACE + 3.0)
    lib.act(lib.STOP)


def perform():
    lib.reset_marks()
    lib.mark("START")
    setup()
    build_tracks()
    total = build_arrangement()
    stereo_width()
    playback(total)
    lib.mark("no re-recording, no synths -- loop libraries, tempo-matched and arranged live")
    hold(2.5)
    lib.mark("END")
    print(f"perform done; total arrangement {total:.1f}s; marks in /tmp/marks.txt")


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

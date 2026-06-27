#!/usr/bin/env python3
"""icons_show.py — build a 24-track session entirely from track icons.

A "no-audio" demo: shows ReaClaw assigning a fitting factory track icon (and a
folder colour) to every track in a full-band production template, in ONE batch
`POST /state/tracks` call. The payoff is purely visual — open REAPER's track
panel (or the MCP, or a screenshot) and every track wears the right icon.

Pairs with the audio trailer (show.py); this one is short, deterministic, and
needs no MIDI, no instruments, and no recording — just the icon write path
added in #29 (P_ICON read/write + GET /state/track-icons discovery).

Run:  (REAPER up with ReaClaw on port 9091)
    python3 icons_show.py

It will:
  1. discover the installed icon set (GET /state/track-icons),
  2. clear the project,
  3. batch-create 24 tracks with name + colour + folder_depth + icon,
  4. read them back and verify every icon resolved (no icon_not_found hints).
"""
import sys
import lib

# Folder colours (one per section) — REAPER colours the folder + bleeds to kids.
RED, PURPLE, ORANGE, BLUE, GREEN, GOLD, TEAL = (
    "#C0392B", "#8E44AD", "#E67E22", "#2980B9", "#27AE60", "#F1C40F", "#16A085",
)

# 24 tracks: (name, icon, folder_depth, color)
#   folder_depth  1 = this track opens a folder
#   folder_depth -1 = this track closes the current folder (last child)
#   folder_depth  0 = a plain track / child
TRACKS = [
    # — DRUMS folder (6) —
    ("DRUMS",       "drums.png",      1,  RED),
    ("Kick",        "kick.png",       0,  None),
    ("Snare",       "snare_top.png",  0,  None),
    ("Hi-Hat",      "hihat.png",      0,  None),
    ("Toms",        "tom.png",        0,  None),
    ("Overheads",   "overheads.png", -1,  None),
    # — BASS folder (2) —
    ("BASS",        "bass.png",       1,  PURPLE),
    ("Synth Bass",  "synthbass.png", -1,  None),
    # — GUITARS folder (4) —
    ("GUITARS",     "guitar.png",     1,  ORANGE),
    ("Rhythm Gtr",  "guitar2.png",    0,  None),
    ("Lead Gtr",    "guitar3.png",    0,  None),
    ("Acoustic",    "ac_guitar.png", -1,  None),
    # — KEYS folder (5) —
    ("KEYS",        "piano.png",      1,  BLUE),
    ("Grand Piano", "piano.png",      0,  None),
    ("Organ",       "organ.png",      0,  None),
    ("Synth",       "synth.png",      0,  None),
    ("Pads",        "pads.png",      -1,  None),
    # — STRINGS folder (3) —
    ("STRINGS",     "violin.png",     1,  GREEN),
    ("Cello",       "cello.png",      0,  None),
    ("Harp",        "harp.png",      -1,  None),
    # — HORNS folder (3) —
    ("HORNS",       "trumpet.png",    1,  GOLD),
    ("Sax",         "sax.png",        0,  None),
    ("Trombone",    "trombone.png",  -1,  None),
    # — VOCALS (1, plain) —
    ("Lead Vox",    "mic.png",        0,  TEAL),
]


def main():
    assert len(TRACKS) == 24, f"expected 24 tracks, have {len(TRACKS)}"

    # 1. Discover the icon set so we only ask for icons that exist on this rig.
    avail = set(lib.get("/state/track-icons").get("icons", []))
    print(f"discovered {len(avail)} installed track icons")
    missing = sorted({ico for _, ico, _, _ in TRACKS} - avail) if avail else []
    if missing:
        print(f"WARNING: rig is missing these icons (will warn, not fail): {missing}")

    # 2. Clean slate.
    lib.act(lib.STOP)
    lib.clear_project()

    # 3. One batch call creates all 24 tracks with their icons/colours/folders.
    specs = []
    for name, icon, fd, color in TRACKS:
        spec = {"name": name, "icon": icon, "folder_depth": fd}
        if color:
            spec["color"] = color
        specs.append(spec)
    resp = lib.post("/state/tracks", {"create": specs})
    created = resp.get("created", [])
    print(f"created {len(created)} tracks in one POST /state/tracks call")

    # Surface any icon_not_found hints the server attached.
    hinted = [c for c in created if c.get("hints")]
    for c in hinted:
        for h in c["hints"]:
            print(f"  hint on '{c.get('name')}': {h.get('code')} — {h.get('message')}")

    # 4. Read back and verify every icon stuck. On read REAPER resolves the
    #    relative name to an absolute path under Data/track_icons, so we report
    #    the basename.
    import os
    tracks = lib.get("/state/tracks").get("tracks", [])
    print("\n  idx  icon                 name")
    print("  ---  -------------------  --------------------")
    ok = 0
    for t in tracks:
        ico = t.get("icon")
        if ico:
            ok += 1
        base = os.path.basename(ico) if ico else None
        print(f"  {t.get('index'):>3}  {str(base):<19}  {t.get('name')}")
    print(f"\n{ok}/{len(tracks)} tracks carry an icon")
    return 0 if (ok == 24 and not hinted) else 1


if __name__ == "__main__":
    sys.exit(main())

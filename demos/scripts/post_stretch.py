#!/usr/bin/env python3
"""post_stretch.py — finish the STRETCH-MARKER trailer: burn captions + cards.

Same pipeline as post_zoom.py / post_fxauto.py: each caption holds until the
next one starts (clamped), text goes through textfile= to dodge ffmpeg
drawtext's multibyte-glyph width bug and argv quote-escaping.

Reads /tmp/rec_start.txt + /tmp/marks.txt (see lib.mark / record.sh).
Output: $OUT (default ~/reaclaw_stretch_trailer.mp4).
"""
import os
import subprocess

RAW = os.environ.get("RAW", "/tmp/raw_stretch.mp4")
OUT = os.environ.get("OUT", os.path.expanduser("~/reaclaw_stretch_trailer.mp4"))
WORK = "/tmp/trailer_stretch_build"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

TRIM = float(os.environ.get("TRIM", "0"))
MIN_HOLD = 2.0
MAX_HOLD = 7.0

VENC = ["-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p",
        "-crf", "20", "-r", "24"]
AENC = ["-c:a", "aac", "-b:a", "192k", "-ar", "44100"]
SIZE = "1920x1080"

SKIP = {"START", "END"}


def sh(*args):
    print("+", " ".join(args))
    subprocess.run(args, check=True)


def load_marks():
    with open("/tmp/rec_start.txt") as f:
        start = float(f.read().strip())
    raw = []
    with open("/tmp/marks.txt") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or "\t" not in line:
                continue
            epoch, label = line.split("\t", 1)
            if label in SKIP:
                continue
            t = float(epoch) - start - TRIM
            if t >= 0:
                raw.append((t, label))
    raw.sort()
    marks = []
    for i, (t, label) in enumerate(raw):
        nxt = raw[i + 1][0] if i + 1 < len(raw) else t + MAX_HOLD
        end = min(t + MAX_HOLD, max(t + MIN_HOLD, nxt))
        marks.append((t, end, label))
    return marks


def fold(text):
    for uni, asc in (("—", "-"), ("–", "-"), ("’", "'"), ("‘", "'"),
                     ("“", '"'), ("”", '"'), ("…", "...")):
        text = text.replace(uni, asc)
    return text


def caption_filter(marks):
    draws = []
    for i, (t, end, label) in enumerate(marks):
        cap = f"{WORK}/cap{i}.txt"
        with open(cap, "w") as f:
            f.write(fold(label))
        draws.append(
            f"drawtext=fontfile={FONT}:textfile={cap}:"
            f"fontcolor=white:fontsize=42:box=1:boxcolor=black@0.58:boxborderw=20:"
            f"x=(w-text_w)/2:y=h-150:enable='between(t,{t:.2f},{end:.2f})'"
        )
    return ",".join(draws)


def make_body(marks):
    out = f"{WORK}/body.mp4"
    vf = caption_filter(marks)
    args = ["ffmpeg", "-y", "-hide_banner"]
    if TRIM > 0:
        args += ["-ss", str(TRIM)]
    args += ["-i", RAW]
    if vf:
        args += ["-vf", vf]
    args += VENC + AENC + [out]
    sh(*args)
    return out


def fit_size(text, base=130, max_w=1500):
    est = len(text) * base * 0.66
    return base if est <= max_w else max(36, int(base * max_w / est))


def make_card(path, line1, line2="", seconds=3.0):
    base = os.path.basename(path)
    fs = fit_size(line1)
    p1 = f"{WORK}/{base}.l1.txt"
    with open(p1, "w") as f:
        f.write(fold(line1))
    vf = (f"drawtext=fontfile={FONT}:textfile={p1}:fontcolor=white:"
          f"fontsize={fs}:x=(w-text_w)/2:y=(h-text_h)/2-40")
    if line2:
        p2 = f"{WORK}/{base}.l2.txt"
        with open(p2, "w") as f:
            f.write(fold(line2))
        vf += (f",drawtext=fontfile={FONT}:textfile={p2}:fontcolor=white@0.82:"
               f"fontsize=46:x=(w-text_w)/2:y=(h-text_h)/2+100")
    sh("ffmpeg", "-y", "-hide_banner",
       "-f", "lavfi", "-i", f"color=c=0x0d1117:s={SIZE}:d={seconds}",
       "-f", "lavfi", "-i", "anullsrc=channel_layout=stereo:sample_rate=44100",
       "-shortest", "-vf", vf, *VENC, *AENC, path)
    return path


def main():
    os.makedirs(WORK, exist_ok=True)
    marks = load_marks()
    print(f"{len(marks)} captions")

    title = make_card(f"{WORK}/title.mp4", "ReaClaw",
                      "Stretch Markers -- fixing a drummer's timing, live", 4.0)
    body = make_body(marks)
    end = make_card(f"{WORK}/end.mp4", "ReaClaw",
                    "github.com/braveness23/reaclaw", 3.5)

    listfile = f"{WORK}/concat.txt"
    with open(listfile, "w") as f:
        for p in (title, body, end):
            f.write(f"file '{p}'\n")

    sh("ffmpeg", "-y", "-hide_banner", "-f", "concat", "-safe", "0",
       "-i", listfile, "-c", "copy", OUT)
    print(f"\ndone -> {OUT}")


if __name__ == "__main__":
    main()

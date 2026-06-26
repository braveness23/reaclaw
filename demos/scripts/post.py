#!/usr/bin/env python3
"""post.py — turn raw.mp4 into the finished captioned trailer.

Pipeline:
  1. Read /tmp/rec_start.txt (epoch when record.sh started) and /tmp/marks.txt
     (epoch <TAB> label per choreography step). Subtract to get each caption's
     video-time offset. Optionally apply a TRIM offset if you cut dead air off
     the head of raw.mp4 first.
  2. Burn lower-third captions onto the body clip with one drawtext per mark,
     each gated by enable='between(t,START,END)'.
  3. Build a title card and an end card as lavfi color= video + anullsrc audio,
     using IDENTICAL encode params to the body so concat -c copy is safe.
  4. Concat title + body + end via the concat demuxer.

Output: ~/trailer.mp4 (override with OUT=...)

Requires: ffmpeg with libx264, drawtext (libfreetype), aac.
"""
import os
import subprocess

RAW = os.environ.get("RAW", "/tmp/raw.mp4")
OUT = os.environ.get("OUT", os.path.expanduser("~/trailer.mp4"))
WORK = "/tmp/trailer_build"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

TRIM = float(os.environ.get("TRIM", "0"))   # seconds trimmed off head of RAW
CAPTION_HOLD = 3.0                           # seconds each caption stays up

# Encode params shared by EVERY clip so concat -c copy works.
VENC = ["-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p",
        "-crf", "20", "-r", "24"]
AENC = ["-c:a", "aac", "-b:a", "192k", "-ar", "44100"]
SIZE = "1920x1080"

TITLE_TEXT = "ReaClaw"
TITLE_SUB = "an AI-native REST API inside REAPER"
END_TEXT = "ReaClaw"
END_SUB = "github.com/braveness23/reaclaw"


def sh(*args):
    print("+", " ".join(args))
    subprocess.run(args, check=True)


def load_marks():
    with open("/tmp/rec_start.txt") as f:
        start = float(f.read().strip())
    marks = []
    with open("/tmp/marks.txt") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or "\t" not in line:
                continue
            epoch, label = line.split("\t", 1)
            t = float(epoch) - start - TRIM
            if t >= 0:
                marks.append((t, label))
    return marks


def esc(text):
    """Escape text for ffmpeg drawtext."""
    return (text.replace("\\", "\\\\").replace(":", "\\:")
                .replace("'", "’").replace("%", "\\%"))


def caption_filter(marks):
    draws = []
    for t, label in marks:
        draws.append(
            f"drawtext=fontfile={FONT}:text='{esc(label)}':"
            f"fontcolor=white:fontsize=40:box=1:boxcolor=black@0.55:boxborderw=18:"
            f"x=(w-text_w)/2:y=h-140:enable='between(t,{t:.2f},{t + CAPTION_HOLD:.2f})'"
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


def fit_size(text, base=110, max_w=1500):
    """Shrink the font so `text` fits within max_w px. DejaVu Bold averages
    ~0.66*size per char (em-dash/spaces are wide), so estimate conservatively."""
    est = len(text) * base * 0.66
    return base if est <= max_w else max(36, int(base * max_w / est))


def make_card(path, line1, line2="", seconds=2.5):
    fs = fit_size(line1)
    vf = (f"drawtext=fontfile={FONT}:text='{esc(line1)}':fontcolor=white:"
          f"fontsize={fs}:x=(w-text_w)/2:y=(h-text_h)/2-40")
    if line2:
        vf += (f",drawtext=fontfile={FONT}:text='{esc(line2)}':fontcolor=white@0.8:"
               f"fontsize=44:x=(w-text_w)/2:y=(h-text_h)/2+90")
    sh("ffmpeg", "-y", "-hide_banner",
       "-f", "lavfi", "-i", f"color=c=0x101418:s={SIZE}:d={seconds}",
       "-f", "lavfi", "-i", f"anullsrc=channel_layout=stereo:sample_rate=44100",
       "-shortest", "-vf", vf, *VENC, *AENC, path)
    return path


def main():
    os.makedirs(WORK, exist_ok=True)
    marks = load_marks()
    print(f"{len(marks)} captions")

    title = make_card(f"{WORK}/title.mp4", TITLE_TEXT, TITLE_SUB, 3.0)
    body = make_body(marks)
    end = make_card(f"{WORK}/end.mp4", END_TEXT, END_SUB, 3.0)

    listfile = f"{WORK}/concat.txt"
    with open(listfile, "w") as f:
        for p in (title, body, end):
            f.write(f"file '{p}'\n")

    sh("ffmpeg", "-y", "-hide_banner", "-f", "concat", "-safe", "0",
       "-i", listfile, "-c", "copy", OUT)
    print(f"\ndone -> {OUT}")


if __name__ == "__main__":
    main()

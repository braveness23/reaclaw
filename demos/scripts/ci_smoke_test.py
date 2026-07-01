#!/usr/bin/env python3
"""ci_smoke_test.py — headless render E2E smoke test (issue #36).

Assumes a REAPER + ReaClaw instance is already running (the CI workflow
starts one under Xvfb with a dummy/no audio device; for local runs, point it
at the usual dev rig). Builds a tiny composition via the API, renders it
offline, and asserts the output is a real, non-silent audio file using
ReaClaw's own analysis endpoint — no ffmpeg dependency.

Env vars (all optional):
    REACLAW_BASE   default "https://127.0.0.1:9091"
    REACLAW_KEY    default "sk_change_me"

Exit code 0 on success, 1 on any assertion/connection failure (message on
stderr explaining what failed).
"""
import json
import math
import os
import ssl
import struct
import sys
import tempfile
import time
import urllib.error
import urllib.request

BASE = os.environ.get("REACLAW_BASE", "https://127.0.0.1:9091")
KEY = os.environ.get("REACLAW_KEY", "sk_change_me")

_CTX = ssl.create_default_context()
_CTX.check_hostname = False
_CTX.verify_mode = ssl.CERT_NONE

TONE_SECONDS = 2.0
TONE_HZ = 440
SAMPLE_RATE = 44100


def _req(method, path, body=None, timeout=30):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(BASE + path, data=data, method=method)
    req.add_header("Authorization", "Bearer " + KEY)
    if data is not None:
        req.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(req, context=_CTX, timeout=timeout) as r:
        raw = r.read().decode()
    return json.loads(raw) if raw else {}


def get(path, timeout=30):
    return _req("GET", path, timeout=timeout)


def post(path, body=None, timeout=30):
    return _req("POST", path, body if body is not None else {}, timeout=timeout)


def fail(msg):
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def write_sine_wav(path, seconds, hz, sample_rate):
    """Write a mono 16-bit PCM sine tone. Stdlib-only (wave + struct) — no
    ffmpeg dependency, since the CI runner image doesn't have it."""
    import wave

    n_samples = int(seconds * sample_rate)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        frames = bytearray()
        for i in range(n_samples):
            val = int(32767 * 0.5 * math.sin(2 * math.pi * hz * i / sample_rate))
            frames += struct.pack("<h", val)
        w.writeframes(bytes(frames))


def wait_for_health(timeout_s=60):
    deadline = time.time() + timeout_s
    last_err = None
    while time.time() < deadline:
        try:
            h = get("/health", timeout=5)
            if h.get("status") == "ok":
                print(f"health ok: {h}")
                return
        except (urllib.error.URLError, OSError) as e:
            last_err = e
        time.sleep(1)
    fail(f"ReaClaw never became healthy within {timeout_s}s (last error: {last_err})")


def main():
    print(f"Smoke test against {BASE}")
    wait_for_health()

    # Diagnostic: confirm the main-thread command queue actually drains at
    # all before relying on it, with a generous timeout — a freshly-launched
    # REAPER's first main-thread tick has been observed to take longer than
    # the default 15s in some CI environments.
    t0 = time.time()
    probe = post("/execute/action", {"id": 40364, "timeout_ms": 90000}, timeout=95)
    print(f"first main-thread call took {time.time() - t0:.1f}s: {probe}")
    if probe.get("status") != "success":
        fail(f"main-thread command queue never drained: {probe}")

    tone_path = os.path.join(tempfile.gettempdir(), "reaclaw_ci_tone.wav")
    write_sine_wav(tone_path, TONE_SECONDS, TONE_HZ, SAMPLE_RATE)

    created = post(
        "/state/tracks", {"create": [{"name": "CISmokeTest"}]}
    )
    if not created.get("created"):
        fail(f"track creation failed: {created}")

    item = post(
        "/state/items",
        {"create": [{"track": 0, "position": 0, "length": TONE_SECONDS, "file": tone_path}]},
    )
    if not item.get("created"):
        fail(f"item creation failed: {item}")

    output_path = os.path.join(tempfile.gettempdir(), "reaclaw_ci_render.wav")
    if os.path.exists(output_path):
        os.remove(output_path)

    render = post("/render", {"output": output_path}, timeout=60)
    if "output_path" not in render:
        fail(f"render did not return output_path: {render}")
    if not os.path.isfile(output_path):
        fail(f"render reported success but {output_path} does not exist")

    analysis = get(f"/analysis/file?path={output_path}&measures=loudness")
    loudness = analysis.get("loudness", {})
    peak_db = loudness.get("peak_db")
    length = analysis.get("source", {}).get("length")

    if peak_db is None:
        fail(f"analysis response missing loudness.peak_db: {analysis}")
    if peak_db <= -150:
        fail(f"rendered output is silent (peak_db={peak_db})")
    if length is None or abs(length - TONE_SECONDS) > 0.1:
        fail(f"rendered output length {length} does not match expected {TONE_SECONDS}s")

    print(
        f"PASS: rendered {length:.2f}s, peak_db={peak_db:.1f}, "
        f"render_seconds={render.get('render_seconds')}, "
        f"offline_ratio={render.get('offline_ratio')}"
    )


if __name__ == "__main__":
    try:
        main()
    except urllib.error.HTTPError as e:
        fail(f"HTTP {e.code} from {e.url}: {e.read().decode(errors='replace')}")
    except urllib.error.URLError as e:
        fail(f"connection error: {e}")

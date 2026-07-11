#!/usr/bin/env python3
"""lib.py — thin ReaClaw REST client + REAPER choreography helpers.

Talks to the ReaClaw HTTPS server embedded in REAPER on the Pi rig.
Config (see CLAUDE.md / local-test-rig notes):
    server : 127.0.0.1:9091   (self-signed TLS -> we disable verification)
    auth   : Bearer sk_change_me

Verified endpoints (against src/ on branch at time of writing):
    POST /execute/action          body {"id": <int|str>}
    POST /scripts/register        body {"name": str, "script": <lua>} -> {"action_id": int}
    DELETE /scripts/{action_id}
    POST /state/tracks            body {"create":[{...}], "update":[{...}]}
    POST /state/tracks/{i}        body {volume_db, pan, muted, name, ...}
    POST /state/tracks/{i}/fx     body {"name": "ReaSynth"}
    POST /state/tracks/{i}/automation  body {"envelope": "Volume", "points":[{time,value}]}
    GET/POST /state/tracks/{i}/fx/{slot}/preset  body {"navigate": -1|1} or {"name": "..."}
    POST /state/items             body {"create":[{"track","position","file",...}]}
    POST /state/tempo             body {"time": float, "bpm": float}
"""
import json
import ssl
import time
import urllib.request

BASE = "https://127.0.0.1:9091"
API_KEY = "sk_change_me"

_CTX = ssl.create_default_context()
_CTX.check_hostname = False
_CTX.verify_mode = ssl.CERT_NONE


def _req(method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(BASE + path, data=data, method=method)
    req.add_header("Authorization", "Bearer " + API_KEY)
    if data is not None:
        req.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(req, context=_CTX, timeout=30) as r:
        raw = r.read().decode()
    return json.loads(raw) if raw else {}


def get(path):            return _req("GET", path)
def post(path, body=None): return _req("POST", path, body if body is not None else {})
def delete(path):         return _req("DELETE", path)


# ---- MIDI helpers -------------------------------------------------------------

def get_midi(item_index):
    """GET /state/items/{index}/midi — returns notes and CC as dicts."""
    return get(f"/state/items/{item_index}/midi")


def post_midi(item_index, notes=None, cc=None, replace=False):
    """POST /state/items/{index}/midi — insert notes/CC into a MIDI take.

    replace=True clears all existing notes and CC first (clean rewrite).
    notes: list of dicts with pitch, start_ppq/end_ppq or start_time/end_time,
           and optional velocity (default 100) and channel (default 0).
    cc: list of dicts with number, value, channel, and ppq or time.
    """
    body = {"replace": replace}
    if notes is not None:
        body["notes"] = notes
    if cc is not None:
        body["cc"] = cc
    return post(f"/state/items/{item_index}/midi", body)


# ---- action / command helpers -------------------------------------------------

def act(cmd_id):
    """Run a REAPER action / command id (built-in number or named command)."""
    return post("/execute/action", {"id": cmd_id})


# Common command ids used by the trailer (labels per memory notes):
PLAY            = 1007
STOP            = 1016
ZOOM_IN_HORIZ   = 1012   # NOTE: REAPER's labels are swapped from intuition
ZOOM_OUT_HORIZ  = 1011
ZOOM_TO_TIMESEL = 40031
TRACKS_TALLER   = 40111
TRACKS_SHORTER  = 40112
ARM_VOL_ENV     = 40406  # activate the track Volume envelope (needed before writing it)
ARM_PAN_ENV     = 40407  # activate the track Pan envelope
OPEN_MIDI_EDIT  = 40153  # open selected item in the MIDI editor (piano roll)


# ---- track helpers ------------------------------------------------------------

def add_track(name=None, volume_db=None, muted=None):
    """Create one track (appended). Returns its index."""
    spec = {}
    if name is not None:       spec["name"] = name
    if volume_db is not None:  spec["volume_db"] = volume_db
    if muted is not None:      spec["muted"] = muted
    r = post("/state/tracks", {"create": [spec]})
    return r["created"][0]["index"]


def set_track(i, **props):
    """Update a track: volume_db=, pan=, muted=, soloed=, name=, color=, ..."""
    return post(f"/state/tracks/{i}", props)


def add_fx(i, name):
    """Add an FX/instrument by name. For the synth use name='ReaSynth'.
    (Resolves to 'VSTi: ReaSynth (Cockos)'. The Lua TrackFX_AddByName route
    did NOT work on this build — use this structured endpoint.)"""
    return post(f"/state/tracks/{i}/fx", {"name": name})


def write_automation(i, envelope, points):
    """points = [{"time": sec, "value": v}, ...].
    The envelope must already be ACTIVE — call act(ARM_VOL_ENV / ARM_PAN_ENV)
    on the selected track first, or this returns 400 'Envelope not found'."""
    return post(f"/state/tracks/{i}/automation",
                {"envelope": envelope, "points": points})


def fx_preset(track_i, slot, navigate=None, name=None):
    """Read (no args) or change the FX's preset: navigate=-1|1 to step, or
    name="..." to load by name. Returns {preset, preset_index, preset_count}."""
    if navigate is None and name is None:
        return get(f"/state/tracks/{track_i}/fx/{slot}/preset")
    body = {}
    if navigate is not None: body["navigate"] = navigate
    if name is not None:     body["name"] = name
    return post(f"/state/tracks/{track_i}/fx/{slot}/preset", body)


def show_fx(track_i, slot, show=True):
    """Float an FX's plugin GUI open (show=True) or closed (show=False).
    No REST verb for this — TrackFX_Show is plain Lua."""
    mode = 3 if show else 2
    return run_lua(f'reaper.TrackFX_Show(reaper.GetTrack(0,{track_i}), {slot}, {mode})',
                    name=f"reaclaw_fxshow_{track_i}_{slot}")


def write_fx_param_envelope(track_i, slot, param_i, points, name=None):
    """points = [(time_sec, value_0_1), ...]. Arms the FX-parameter envelope
    (GetFXEnvelope(...,create=true)) and writes points directly via
    InsertEnvelopePoint — verified live: REAPER draws the lane in the arrange
    immediately, no separate 'show envelope' step needed. There is no REST
    verb for this (only track-level Volume/Pan/etc. envelopes have one via
    write_automation), so it goes through run_lua."""
    inserts = "\n".join(
        f'reaper.InsertEnvelopePoint(env, {float(t)}, {float(v)}, 0, 0, false, true)'
        for t, v in points
    )
    lua = (
        f'local tr = reaper.GetTrack(0, {track_i})\n'
        f'local env = reaper.GetFXEnvelope(tr, {slot}, {param_i}, true)\n'
        f'if env then\n{inserts}\n  reaper.Envelope_SortPoints(env)\nend\n'
    )
    return run_lua(lua, name=name or f"reaclaw_fxenv_{track_i}_{slot}_{param_i}")


def set_tempo(time_sec, bpm):
    return post("/state/tempo", {"time": time_sec, "bpm": bpm})


# ---- media items (audio) -------------------------------------------------

def create_items(specs):
    """specs = [{"track","position","file",...}, ...]. Unlike .mid insertion
    (no MIDI-item REST verb, see insert_media), audio items have a real one:
    POST /state/items {"create":[...]} loads the file as the item's source
    directly. Returns the created item list (each with its assigned index)."""
    return post("/state/items", {"create": specs})["created"]


# ---- stretch markers (audio timing correction) ----------------------------
#
# No REST verb for any of this — same story as FX-parameter envelopes.
# SetTakeStretchMarker/GetTakeNumStretchMarkers are plain Lua; "snap to grid"
# is the native action 41846. Verified live against this build:
#   - 41843 "Add stretch markers at time selection" does NOT do the transient
#     detection its name implies here — it only drops markers at the two
#     EDGES of the time selection. Per-hit markers need placing explicitly.
#   - 40836 "Item navigation: move cursor to nearest transient in items" DOES
#     do real transient analysis and is what nearest_transient() rides on.
#   - A marker's `pos` is take-relative (seconds from the start of the
#     take's source), not the item's absolute project position.

def glue_track_items(track_i):
    """Select every item on `track_i` and glue them into one continuous take
    (40362, "ignoring time selection" so a stale time selection elsewhere
    can't clip it). Leaves that track with exactly one item, selected."""
    lua = (
        f'local tr = reaper.GetTrack(0, {track_i})\n'
        'reaper.SetOnlyTrackSelected(tr)\n'
        'reaper.Main_OnCommand(40289, 0)\n'
        'local n = reaper.CountTrackMediaItems(tr)\n'
        'for i = 0, n - 1 do reaper.SetMediaItemSelected(reaper.GetTrackMediaItem(tr, i), true) end\n'
        'reaper.Main_OnCommand(40362, 0)\n'
        'reaper.UpdateArrange()\n'
    )
    return run_lua(lua, name=f"reaclaw_glue_{track_i}")


def select_track_item(track_i, item_i=0):
    """Select only the given track + the one item on it at slot `item_i`."""
    lua = (
        f'local tr = reaper.GetTrack(0, {track_i})\n'
        'reaper.SetOnlyTrackSelected(tr)\n'
        'reaper.Main_OnCommand(40289, 0)\n'
        f'local it = reaper.GetTrackMediaItem(tr, {item_i})\n'
        'if it then reaper.SetMediaItemSelected(it, true) end\n'
        'reaper.UpdateArrange()\n'
    )
    return run_lua(lua, name=f"reaclaw_seltrkitem_{track_i}_{item_i}")


def set_project_grid(division):
    """division = fraction of a whole note (0.25 = quarter note)."""
    return run_lua(f'reaper.GetSetProjectGrid(0, true, {float(division)}, 0, 0)',
                    name="reaclaw_grid")


def nearest_transient(near_time):
    """Park the cursor at near_time, snap it onto the nearest REAL detected
    transient (native action 40836 — genuine analysis, not a guess), and
    return where it landed. Used for the on-camera 'REAPER finds the hit'
    beat; the cursor move is visible in the arrange."""
    run_lua(f'reaper.SetEditCurPos({float(near_time):.6f}, false, false)', name="reaclaw_curat")
    act(40836)
    lua = ('local f = io.open("/tmp/reaclaw_transient.txt", "w")\n'
           'f:write(string.format("%.6f", reaper.GetCursorPosition()))\n'
           'f:close()\n')
    run_lua(lua, name="reaclaw_readtransient")
    return float(open("/tmp/reaclaw_transient.txt").read())


def add_stretch_marker_at_cursor():
    """41842 — adds a stretch marker on the selected item at the edit cursor."""
    return act(41842)


def set_stretch_markers(track_i, positions, item_i=0):
    """positions = take-relative seconds. Places one stretch marker at each
    (SetTakeStretchMarker(take, -1, pos) appends) — the real 'audio quantize'
    primitive, batched into one HTTP round trip."""
    lua = (f'local it = reaper.GetTrackMediaItem(reaper.GetTrack(0, {track_i}), {item_i})\n'
           'local tk = reaper.GetActiveTake(it)\n')
    for p in positions:
        lua += f'reaper.SetTakeStretchMarker(tk, -1, {float(p):.6f})\n'
    lua += 'reaper.UpdateArrange()\n'
    return run_lua(lua, name=f"reaclaw_stretchmarkers_{track_i}")


def snap_stretch_markers():
    """41846 "Item: Snap stretch markers to grid" — the payoff. Every marker
    on the selected item jumps to the nearest grid line; REAPER locally
    stretches the audio around it so the transient lands on the beat, in
    place, no pitch shift (verified live: take-relative marker positions
    move to exactly n/BPM*60 while their srcpos — the audio content —
    stays put)."""
    return act(41846)


# ---- raw Lua escape hatch -----------------------------------------------------

_lua_seq = 0

def run_lua(lua, name=None):
    """Register a one-shot Lua script, run it, then unregister it.
    This is how we reach REAPER APIs with no REST endpoint (e.g. InsertMedia)."""
    global _lua_seq
    _lua_seq += 1
    nm = name or f"reaclaw_oneshot_{int(time.time())}_{_lua_seq}"
    reg = post("/scripts/register", {"name": nm, "script": lua})
    if not reg.get("registered"):
        raise RuntimeError(f"Lua register failed: {reg}")
    aid = reg["action_id"]
    try:
        return act(aid)
    finally:
        try:
            delete(f"/scripts/{aid}")
        except Exception:
            pass


def insert_media(track_index, path):
    """Insert a media file (e.g. a .mid) at time 0 on the given track.
    Selects the track + parks the edit cursor first, per the working recipe."""
    lua = (
        f'reaper.SetOnlyTrackSelected(reaper.GetTrack(0, {track_index}))\n'
        f'reaper.SetEditCurPos(0, false, false)\n'
        f'reaper.InsertMedia({json.dumps(path)}, 0)\n'
    )
    return run_lua(lua, name=f"reaclaw_insert_{track_index}")


def set_loop_and_repeat(length_sec):
    """Set the loop/time range to [0, length] and enable repeat.
    GetSetRepeat(1) can behave like a toggle on this build, so query first
    and only flip it when it's actually off."""
    lua = (
        f'reaper.GetSet_LoopTimeRange(true, false, 0, {float(length_sec)}, false)\n'
        f'if reaper.GetSetRepeat(-1) == 0 then reaper.GetSetRepeat(1) end\n'
    )
    return run_lua(lua, name="reaclaw_loop")


def play_from_start():
    """Park the edit cursor at 0 and start playback (so we never roll into the
    silent tail past the content)."""
    return run_lua('reaper.SetEditCurPos(0, false, false)\nreaper.CSurf_OnPlay()')


def clear_project():
    """Delete every track — a clean slate without a 'save changes?' dialog."""
    return run_lua('while reaper.CountTracks(0) > 0 do '
                   'reaper.DeleteTrack(reaper.GetTrack(0, 0)) end')


# ---- camera / zoom helpers ----------------------------------------------------
#
# The arrange "camera" is the visible time window, set with GetSet_ArrangeView2.
# A blocking Lua loop that animates it does NOT repaint intermediate frames on
# this SWELL build (the event loop can't paint while the script holds the main
# thread). So we drive the animation frame-by-frame FROM PYTHON: a tiny
# persistent action reads a target window from a file and applies it; between
# Python calls REAPER's event loop runs and repaints. That gives a genuinely
# smooth cinematic zoom on tape.

_VIEW_PATH = "/tmp/reaclaw_view.txt"
_view_aid = None


def _ensure_view_applier():
    """Register (once) a persistent action that applies the window in _VIEW_PATH."""
    global _view_aid
    if _view_aid is not None:
        return _view_aid
    lua = (
        f'local fh = io.open({json.dumps(_VIEW_PATH)}, "r")\n'
        'if fh then\n'
        '  local s = fh:read("*a"); fh:close()\n'
        '  local a, b = s:match("([%-%d%.]+)%s+([%-%d%.]+)")\n'
        '  if a and b then\n'
        '    reaper.GetSet_ArrangeView2(0, true, 0, 0, tonumber(a), tonumber(b))\n'
        '    reaper.UpdateArrange()\n'
        '  end\n'
        'end\n'
    )
    reg = post("/scripts/register", {"name": "reaclaw_applyview", "script": lua})
    if not reg.get("registered"):
        raise RuntimeError(f"view applier register failed: {reg}")
    _view_aid = reg["action_id"]
    return _view_aid


def set_view(start, end):
    """Set the arrange's visible time window to [start, end] seconds (one frame)."""
    with open(_VIEW_PATH, "w") as f:
        f.write(f"{float(start)} {float(end)}")
    return act(_ensure_view_applier())


def get_view():
    """Read the current visible [start, end] window (seconds)."""
    lua = (
        'local a, b = reaper.GetSet_ArrangeView2(0, false, 0, 0)\n'
        'local f = io.open("/tmp/reaclaw_getview.txt", "w")\n'
        'f:write(string.format("%.6f %.6f", a, b)); f:close()\n'
    )
    run_lua(lua, name="reaclaw_getview")
    a, b = open("/tmp/reaclaw_getview.txt").read().split()
    return float(a), float(b)


def project_length():
    lua = ('local f=io.open("/tmp/reaclaw_plen.txt","w")\n'
           'f:write(string.format("%.6f", reaper.GetProjectLength(0))); f:close()\n')
    run_lua(lua, name="reaclaw_plen")
    return float(open("/tmp/reaclaw_plen.txt").read())


def _smoothstep(e):
    return e * e * (3.0 - 2.0 * e)


def animate_view(a0, b0, a1, b1, dur, ease=_smoothstep):
    """Animate the visible window from [a0,b0] to [a1,b1] over `dur` seconds.
    Wall-clock paced: the easing tracks real elapsed time, so the move always
    lasts `dur` regardless of per-frame HTTP latency (it just drops frames if
    the rig is slow). Drives one set_view per loop; the event loop repaints
    between calls."""
    st = time.time()
    while True:
        e = (time.time() - st) / dur
        if e >= 1.0:
            break
        k = ease(e)
        set_view(a0 + (a1 - a0) * k, b0 + (b1 - b0) * k)
    set_view(a1, b1)


def animate_view_to(a1, b1, dur, ease=_smoothstep):
    """Animate from the CURRENT window to [a1,b1] over `dur` seconds."""
    a0, b0 = get_view()
    animate_view(a0, b0, a1, b1, dur, ease)


def disable_follow():
    """Turn OFF 'continuous scrolling during playback' (action 40036) so our
    view commands aren't yanked to follow the playhead. Verified: with this on,
    a pinned view drifts to centre the playhead within ~1s."""
    run_lua('if reaper.GetToggleCommandState(40036) == 1 then '
            'reaper.Main_OnCommand(40036, 0) end', name="reaclaw_nofollow")


# ---- caption marks ------------------------------------------------------------

_MARKS_PATH = "/tmp/marks.txt"

def mark(label):
    """Append `epoch<TAB>label` so post.py can align a caption to this moment."""
    with open(_MARKS_PATH, "a") as f:
        f.write(f"{time.time():.3f}\t{label}\n")


def reset_marks():
    open(_MARKS_PATH, "w").close()

# Learned — not yet folded into the guide

Dated, generic ReaClaw/REAPER facts discovered during live operation. When
this list passes ~15 bullets, fold them into `docs/AGENT_GUIDE.md` (§5
gotchas / §6 traps / §4 cheat sheet) and clear it. Machine- or user-specific
facts belong in `LOCAL.md` (gitignored), not here.

<!-- append below -->

- 2026-07-12 — `POST /reaper/restart` on Linux does NOT reliably relaunch REAPER
  (the child relaunch didn't survive on the aarch64 rig — REAPER exited and
  stayed down). Treat restart as "expect to relaunch it yourself" and verify
  `GET /health` comes back; don't assume the endpoint brought it back up.
- 2026-07-12 — Right after a REAPER restart, main-thread reads
  (`/state/changes`, `/project/new`) can return `{"code":"TIMEOUT"}` and the
  command queue climbs (`/health` → `status:"degraded"`, `queue_depth` rising).
  On a fresh REAPER that's the **update-check modal** wedging the main thread
  (window title "REAPER New Version Notification"). Recovery: screenshot /
  `wmctrl -l`, `wmctrl -c "REAPER New Version Notification"`, then the queue
  drains and reads return. Prevent it by pre-seeding `[verchk]` in reaper.ini.
- 2026-07-12 — `POST /project/new` can be a silent no-op here (returns
  `{"ok":true}` but tracks/title unchanged). To actually clear a project use
  `POST /project/reset` (blanks in place: 0 tracks, tempo→120) after a
  `POST /project/save` if you want the .rpp preserved on disk.
- 2026-07-12 — Event ring resets on REAPER restart (seq restarts near 0). A
  tail resuming from a stale high `since` filters everything out forever —
  clamp the resume point to the server's current cursor (fixed in
  events-tail.sh).
- 2026-07-12 — On the Windows build (REAPER 7.77/x64, ReaClaw 1.18.0), the
  `estimated_dsp` spectral path of `/analysis/file` AND `/analysis/item`
  returns `"no decodable audio in window"` for WAVs REAPER itself rendered
  (16- and 24-bit both), while the `loudness` measures (REAPER's own decoder)
  work fine on the same file. Workaround: pull the file and do spectral DSP
  yourself. Worth a GitHub issue.
- 2026-07-12 — Reason Rack Plugin (VST3i) over the API: an *empty* rack shows
  2570 placeholder params ("Param 1000"…) and `preset_count: 0`; once a device
  is loaded the params rename to `<patch name>:<param>` (e.g. "808 Bendin
  Kit:Drum 1 Level") — that rename is how you detect "device loaded" without a
  GUI. Loading devices/patches is GUI-only; `TrackFX_Show(tr,fx,3)` floats the
  window for the human. Kong exposes Drum 1-16 Level/Pan/DM Pitch/Decay etc.
  as params, MIDI notes 36-51 = pads 1-16.
- 2026-07-12 — Mapping an unknown drum kit autonomously works well: mute other
  tracks, one MIDI hit per pad 0.5 s apart at the end of the timeline, render
  `bounds:"custom"` around it, classify each window by band energy + decay
  (kick <120 Hz long, closed hat >6 kHz short, 808 cowbell ~845 Hz pure mid,
  toms tonal low-mid ladder, clap 1.5-6 kHz burst).
- 2026-07-12 — **ReaClaw 1.18.0's Windows DLL is not safe to run**: while loaded,
  plain clicks on mute/solo execute their Ctrl variants ("exclusively"), wheel
  zoom dies, FX windows won't open — REAPER's own input handling is corrupted
  in-process (OS modifier state clean, config factory). Removing the DLL fully
  restores normal behavior. Tracked in issue #111 (suspected csurf
  vtable/packing ABI mismatch under MSVC). Until fixed: don't operate Windows
  rigs with the extension loaded longer than needed, and expect GUI weirdness
  for the human while you do.
- 2026-07-12 — Linux control test: the #111 input corruption does NOT occur on
  the Linux build (xdotool-clicked TCP mute behaves normally with ReaClaw
  loaded, even after renders/API bursts) — Windows-specific. Also: `POST
  /render` on Linux leaves REAPER's "Finished in 0:00" window open; it eats GUI
  clicks under its rectangle — close with `wmctrl -c "Finished in"` if a human
  needs the GUI afterwards.

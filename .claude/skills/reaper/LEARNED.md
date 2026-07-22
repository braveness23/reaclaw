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
- 2026-07-19 — **`POST /reaper/restart` silently saves the current project to
  its existing path first, before restarting.** If that in-memory project is
  a scratch session built via `/project/reset` on top of a REAL project's
  file path (`/project/reset` blanks tracks in place — it does NOT change
  the path), restarting overwrites the real project file with the scratch
  content. This actually happened: a throwaway drum-beat test session sitting
  under `PostPunkTrailer.rpp`'s path got saved over the real 7-track project
  when a restart was needed to pick up a newly-installed system plugin.
  Recovered via the timestamped `Backups/*.rpp-bak` (confirmed exact match to
  pre-session state: same BPM, track count/names, project length, `dirty:
  false`) — those backups are the safety net, so know where they live before
  you ever call restart. **Rule: before any `/reaper/restart`, check
  `GET /project` → `dirty` and the current path; if the in-memory project
  doesn't match what should legitimately live at that path, `/project/save`
  to somewhere safe (or `/project/reset` again pointed nowhere important)
  first — never let restart's implicit save be the first time you think
  about what's about to hit disk.**
- 2026-07-19 — **Generic technique: cloning opaque VST/VSTi state via
  `/state/chunk`.** For any plugin with proprietary internal state that has
  no structured verb and no useful named-config param (e.g. Reason Rack
  Plugin — a whole Reason rack, device + patch, is one binary blob inside
  the VST3's serialized state), the FX's state still round-trips as plain
  text inside `GET/POST /state/chunk?target=track&index=N` (it's embedded in
  the track's `<FXCHAIN>` block). So: get one instance into the state you
  want (GUI-only, one-time), capture the track chunk, then replay it onto
  other tracks/projects by rewriting just `TRACKID` and each FX's `FXID`
  guid (to avoid ID collisions) before POSTing. Verified live: params
  renamed to the real patch's names and the cloned instance produced
  identical audio output, with zero further GUI interaction. Turns "GUI-only
  forever" into "one human step, then scriptable indefinitely" for any
  black-box plugin.
- 2026-07-18 — Pre-seeding `[verchk]` in `reaper.ini` (the 2026-07-12 fix)
  does NOT fully prevent the "REAPER New Version Notification" modal: it
  stops the *check* from re-firing, but if a real newer version exists the
  one-time "update available" dialog still surfaces on launch even with
  `lastt` already populated. Keep treating a post-launch `/health` showing
  `queue_depth` rising / TIMEOUTs as "assume the modal is up" regardless.
- 2026-07-18 — On a WM-less Xvfb display (no window manager running),
  `wmctrl -l` fails outright (`Cannot get client list properties` —
  `_NET_CLIENT_LIST` needs a WM to set it). `xdotool search --name "<title>"`
  still finds windows fine by title on the same display; use that instead of
  wmctrl for modal-recovery on this class of headless rig.
- 2026-07-15 — `config.json`'s `tls.enabled` field is dead at the transport
  layer: `src/server/server.cpp` always constructs an `httplib::SSLServer`
  regardless of the flag (matches TECH_DECISIONS.md §4 — TLS-always is the
  settled design, so this isn't a bug to silently "fix" by adding an HTTP
  branch, just a misleading unused config key). Don't trust `tls.enabled` when
  reasoning about how to reach the API — it's always `https://`, self-signed
  cert, `-k`/trust-prompt required. `rc` already assumes this (`BASE=https://…`
  hardcoded) and is correct; a raw `curl http://host:port/...` will hang/empty-reply.
- 2026-07-21 — **REAPER's Linux audio backend (Preferences → Audio → Device
  → "Audio system") cannot be changed by editing `reaper.ini`'s `audiodev=`
  key while REAPER is closed** — the key gets written back to whatever
  value you set, but it has no effect on which backend is actually active
  on next launch; only driving the real Preferences dialog (GUI click or
  automated via `xdotool`) and clicking OK does anything. This is easy to
  get wrong silently: a tool like `pw-jack jack_lsp` can show REAPER's
  ports connected even while it's genuinely still on a *different* backend
  (PulseAudio/ALSA), because under PipeWire every client — regardless of
  which protocol it actually used — shows up in every protocol's view of
  the unified graph. The only trustworthy signals that a backend switch
  actually took: REAPER's own status bar text, and (bonus tell) the master
  track's output label switching from generic `Output 1 / Output 2` to the
  real device name once on JACK/ALSA. Once switched via the GUI, it does
  persist across a `POST /reaper/restart` without repeating the GUI step.
- 2026-07-21 — **`POST /transport/stop` after a recording can hang for the
  full main-thread timeout (~15s) and return `{"code":"TIMEOUT"}`** — cause:
  REAPER's own "Select files to save or delete" modal (Preferences default:
  "Prompt to save/delete/rename new files: on stop" is checked out of the
  box), which pops up over the recorded take and blocks the main thread
  exactly like the verchk/render-finished modals already documented above.
  `GET /health` can still report `status:"ok", queue_depth:0` *while this
  modal is up* — health isn't a reliable "is a modal blocking me" signal
  for this particular case, only the timeout itself and/or a screenshot are.
  Fix: uncheck "on stop" on that dialog once (persists to `reaper.ini` as
  `promptendrec=0`, confirmed durable — unlike `audiodev=`, this key *is*
  read on startup) — after that, `/transport/stop` returns in well under a
  second even with freshly recorded media on the timeline. If a request
  needs to record via the API at all, check/set `promptendrec=0` first
  rather than eating a 15s timeout on the first stop.

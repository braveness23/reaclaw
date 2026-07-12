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

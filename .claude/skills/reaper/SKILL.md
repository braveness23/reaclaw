---
name: reaper
description: Operate the live REAPER instance through ReaClaw's REST API — mute/solo/volume/pan, transport (play/stop/record), tempo, markers, items, MIDI, FX (add/tweak/presets), render, screenshots, project open/save. Use for ANY request to do something in REAPER or "the project" right now ("mute track 3", "play it", "add an EQ", "drop a marker", "render the mix"), as opposed to changing ReaClaw's own source code.
---

# Live REAPER operation via ReaClaw

The contract: a common request costs **one warmup (first time this session) +
one mutation call**. Never explore, never act surprised.

## On first use in a session

1. Read `docs/AGENT_GUIDE.md` (repo root) — the operating manual: connection,
   latency contract, sync protocol, cheat sheet, trap list. Everything below
   assumes you have it loaded.
2. Read `LOCAL.md` in this directory if it exists — machine- and user-specific
   facts (displays, launch recipe, media libraries, the user's shorthand).
3. Read `LEARNED.md` in this directory — recent gotchas not yet folded into
   the guide.
4. Run `.claude/skills/reaper/warmup.sh` — prints the project digest (tracks,
   BPM, transport), seeds the change cursors, and starts the background event
   tail. Budget ≤1 s. If it fails, REAPER/ReaClaw isn't up: see LOCAL.md for
   the launch recipe.

## Making calls

Use the `rc` wrapper in this directory (auth + TLS + host discovery built in):

```bash
.claude/skills/reaper/rc GET  /state
.claude/skills/reaper/rc POST /state/tracks/2 '{"muted":true}'
RC_TIME=1 .claude/skills/reaper/rc POST /transport/play   # appends total time
```

Known verb ⇒ mutation first, single tool call, response body is the
verification. Batch independent calls in one invocation.

## API-first — and log when it isn't possible

Default to the ReaClaw REST API for anything it can do — project/track
state, transport, FX, items, MIDI, render, and so on. Don't reach for a
`reaper.ini` edit, `/execute/script` Lua, or GUI automation (`xdotool`
against REAPER's actual windows) until the API is confirmed insufficient
for the specific thing being changed.

When it genuinely can't — usually because the thing is a REAPER
*application* preference rather than project/track state ReaClaw models
(audio device/system selection, channel counts, dialog-prompt checkboxes,
etc.) — say so **once per session** (not on every repeated use of the same
workaround) and log it as a dated bullet in `LEARNED.md`, same as any other
discovered fact. Issue #119 has the concrete inventory that prompted this
rule — worth a skim for what's already known to be API-unreachable, so it
isn't rediscovered from scratch.

## Staying in sync with the user's GUI edits

The event tail appends to `~/.cache/reaclaw-agent/events.jsonl` — reading it
is free. Before acting on cached beliefs about project state, check the tail
(or one `rc GET /state/changes`, ~75 ms). Full re-read of `/state/tracks`
only on `track_list_change`, a cursor reset (REAPER restarted), or heavy
churn. Details: guide §3.

## Learning loop — never surprised twice

The same turn you discover something new, write it down:

- Generic ReaClaw/REAPER/plugin fact → dated bullet in `LEARNED.md` (ships to
  every user of this repo).
- This-machine or this-user fact (paths, media, shorthand) → `LOCAL.md`
  (gitignored).
- When `LEARNED.md` grows past ~15 bullets, fold them into
  `docs/AGENT_GUIDE.md` §5/§6 and clear it — that rides along with any normal
  PR, and the served `GET /agent/guide` picks it up on the next release.

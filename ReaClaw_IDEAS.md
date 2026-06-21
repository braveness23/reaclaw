# ReaClaw — Ideas Queue

**Status:** raw idea backlog. Nothing here is decided or scoped. Each item is a
concept to discuss and potentially implement — **no implementation choices are
captured on purpose.** When an item graduates, it moves into
`ReaClaw_IMPLEMENTATION_CHECKLIST.md` with real design, and the relevant settled
constraints in `ReaClaw_TECH_DECISIONS.md` apply.

Origin: a June 2026 design session (the "perception, learning & discovery"
layer). The implementation suggestions from that session are intentionally
discarded — they assumed a different architecture. Only the core ideas survive
here.

These ideas are the **perception/learning** half of the project. The repo's open
GitHub issues cover the complementary **control/ergonomics** half (#7 API
coverage, #9 friction log, #10 "magic wand" Skill/MCP, #8 action-name logging).
Where an idea overlaps an existing issue, it's cross-linked below and the issue —
being grounded in a real session — is the authority. See *Relationship to open
issues* at the bottom.

---

## Decisions taken (2026-06-20)

Dave settled the direction before implementation:

1. **Tiered coverage** — add direct commands for common objects (track create/
   name/color/folder, add named FX, routing, batch writes, selection write) and
   keep the action + Lua escape hatch for the long tail. *(Resolves #7's open
   philosophy question; a `ReaClaw_TECH_DECISIONS.md` entry is due when the verbs
   land.)*
2. **Audio = basic built-in, advanced optional** — loudness/true-peak/clipping/
   EQ-balance built in and always available; key/tempo/pitch gated behind an
   optional external tool, skipped gracefully when absent.
3. **Phased with check-ins** — build in chunks, review each before the next.
4. **Start with quick wins** — #8 (log action names), readable structure
   (color/folder in `/state/tracks` reads, part of Q2), #2 (nicer menu dialogs).

## The thesis (why any of this matters)

Most REAPER bridges are **blind command pipes**: they fire actions and never
perceive the result. ReaClaw's intended edge is two-fold:

1. **Close the perceptual + learning loop** — the agent can observe (and
   eventually *hear*) the consequences of its own edits.
2. **Discover/reuse over regenerate** — and the agent's libraries (actions,
   probes, learned hints) **compound with use** instead of starting cold every
   time.

Two framing principles worth keeping as we discuss each item:

- **Consequence of an edit vs. independent observation.** A consequence of a
  specific edit belongs *with that edit's response*. An independent observation
  of state is something the agent should be able to ask for on its own.
- **Token economy.** Prefer targeted over total (one surface, one band — not the
  whole screen). Prefer a number/digest over an image when a number suffices.
  Keep heavier feedback opt-in.

---

## The queue

Q-numbers are **stable IDs, not a ranking**. Reconciled against the open issues,
the rough priority is: the genuinely-new perception items (Q1 analysis, Q3
hints) and the grounded state work (Q2, already demanded by #9) lead; Q5
screenshots is deliberately a **fallback, not a goal** (see Q5); Q6 is largely
already realized. Order below is by ID for stable reference.

### Q1 — Audio analysis ("the agent can hear itself")
Let the agent measure its own audio output: loudness, peak level, rough spectral
balance, onset/density, clipping — and later, similarity/"sounds like" matching.
This is the headline differentiator; most of the rest is more valuable once the
agent can already perceive sound.

### Q2 — Session introspection (structured state)
A cheap, structured view of the project (tracks, FX, routing, regions, tempo/
time-sig, etc.) so the agent consults facts instead of guessing or paying for a
screenshot. Also the substrate the "hints" idea reads from.

> **Cross-link:** overlaps **#9** (friction points #6 readable structure, #9 batch
> reads), **#7** (coverage / data-model), **#10** (eyes-free verification). Those
> issues are the grounded version — they ask for concrete additions to
> `/state/tracks` (folder_depth, color, parent_index, routing). Treat Q2 as the
> "full session graph" framing *over* that work, not a separate effort.

### Q3 — Consequence-aware hints
After an edit, surface the *consequence of that specific edit* against the
current session: e.g. note added to a track with no instrument (silent), a send
to a bus that routes nowhere, an edit that would clip, record-arm with no input.
Starts as a small hand-authored set of invariants.

### Q4 — Audio visualization
Pictures of audio when a picture is faster than numbers: spectrogram, EQ/
spectrum curve, loudness contour, waveform (with onset markers), stereo/phase,
harmonic/chroma content. Key refinements to preserve: an optional
machine-readable **digest** alongside the image (so the agent reads a number
instead of OCR-ing pixels), and an **A/B diff** against an earlier snapshot.

### Q5 — Targeted screenshots (fallback, on demand)
Guiding principle (reconciled with #9): **give the agent readable structure so it
doesn't *need* to screenshot — but give it screenshots when it asks for them.**
Vision is the fallback for GUI-only state that structured data genuinely can't
express (a plugin's custom GUI, a metering display), never the default path for
facts that Q2 should expose as data.

When asked for, capture a *specific* GUI surface (mixer, an FX chain, a focused
plugin, MIDI editor, routing matrix, arrange, master) rather than a blind
full-screen grab — framed and downscaled to bound cost.

> **Cross-link:** **#9** friction points #6/#7 — screenshotting was a yak-shave and
> the real fix was readable structure (→ Q2). So Q5 ranks **below** Q2; it
> complements it, doesn't replace it. Also relates to **#10** "verify-without-eyes."

> **Status (2026-06-21):** the *capability* is proven and the recipe is baked into
> `skill/reaclaw/SKILL.md`. On X11, `ffmpeg -f x11grab -i "$DISPLAY"` captures the
> live REAPER window (arrange, mixer, even SWELL dialogs) where `xwd` fails; the
> agent then `Read`s the PNG, cropping to a region with ffmpeg. This honors the
> guiding principle: structure-first by default, screenshot on demand. What remains
> for a fuller Q5 is *ergonomics* — named surface targets (mixer/FX/arrange) and
> auto-framing/downscaling — not the underlying ability.

### Q6 — Discover-before-generate
The agent should **search the existing action catalog before generating a new
script** — reuse over regenerate. (Note: the catalog already indexes native +
extension/script actions, so this idea is partly realized; the open part is the
*convention/affordance* that makes the agent look first.)

> **Cross-link:** largely owned by existing issues — **#4** (closed; catalog now
> exposes the full action set), **#10** (semantic/embedding search over the
> catalog), **#9** friction #8 (per-verb discovery round trips). Q6 adds little
> beyond those; keep it only as the "search-first convention" reminder unless the
> semantic-search work in #10 doesn't cover it.

### Q7 — Musical-attribute probes (key / pitch / tempo)
The durable insight: a **probe is the "measure" counterpart of an "action"** — a
reusable, registerable unit that returns data instead of changing the project,
so the probe library compounds the same way the action library does. The agent
could even author its own probes ("detect swing % of this take").

Worth preserving: **two truth sources, tagged so the agent trusts them
correctly** — exact introspection (read directly from the project, cheap, no
render) vs. estimated DSP analysis (needs a bounce, carries a confidence value).

### Q8 — Learned suggestions (the compounding moat)
Log `(call → result → did the agent immediately correct it, and how?)` and mine
for "after X, agents usually do Y," then surface those as suggestions tagged as
*learned* with a confidence value. The quality improves with real usage. Must be
**local-first and opt-in** — no phoning home. This is the long-term moat;
distinct from Q3 even if it shares the same suggestion channel.

---

## Cross-cutting concepts to keep in mind

- **Compounding libraries.** Actions, probes, and learned hints all *grow* with
  use — that accumulation is the point, not any single feature.
- **Snapshot layer.** Several ideas (A/B visual diffs, learned-correction
  mining) presume the ability to capture and compare session snapshots over
  time.

---

## Relationship to open GitHub issues

The repo's open issues are the **control/ergonomics** half; this queue is the
**perception/learning** half. Most items are genuinely new; a few overlap and
defer to the (more grounded) issues.

| Idea | Status vs issues |
|---|---|
| Q1 audio analysis | **New** — no issue covers it |
| Q2 session introspection | **Overlaps #7 / #9 / #10** — they own the concrete readback work; Q2 is the framing |
| Q3 consequence hints | **New** |
| Q4 audio visualization | **New** |
| Q5 targeted screenshots | **Fallback** — relates to #9 (#6/#7) / #10; ranks below Q2 |
| Q6 discover-before-generate | **Largely done/owned** — #4 (closed), #10, #9 |
| Q7 musical probes | **New** |
| Q8 learned suggestions | **New** |

Gating dependency: **#7's coverage-philosophy decision** (action-runner vs
structured REST data model) shapes how Q2 lands and how Q7's "project"
(introspection) source is exposed. Worth settling before scoping those two.

## Open questions to revisit (kept, deliberately implementation-agnostic)

- **Long-render UX.** When the thing being analyzed/visualized is long, perceived
  feedback can't be instant. How should the agent experience that (wait? poll?
  fast-path short material?) — independent of how it's built.
- **Probe modeling.** Is a "probe" a first-class concept, or just a flavor of the
  existing action machinery?
- **Where musical attributes live.** Are key/tempo/pitch part of the general
  analysis surface, or their own thing?
- **DSP locus.** Heavier audio analysis could run in-process or be handed to a
  dedicated analyzer — a footprint/complexity tradeoff to weigh when Q1/Q7 get
  scoped.

# ReaClaw — Roadmap (the consolidated thought)

**Status:** this is the single forward-looking plan. It merges the two halves of the
project — *control/coverage* and *perception/learning* — that were previously split
across `ReaClaw_REAPER_API_ANALYSIS.md` (SDK gap analysis, Tiers A–D) and
`ReaClaw_IDEAS.md` (perception/learning backlog, Q1–Q8). Those two remain as the
**source/reference** material; this file is the authority for *what's next and in what
order*. Settled constraints still live in `ReaClaw_TECH_DECISIONS.md`; per-item task
tracking lives in `ReaClaw_IMPLEMENTATION_CHECKLIST.md` (Phase 4).

Each epic below maps to one GitHub issue, ready to develop.

---

## 1. Where we are

Shipped:

- **Phases 0–2** (foundation, scripts/sequences, integration & hardening) — `v1.0.0`.
- **Phase 4 Stages 1–3** — quick wins (action-name logging, readable `folder_depth`/
  `color`, polished SWELL dialogs), tiered control verbs (track create/delete/name/color/
  folder, `add_fx` + params, sends/routing, batch writes, selection write, `GET
  /capabilities`), and agent-friendliness (ReaClaw **Skill**, Python **MCP** with 18 typed
  tools, **semantic catalog search** via local Ollama).
- **Epic 1 — Tier-A control verbs** — `v1.3.0` (#16): undo grouping, markers/regions,
  tempo map + beat↔time, FX presets + real-unit param metadata, envelope writes,
  send extended props, project extras, MIDI-editor catalog.
- **Epic 2 — Tier-B content manipulation** — `v1.4.0` (#17): media item CRUD,
  take properties, source metadata, track extras, FX copy + online/offline, item
  selection write, per-project ext state. (Tier-C — take FX, MIDI note CRUD,
  multi-project — intentionally deferred to the Lua escape hatch.)
- **Epic 3 — Audio perception ("hears itself")** — `v1.5.0` (#18): exact offline
  loudness/peak/true-peak/RMS + clipping, a rough spectral digest (in-tree FFT),
  live per-track metering, and consequence-aware hints inline on mutating
  responses. All tagged method+confidence. (Onset/density deferred.)

That means two of the original ideas are effectively **done**: **Q2** (session
introspection — the structured `/state` surface) and **Q6** (discover-before-generate —
the catalog + semantic search). The current REST/API surface is enumerated in
`ReaClaw_REAPER_API_ANALYSIS.md` Part 2.

What remains is everything below.

---

## 2. The two halves, unified

| # | Epic | Half | Sources | Issue |
|---|------|------|---------|-------|
| 1 | Tier-A control verbs ✅ *done (v1.3.0)* | Control | API_ANALYSIS Tier A | [#16](https://github.com/braveness23/reaclaw/issues/16) |
| 2 | Tier-B/C content manipulation ✅ *Tier-B done (v1.4.0); Tier-C deferred* | Control | API_ANALYSIS Tiers B/C | [#17](https://github.com/braveness23/reaclaw/issues/17) |
| 3 | Audio perception ("hears itself") ✅ *done (v1.5.0); onset/density deferred* | Perception | IDEAS Q1, Q3 | [#18](https://github.com/braveness23/reaclaw/issues/18) |
| 4 | Visual perception & musical probes ✅ *done (v1.6.0); A/B diff lands with the snapshot layer* | Perception | IDEAS Q4, Q5, Q7 | [#19](https://github.com/braveness23/reaclaw/issues/19) |
| 5 | Learned suggestions (the moat) ✅ *done; pairwise layer + snapshot/diff. Heavier mining deferred* | Learning | IDEAS Q8 | [#20](https://github.com/braveness23/reaclaw/issues/20) |

**All five roadmap epics are now complete.** The shared **snapshot / state-diff
layer** built as #20's prep also retro-unlocks the one #19 sliver (A/B visual
diff). Forward work now lives in `ReaClaw_IDEAS.md` plus one-off requests (e.g.
track icons #29, external-change detection #31).

**Tier D** (API_ANALYSIS) is intentionally *not* an epic — real-time PCM/waveform access,
hardware metering, GUI window control, and audio-device config are not meaningfully
achievable via the SDK today. Recorded for completeness, not planned.

### Sequencing

1. **Undo grouping first** (inside Epic 1). API_ANALYSIS flags it as the single biggest
   quality-of-life gap — *every* REST mutation today is un-undoable. Wrapping mutations in
   undo blocks is foundational and should land before more mutating verbs pile on.
2. **Control before deeper perception** — Epic 1 → Epic 2. Tier-A thin-layer verbs, then
   Tier-B/C content manipulation.
3. **Numbers before pictures** — Epic 3 (audio analysis) before Epic 4 (visualization);
   the digest-first principle means a number usually beats an image.
4. **Learning last** — Epic 5 needs Epic 3/4 producing perception signal and the
   snapshot/diff layer to mine corrections.

These epics are otherwise independent and can be picked up in any order the moment their
dependency is met; the project's "phased with check-ins" decision (review each chunk before
the next) still applies.

---

## 3. The five epics

### Epic 1 — Tier-A control verbs ([#16](https://github.com/braveness23/reaclaw/issues/16))

High-ROI, SDK-confirmed, thin REST layer. Undo grouping (`Undo_BeginBlock2/EndBlock2` +
undo/redo verbs); markers & regions (`/state/markers`); tempo/time-sig map + beat↔time
utilities (`TimeMap2_*`); FX presets + param metadata (`TrackFX_GetParamEx`, real units);
envelope automation write; send extended props (mute/phase/mono/sendmode); project extras
(dirty/length/notes); MIDI section catalog (`?section=midi_editor`); catalog
`interactive`/`modal` flag; `/capabilities` additions (markers, tempo, undo status).

### Epic 2 — Tier-B/C content manipulation ([#17](https://github.com/braveness23/reaclaw/issues/17))

Items are read-only today. Item CRUD (create/move/split/trim/delete); audio source
metadata (`GetMediaSource*`); take properties (vol/pan/pitch/rate); track extras
(phase/nchan/dual-pan/MIDI routing); FX copy + online/offline; item selection write;
per-project ext state. Tier-C (take FX, MIDI note CRUD, multi-project) deferred within the
epic.

### Epic 3 — Audio perception: the agent hears itself ([#18](https://github.com/braveness23/reaclaw/issues/18))

The headline differentiator. Built-in audio analysis (track peak metering via
`Track_GetPeakInfo` as the cheap first step; loudness/true-peak/RMS/spectral/onsets/clip),
always available; consequence-aware hints (~10–20 hand-authored invariants) inline on
mutating responses. Results tagged exact-introspection vs. estimated-DSP with confidence.

### Epic 4 — Visual perception & musical probes ([#19](https://github.com/braveness23/reaclaw/issues/19))

When a picture beats numbers, and for musical attributes analysis can't cheaply derive.
Audio visualization (spectrogram/EQ-curve/loudness-contour/waveform) with a machine-readable
**digest** + **A/B diff**; screenshot ergonomics (named surface targets + auto-framing —
the raw capability is already proven and baked into the Skill); musical-attribute probes
(key/tempo/pitch via optional external tool, graceful when absent; "probe = the measure
counterpart of an action"). Also absorbs the remaining `/screenshot` + recipes slivers from
#9/#10.

### Epic 5 — Learned suggestions: the compounding moat ([#20](https://github.com/braveness23/reaclaw/issues/20))

Mine `(call → result → correction)` history for "after X, agents usually do Y"; surface as
suggestions tagged *learned* with confidence. **Local-first, opt-in, never phones home.**
Distinct from Epic 3's hand-authored hints though they share the suggestion channel.

---

## 4. Cross-cutting concerns

- **Snapshot / state-diff layer.** Both Epic 4's A/B visual diff and Epic 5's
  correction-mining presume the ability to capture and compare session snapshots over time.
  Build it once, shared.
- **`/capabilities` manifest.** Keep it honest as the surface grows — it currently omits
  markers, tempo map, and undo-grouping status. Agents should never discover support by
  trial and error.
- **Undo-safety status.** Once undo grouping lands (Epic 1), the agent should be able to
  tell whether a mutation is undo-safe.
- **Token economy** (applies to all perception work). Prefer targeted over total (one
  surface, one band — not the whole screen); prefer a number/digest over an image when a
  number suffices; keep heavier feedback opt-in.
- **Compounding libraries.** Actions, probes (Epic 4), and learned hints (Epic 5) all
  *grow* with use — that accumulation is the point, not any single feature.

---

## 5. Open questions (kept implementation-agnostic)

- **Long-render UX.** When the analyzed/visualized material is long, feedback can't be
  instant — wait? poll? fast-path short clips? (Epic 3/4.)
- **Probe modeling.** Is a "probe" a first-class concept or a flavor of the existing action
  machinery? (Epic 4.)
- **Where musical attributes live.** Are key/tempo/pitch part of the general analysis
  surface or their own thing? (Epic 3/4.)
- **DSP locus.** Heavier analysis in-process vs. handed to a dedicated analyzer — a
  footprint/complexity tradeoff. (Epic 3.)
- **Coverage philosophy gate.** `TECH_DECISIONS.md` §16 settled tiered coverage; reaffirm
  it shapes how Epic 1/2 verbs and Epic 4's probe-introspection source are exposed.

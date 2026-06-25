# SWS / S&M Extension — Deep Dive (for ReaClaw)

> **Status:** research / reference. Not a commitment. Feeds the perception/learning
> backlog in [`../../ReaClaw_IDEAS.md`](../../ReaClaw_IDEAS.md) (esp. Q1 audio
> analysis, Q4 visualization, Q10 A/B "taste" loop) and the control-coverage half.
>
> **Author:** consultant deep-dive, 2026-06-24. Grounded in the **live action
> catalog** on the Pi rig (SWS v2.14-era build, `reaper_sws-aarch64.so`) plus the
> SWS project docs. Where a claim is about API signatures rather than the
> catalog, it's flagged "verify against live install."

---

## TL;DR — why this matters to ReaClaw

SWS is the largest, most mature quality-of-life extension in the REAPER world. On
our rig it adds **2,425 actions** to the catalog (total 6,747 with SWS on vs
~4,319 with it off). But the *actions* are only half the story — SWS also exports
a large **ReaScript/C++ API** (function families `SNM_`, `BR_`, `NF_`, `CF_`,
`ULT_`, `SN_`) that ReaClaw can call **directly through the Lua escape hatch
today**, with no SWS-specific code in our extension.

The single highest-value find for the "taste" thread: **SWS already ships a full
EBU R128 loudness engine** (`SWS/BR: Analyze loudness…` + the `NF_*`/`BR_*`
loudness API). That is most of Q1 ("the agent can hear itself") — integrated
LUFS, short-term/momentary, true peak, LRA — **without us writing any DSP.**

> **Framing.** SWS is an open-source, donationware, multi-author community
> project (Jeffos, Breeder, Xenakios, Adam Wathan, FNG, wol, NF, and others —
> credited throughout). ReaClaw's posture is to **work in concert with it**:
> integrate, feature-detect, credit, and degrade gracefully when it's absent —
> standing on the shoulders of that ecosystem, never extracting from it.

---

## 1. What SWS is

SWS/S&M is a collaborative open-source bundle of features that integrate into
REAPER as native-feeling actions, windows, and a scripting API. Donationware,
cross-platform (Win/macOS/Linux incl. aarch64 — which is why it runs on our Pi),
and as of v2.14 installable/updatable **via ReaPack**.

- Home: <https://sws-extension.org/>
- Source: <https://github.com/reaper-oss/sws>
- User manual ("REAPER Plus! The Power of SWS Extensions", G. Francis):
  <https://sws-extension.org/download/REAPERPlusSWS171.pdf>

It's an umbrella of contributor namespaces, visible in every action label:

| Prefix | Author / lineage | Catalog count | Flavor |
|---|---|---:|---|
| `SWS/S&M:` | Jeffos (S&M) | 780 | FX-chain ops, Live Configs, Region Playlist, Cycle Actions, Find, config |
| `SWS:` | core team | 591 | Snapshots, markers/regions, color, zoom, RMS analyze/normalize |
| `SWS/BR:` | Breeder | 588 | Envelopes, **loudness (R128)**, tempo, mouse/contextual, markers-from-notes |
| `Xenakios/SWS:` | Xenakios | 244 | Take/item batch ops, rename/label, nudge, templates (many deprecated) |
| `SWS/AW:` | Adam Wathan | 74 | Metronome, timebase, fades, record-arm helpers |
| `SWS/FNG:` | FNG | 71 | MIDI "FNG" tools, groove extract/apply, item-as-MIDI |
| `SWS/wol:` | wol | 48 | TCP/MCP layout, focus, zoom helpers |
| `SWS/NF:` | NF (cfillion-adjacent) | 13 | Notes, loudness precision toggles |
| others (`PADRE`,`Shane`,`IX`,`SN`,`gofer`) | — | ~16 | misc |

---

## 2. The catalog grounding (what's actually on the rig)

Counts are live from `reaclawdb.sqlite` (SWS enabled), matched by action-label
family + topic keyword. Topic counts overlap (an action can match several).

| Cluster | ~Actions | Catalog-confirmed examples |
|---|---:|---|
| **Envelopes** (BR) | 281 | shift/scale points, select peaks, fit, curve, vertical zoom-to-env |
| **Items/takes** | 545 | organize, normalize, fades, take cycling, item-as-source |
| **Selection logic** | 1227* | select by color/folder/property, grow/shrink, save/restore selection |
| **Cursor/mouse context** (BR) | 150 | "…under mouse cursor", contextual toolbars, action-marker-under-cursor |
| **Markers/Regions** | 118 | marker list, save/load marker **sets**, convert markers↔regions, renumber, export formatted list |
| **Live Configs** | 113 | 8 configs × monitoring/apply/step (use REAPER like an FX pedal live) |
| **Color/Auto-color/Icon** | 87 | auto color/icon/layout rules, random/gradient color, color mgmt window |
| **Zoom** | 65 | zoom to tracks/items/time-sel, minimize others, toggle |
| **Snapshots** | 62 | save/recall track-parameter snapshots (vis, mix, full) |
| **Templates/Resources** | 204 | Resources window: FX chains, track/project templates, media, themes |
| **Notes** | 40 | track/item/project/region/subtitle notes, markers-from-notes |
| **Loudness/RMS/normalize** | 22 | **R128 analyze**, normalize to LUFS/LU, RMS analyze/organize |
| **Cycle Actions** | 3 | the macro/conditional-action *editor* (defines hundreds more) |
| **Label processor/rename** | 13 | wildcard rename of tracks/takes/markers, prefix/suffix |

\* "select" is a keyword match and overcounts; treat as "selection is a huge SWS
theme," not a literal feature count.

---

## 3. Feature deep dive — the quality-of-life that matters

### 3.1 Loudness & analysis — the headline for ReaClaw ⭐
`SWS/BR: Analyze loudness…` runs **EBU R128**: integrated LUFS, short-term &
momentary, **LRA** (loudness range), and **true peak**. There are one-shot
normalize actions (`…to -23 LUFS`, `…to 0 LU`) and precision/dual-mono toggles.
Separately, the older core `SWS: Analyze and display item peak and RMS` plus
"organize items by peak/RMS" give cheap level stats.

**ReaClaw relevance:** this is ~80% of Q1 for free. Combined with our offline
render (Q9/Epic 6), the maker→critic loop (Q10) gets a real, standardized number
to prefer on: "B is −1.8 LU louder and 0.4 dBTP hotter than A." See §4 for the
API form that returns these as data instead of a dialog.

### 3.2 Snapshots — directly the "snapshot layer" ReaClaw assumes
SWS Snapshots save **selected track parameters** (volume/pan/mute/solo, FX state,
visibility, full mixer) for later recall, with copy/recall-next/prev. ReaClaw's
ideas queue already names a "snapshot layer" as the substrate for A/B diffs and
learned-correction mining — SWS is a working reference implementation of exactly
that concept (and our own `state_snapshots` table is the homegrown version).

### 3.3 Markers & Regions — set management + formatted export
Beyond REAPER's natives: save/load/delete named **marker sets**, copy/paste
marker sets via clipboard, renumber IDs, convert markers↔regions, and **export a
formatted marker list** to clipboard/file (custom wildcard format). Great for
turning a render into a cue sheet / chapter list — relevant to the podcast &
video-scoring pipelines in Q9.

### 3.4 Region Playlist — non-linear arrangement
Define a playlist of regions and play them in any order (live, smooth-seek), or
**crop/append/paste the playlist into a real timeline** or new project tab.
Effectively "arrange by reference" — compose structure as a list, then bounce it
flat. Interesting for generative/parametric arrangement (Q9).

### 3.5 Auto color / icon / layout — rule-based session hygiene
Rules that auto-color/icon/layout tracks by name (e.g. anything named "Drums" →
red + drum icon + a layout). Plus a pile of direct color actions (random,
gradient, copy neighbor's color). Cheap way for an agent to make a generated
session *legible*.

### 3.6 Cycle Actions — conditional macros (the power user's power tool)
A GUI to build **custom actions** from sequences with state/conditionals (IF
toggle-on … ELSE …). These register as new actions in the catalog. ReaClaw's own
`/execute/sequence` covers the linear case; Cycle Actions are the
stateful/branching superset. Worth knowing they exist; not something to depend on
(they live in the user's REAPER config, not portable).

### 3.7 Notes & Label Processor
Free-text notes attached to track/item/project/region (+ subtitle export), and a
wildcard **label processor** to batch-rename tracks/takes/markers
(prefix/suffix/numbering/source-file rename). Both are reachable as *data* via the
`NF_`/`ULT_` API (§4) — useful for an agent to read/write structured annotations.

### 3.8 FX chain ops (S&M) — copy/paste/clear chains as objects
Copy/cut/paste/clear **entire FX chains** between tracks/items/takes/input-FX, and
manage chain windows. This is the manipulation layer behind "apply this vocal
chain to those 6 tracks" — pairs with the Resources window's saved FX-chain
library.

### 3.9 Tempo / groove (BR + FNG)
Fine tempo-marker editing (gradual/linear, preserve-overall-tempo nudges in
BPM/%), markers↔tempo conversion, and FNG **groove** extract/apply to MIDI/items.
Relevant to Q7 (musical-attribute probes) and rhythmic "feel" as a taste axis.

### 3.10 Mouse-context / contextual toolbars (BR)
A whole family of "do X to whatever is **under the mouse cursor**" and contextual
toolbars. These are *human ergonomics* — low value for a headless agent (no mouse
position), explicitly **out of scope** for ReaClaw's API-driven use.

---

## 4. The programmatic API surface (the part ReaClaw can call **today**)

This is the most ReaClaw-relevant angle and the one a pure "list of actions" view
misses. SWS registers C/ReaScript functions that ReaClaw can invoke through the
existing Lua escape hatch (`/execute` Lua), returning **data**, not just firing
actions. Families and representative functions (⚠ verify exact signatures against
the live install — dump with `CF_EnumerateActions` / the ReaScript API browser):

- **`NF_*` / `BR_*` — loudness & peaks (the Q1 enabler):**
  `NF_AnalyzeTakeLoudness` / `NF_AnalyzeTakeLoudness2` → integrated LUFS,
  short-term/momentary max, LRA, true peak, returned as numbers;
  `NF_GetMediaItemMaxPeak`, `NF_GetMediaItemMaxPeakAndMaxPeakPos`,
  `NF_GetMediaItemAverageRMS`, `NF_GetMediaItemPeakRMS_*`. These return analysis
  **without opening a dialog** — exactly what an agent needs.
- **`SNM_*` — config & object helpers:** `SNM_GetIntConfigVar` /
  `SNM_SetIntConfigVar`, `SNM_GetDoubleConfigVar`, `SNM_MoveOrRemoveTrackFX`,
  `SNM_GetSetObjectState`, FX-chain/preset helpers. The reflective hooks for
  reading/writing REAPER prefs an agent otherwise can't reach.
- **`NF_*`/`ULT_*` — notes as data:** `NF_GetSWSTrackNotes` /
  `NF_SetSWSTrackNotes`, `NF_GetSWSMarkerRegionSub` / `…Set…`,
  `ULT_GetMediaItemNote` / `ULT_SetMediaItemNote`. Read/write the structured
  annotations from §3.7.
- **`CF_*` — files, actions, shell:** `CF_EnumerateActions` (+
  `CF_GetCommandText`) — *this is how our `tools/native_actions.tsv` table was
  generated*; `CF_GetSWSVersion`, `CF_LocateInExplorer`, `CF_ShellExecute`,
  `CF_GetMediaSourceBitDepth`, `CF_GetTakeFX*`. Catalog/file introspection.
- **`BR_*` — envelopes/tempo/items:** `BR_EnvAlloc`/`BR_EnvGetPoint`/
  `BR_EnvSetPoint`/`BR_EnvFree` (read/write envelope points programmatically —
  the data layer under §3.1's automation), `BR_GetMediaItemImageResource`,
  tempo-map helpers, `BR_GetMouseCursorContext` (mouse — skip for headless).
- **`SN_*` — focus/UI:** `SN_FocusMIDIEditor`, misc UI focus.

> **Why this is the big deal:** ReaClaw's TECH_DECISIONS keep the extension thin
> and treat Lua as the long-tail escape hatch. SWS's API means we get a loudness
> analyzer, envelope read/write, notes-as-data, and config introspection **as
> library calls through that existing hatch** — no new C++ in ReaClaw, no new
> dependency to ship (SWS stays the user's optional install, like ReaPack).

---

## 5. Highest-value picks for ReaClaw (mapped to the backlog)

Ranked by leverage for the perception/taste direction:

1. **Loudness API → Q1 "hear itself" / Q10 "taste."** Wrap `NF_AnalyzeTakeLoudness`
   (+ true-peak/LRA) behind a probe (Q7). Gives the A/B critic a standardized
   number to prefer on. *Biggest win, smallest effort.*
2. **Snapshots concept → snapshot layer / Q10.** SWS Snapshots is the reference
   design for capture-and-compare; informs our `state_snapshots` schema and the
   A/B primitive.
3. **Marker-set export → Q9 pipelines.** Formatted marker/region export = cue
   sheets / chapter lists for podcast & video-scoring outputs.
4. **FX-chain copy/paste + Resources library → control coverage (#7 tiered).**
   "Apply saved chain X to these tracks" as a first-class agent move.
5. **Auto-color/icon → session legibility.** Make agent-generated sessions
   human-readable in one action.
6. **Envelope `BR_Env*` API → automation read/write.** Programmatic point-level
   automation beyond what native verbs expose.

**Deliberately skip:** mouse-context/contextual toolbars (no cursor in headless),
Live Configs (live-performance, not production), deprecated Xenakios template
slots.

---

## 6. Caveats

- **Optional dependency, not a hard one.** Per `reaclaw-local-test-rig` memory and
  TECH_DECISIONS, ReaClaw has **no runtime dependency** on SWS and must degrade
  gracefully when absent (mirrors the Q-decision: "advanced audio gated behind an
  optional external tool, skipped when missing"). Any SWS-backed probe must
  feature-detect (e.g. `CF_GetSWSVersion`) and fall back.
- **Catalog churn.** Toggling SWS changes the action set (4,319 ↔ 6,747) and
  forces a catalog rebuild (clear `meta` rebuild markers while REAPER is stopped).
  Numeric command IDs are **not stable across installs** — match SWS actions by
  *label* (the `SWS…:`/`Xenakios…:` prefix), never by raw command number.
- **API signatures drift.** The `NF_`/`BR_` loudness functions have had v1/v2
  variants; confirm names/args against the installed build before wiring a probe.
- **Cross-platform.** SWS runs on the Linux/aarch64 rig, so SWS-backed features
  are viable on the production target, not just on Dave's Windows REAPER.

---

## Appendix — how this report was produced

- Live catalog: `~/.config/REAPER/reaclaw/reaclawdb.sqlite`, `actions` table
  (6,747 rows), matched by label-family regex + topic keywords.
- SWS binary on rig: `~/.config/REAPER/UserPlugins/reaper_sws-aarch64.so`.
- Web: sws-extension.org, github.com/reaper-oss/sws, The REAPER Blog (v2.14).
- To regenerate the SWS API list authoritatively: with SWS enabled, use
  `CF_EnumerateActions` + the ReaScript API browser, or read the SWS source
  (`Breeder/BR_ReaScript.cpp`, `SnM/`, `Fingers/`). See `tools/README.md`.

# ReaClaw — REAPER Coverage Report

> **Measured**, not estimated. Every number here is reproducible from the commands in
> [§1 Methodology](#1-methodology). Companion to `ReaClaw_REAPER_API_ANALYSIS.md` (the
> domain-by-domain gap narrative) and `ReaClaw_ROADMAP.md` (the forward plan). The proposal
> that follows from this report is [§6](#6-proposal--provable--legible-full-coverage).
>
> Generated 2026-06-27 against the vendored SDK (`vendor/reaper-sdk/`, REAPER v7.67 header)
> and `src/` at HEAD.
>
> **STATUS (2026-07-02): Epic #45 complete.** All six §8 sequencing steps shipped (see
> `ReaClaw_IMPLEMENTATION_CHECKLIST.md`); config vars (#44) deliberately deferred to the wish
> list rather than delivered as a typed endpoint (still reachable via the action/Lua tiers, so
> the 100%-reachability goal holds). The §2 headline numbers below are as-measured on the
> generation date; re-run §1's commands for current figures (868 total / 188 called / 21.7% as
> of 2026-07-02) — the live `/capabilities` `sdk` object is the source of truth going forward.

---

## 1. Methodology

Two questions, two reproducible measurements.

**How big is the surface?** — count function-pointer declarations in the SDK header:
```bash
grep -cE '\(\*REAPERAPI_FUNCNAME\(' vendor/reaper-sdk/reaper_plugin_functions.h
# → 865
```

**How much do we call?** — intersect those 865 names against our own source (excluding the
generated native-action *string* table, which would create false hits):
```bash
grep -oE '\(\*REAPERAPI_FUNCNAME\(([A-Za-z0-9_]+)\)\)' vendor/reaper-sdk/reaper_plugin_functions.h \
  | sed -E 's/.*FUNCNAME\(([A-Za-z0-9_]+)\)\)/\1/' | sort -u > /tmp/sdk_all.txt
while read fn; do grep -rqwE "$fn" src/ --include=*.cpp --include=*.h \
  --exclude=native_actions.gen.h && echo "$fn"; done < /tmp/sdk_all.txt | sort -u | wc -l
# → 131
```

Non-function touchpoints (C++ interface classes, registration hooks) are catalogued by hand from
`reaper_plugin.h`, `reaper_vst3_interfaces.h`, `reaper_plugin_fx_embed.h`, `video_frame.h`.

---

## 2. Headline

| Metric | Value |
|---|---|
| SDK functions exposed to a native extension | **865** |
| SDK functions ReaClaw actually calls | **131** |
| Raw function coverage | **15.1%** |
| `*2` / `*Ex` redundant variants in the 865 | 80 |
| Functions the header marks deprecated | 25 |
| Control-surface functions (`CSurf_*`, inverted model) | 54 |
| UI/window/theme/dock functions | ~30 |

**The raw 15.1% is real but misleading**, and this report's first job is to say why. The 734
functions ReaClaw does *not* call are not a uniform "abyss" — they are four very different piles,
and only one of them is a gap worth closing.

---

## 3. The four piles (what the other 85% actually is)

| Pile | Approx. size | What it is | Verdict |
|---|---|---|---|
| **Out-of-scope by design** | ~84 in-header (+ the interface classes in §5) | `CSurf_*` (54) and most UI/window/theme/dock (~30) — plus, *outside* the 865, the C++ interfaces you *implement* (REAPER calls you): control surfaces, PCM source/sink, VST3 host, FX-embed, video. A REST control API never mirrors these. | Declare out of scope. |
| **Redundant variants & deprecated** | 80 variants + 25 deprecated | `*2`/`*Ex` superseding pairs and legacy entries inflate the denominator; calling the modern variant covers the capability. | Discount from the denominator. |
| **Reachable today via action-runner or Lua** | the long tail | The settled tiered model (`TECH_DECISIONS.md`): anything without a verb is still reachable via `POST /execute/action` (~6,700 actions) or `POST /scripts/register` (arbitrary Lua = the full API). Not "uncovered" — just not a *verb*. | Already reachable. |
| **Genuinely missing & valuable** | **6 domains** | MIDI, transport-as-verbs, take-FX, project lifecycle, config vars, state-chunks. | **This is the report's target.** |

> The core header carries **zero** SWS/JS extension functions (`grep -cE '^(JS_|SNM_|BR_|CF_)' → 0`);
> those arrive at runtime from installed extensions and already swell the live action catalog to
> ~6,747. So the action-runner tier is far larger in practice than the 865 number suggests.

---

## 4. Coverage by domain (the honest picture)

Bucketed from the 865 by name prefix/keyword; `used` = called in `src/`.

```
domain                         used/total  pct   verdict
Color                            2/2       100%  done
Project/file                     9/16       56%  strong — lifecycle gap (open/save/new)
Items/takes                     22/44       50%  strong
Undo                             6/13       46%  done (rest are variants)
Selection                        3/7        43%  done
Ext-state/IPC                    3/7        43%  done (cross-ext signaling deferred)
Routing/sends                    6/18       33%  strong (HW-out cat -1 deferred)
Time/Tempo                       8/29       28%  strong
Tracks (core)                    9/37       24%  strong (many niche fields/variants)
Transport/actions                9/41       22%  GAP — no transport VERBS (action-id only)
Audio/PCM/analysis               8/39       21%  strong (the perception engine)
FX (track/take)                 20/97       21%  GAP — take-FX not graduated
Envelopes/Automation             9/48       19%  strong (automation items deferred)
UI/windows/theme                 4/27       15%  mostly out of scope
Markers/Regions                  3/35        9%  done (rest are variants)
Config vars                      0/5         0%  GAP — issue #44
State chunks (RPP)               0/5         0%  GAP — keystone of the proposal
MIDI                             0/53        0%  GAP — biggest real one (Lua-only today)
Control surface (CSurf_)         0/54        0%  OUT OF SCOPE (inverted model)
```

**Read this as domains, not functions.** A domain at 20–50% is not half-built: the unused
functions are overwhelmingly variants, niche fields, and deprecated entries. At the *domain*
level ReaClaw exposes **structured verbs for ~16 of ~20 REST-relevant domains**. The measurable
abyss that matters is **six domains**:

1. **MIDI** — 0/53. The single biggest real gap. Note/CC CRUD is Lua-only today.
2. **Transport-as-verbs** — play/stop/record/cursor/loop are reachable *only* via opaque action
   IDs (1007/1016/1013…); functional but undiscoverable.
3. **Take-FX** — `TakeFX_*` never graduated even though `TrackFX_*` is fully covered.
4. **Project lifecycle** — new/open/save/reset (issues #43, #34).
5. **Config vars** — 0/5; the Preferences/`reaper.ini` surface (issue #44).
6. **State-chunks (RPP)** — 0/5; `GetSetObjectState` is the universal in-SDK escape hatch.

Everything else is either out-of-scope-by-design or already reachable via the action/Lua tiers.

---

## 5. Non-function touchpoints

The 865 functions are not the whole SDK. REAPER also exposes:

| Touchpoint | Header | Scope verdict |
|---|---|---|
| `plugin_register` hooks (~25: hookcommand, timer, gaccel, custom_action, prefpage, projectconfig, pcmsrc/pcmsink, csurf, accelerator…) | `reaper_plugin.h` | **Partial** — ReaClaw already uses `timer` + `custom_action`/script registration. Others mostly out of scope. |
| `IReaperControlSurface` | `reaper_plugin.h` | **Out of scope** — inverted model (REAPER calls you); not REST-mappable. |
| `PCM_source` / `PCM_sink` / `ISimpleMediaDecoder` | `reaper_plugin.h` | **Out of scope** as interfaces; rendering is covered via `/render`. |
| `ProjectStateContext` / state chunks (RPP) | `reaper_plugin.h` | **In scope** — reachable via `GetSetObjectState` (the proposal's keystone). |
| `MIDI_eventlist` / `MIDI_event_t` | `reaper_plugin.h` | **In scope** — backs the MIDI verbs. |
| VST3 host (`IReaperHostApplication`), FX-embed, `IVideoFrame` | misc headers | **Out of scope.** |
| Preferences dialog internals, OSC config, web remote, `.ReaperTheme` editing, MIDI-learn | UI/config only | **Out of scope** (no C-API path; config-var subset addressed by #44). |

---

## 6. Proposal — Provable + Legible Full Coverage

**"Full coverage" deliberately does *not* mean mirroring 865 functions** — `TECH_DECISIONS.md`
settled that as unmaintainable, and §3 shows most of the 865 shouldn't be mirrored anyway.
Instead, *full coverage* = **provable 100% reachability** + **a legible map** + **verbs for the
six gaps that hurt**. All additive; no breaking changes; no major version bump (see §7).

### 6.1 Keystone — a universal state-chunk endpoint
`GET/POST /state/chunk` over **`GetSetObjectState`** for `track | item | envelope | project`.
One endpoint makes the *entire* serialized state of any object readable and writable even with
no dedicated verb. Together with the action-runner (any of ~6,700 actions) and Lua (the full
API), **reachability becomes 100% and provable** — statable as a theorem, not a hope. Writes
wrap in `Undo_BeginBlock2/EndBlock2` (existing pattern).

### 6.2 Graduate the six gaps to structured verbs (discoverability)
| Gap | New surface | Backing | Issue |
|---|---|---|---|
| Transport | `POST /transport {action}`, `/transport/cursor`, `/transport/loop` | `CSurf_OnPlay/Stop`, `SetEditCurPos`, `GetSet_LoopTimeRange2`, `GetSetRepeatEx` | new |
| MIDI | `GET/POST /state/items/{i}/midi` (notes/CC) | `MIDI_*` | new |
| Take-FX | `/state/items/{i}/takes/{t}/fx/…` (mirror TrackFX) | `TakeFX_*` | new |
| Project lifecycle | `POST /project/new\|open\|save\|reset` | `Main_openProject`, `Main_SaveProject` | #43, #34 |
| Config vars | `GET/POST /config/reaper` (allowlist, SWS-backed, graceful degrade) | `get_config_var*` / SWS | #44 |

### 6.3 Make completeness legible (the "intentional & discoverable" win)
- **Coverage matrix in `/capabilities`** — a `coverage` object mapping every domain to
  `{status: structured|action|lua|chunk|out_of_scope, note}`, plus an honest `sdk` summary
  (`{functions_total:865, called:131, reachable:"100% via verbs+actions+lua+chunk"}`). The whole
  map becomes visible; out-of-scope is *declared*, not accidental.
- **A written API-stability / versioning policy** — the one true doc gap (no stability statement
  exists today). SemVer contract (additive→minor, breaking→major), what "stable" means per
  endpoint, deprecation policy. This — not a version number — is what makes the surface feel
  intentional.

### 6.4 Cohesion (assessed)
**Do we need API changes? Are they cohesive?** Yes and yes — every item is **additive** (new
endpoints + new `/capabilities` fields), cohesive via one universal backstop (chunk) + verbs
reusing existing handler/undo/hints patterns + one unifying capabilities matrix. The few
*breaking* cohesion nits are logged, **not** fixed now (parked for a hypothetical 2.0):
`GET /state/tracks` returns `{tracks:[…]}` while single-track POST returns a bare object; track
`icon` writes a relative name but reads back absolute; response/error/hints envelopes drift
slightly across handlers.

---

## 7. Versioning verdict

**No major bump.** Discoverability is already largely handled (tiered model, `/capabilities`,
`/catalog` + semantic search, the agent Skill, consequence-aware hints). Additive expansion is
correct SemVer as **minor** bumps (v1.8.0 →), exactly as v1.3→v1.7 added large surface. The
missing "intentional" feeling comes from the **policy doc + coverage matrix** (§6.3), both
shippable without breaking anything. Reserve 2.0.0 for the day we choose to make the §6.4
breaking cohesion fixes.

---

## 8. Sequencing

Tracked under umbrella epic **#45** ("Full coverage — provable reachability + legible map").
**Epic closed 2026-07-02 — all steps shipped except #44 (deliberately deferred):**

| Step | Issue | Status |
|---|---|---|
| `/capabilities` coverage matrix (+ optional-dep feature detection) | #46 | done |
| Governance policies (deps + API stability/versioning) | #37 | done |
| State-chunk endpoint (keystone) | #48 | done |
| Transport verbs | #49 | done |
| Take-FX verbs | #50 | done |
| MIDI verbs | #51 | done |
| Project lifecycle (new/open/save/load/reset) | #34 | done |
| Config vars | #44 | deferred (wish list — still action/Lua-reachable) |

Order — deliver the "intentional & discoverable" story first, biggest/heaviest last:
**matrix (#46) + policy (#37) + chunk (#48) → transport (#49) → project-lifecycle/config
(#34/#44) → take-FX (#50) → MIDI (#51).** Each shipped as its own additive minor release.

---

*Reproduce every figure: `vendor/reaper-sdk/reaper_plugin_functions.h` for the surface; the
two snippets in §1 for usage; `src/handlers/capabilities.cpp` for the current manifest.*

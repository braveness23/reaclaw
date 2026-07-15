# ReaClaw: Technical Decisions

Key architectural decisions and rationale. Read this before modifying the design.

---

## 1. Implementation Type: Native C++ REAPER Extension

**Decision:** ReaClaw is a native REAPER extension (`.dll` / `.dylib` / `.so`), not an external process.

**Rationale:**
- A native extension runs inside REAPER's process and has direct access to the full REAPER C++ SDK via `GetFunc()` — every function REAPER exposes is available.
- No bridge scripts. No scraping REAPER's web interface. No limitations on what can be queried or controlled.
- Script registration via `AddRemoveReaScript()` is a direct API call.
- Action enumeration is two-sourced:
  - **Native built-in actions** come from a **bundled static ID→name table** (`tools/native_actions.tsv` → generated `src/reaper/native_actions.gen.h`, ~4300 entries). REAPER does **not** expose its native actions through any SDK enumeration call — `kbd_getTextFromCmd`/`kbd_enumerateActions` return nothing for them (verified: SWS off → live enumeration yields only ~10 extension actions, zero native). Native command IDs are stable across REAPER versions/platforms, so one bundled table serves all builds.
  - **Extension/script actions** (SWS, ReaPack, ReaScripts, …) are live-enumerated at startup via `kbd_getTextFromCmd()` + `kbd_enumerateActions()`, so the catalog reflects whatever is actually installed.
- REAPER loads the extension automatically at startup from the `UserPlugins` directory.

**Alternatives Rejected:**
- External Go/Python/Rust process calling REAPER's built-in web interface: Limited to what REAPER's HTTP interface exposes. Requires a "bridge" ReaScript workaround. No native script registration. Wrong architecture for this problem.

---

## 2. Language: C++17

**Decision:** C++17 standard.

**Rationale:**
- REAPER itself is C++. The SDK headers and WDL use C++.
- C++17 gives us `std::filesystem`, `std::optional`, `std::string_view`, structured bindings — reduces boilerplate without extra libraries.
- All chosen vendor libraries (cpp-httplib, nlohmann/json) work with C++17.

**Platform compilers:**
- Windows: MinGW-w64 GCC, cross-compiled from Linux CI (issue #84) — see the
  ABI note below. MSVC also works if building natively.
- macOS: Clang (Xcode)
- Linux: GCC or Clang

**Windows ABI note (revised — the original claim here was overbroad).**
The bulk of the REAPER SDK surface — `reaper_plugin_functions.h`'s `GetFunc()`-loaded
function-pointer table, which is what ~all of ReaClaw's handlers use — is plain C
calling convention, not C++ name-mangled/vtable-dependent. It is compiler-ABI-neutral:
confirmed live by #84's MinGW-w64 cross-compile, which links and produces a working
`.dll` with the full handler surface, no MSVC involved. **The one place true C++ ABI
compatibility genuinely matters is a *virtual-interface* boundary REAPER calls
*into*** — e.g. `IReaperControlSurface` (issue #31's event feed): REAPER's own binary
(commonly MSVC-built on Windows) must agree with the extension's binary on vtable
layout and calling convention to dispatch virtual calls into a subclass correctly. This
is unverified for a MinGW-built `IReaperControlSurface` on Windows specifically — there
is no Windows REAPER install in this environment to test against. Flagged as a known
risk when #31 lands; the mitigation is the same "ship on CI-green, no load-test
available" posture already accepted for the whole Windows binary (#84).

---

## 3. Embedded HTTP/HTTPS Server: cpp-httplib

**Decision:** [cpp-httplib](https://github.com/yhirose/cpp-httplib)

**Rationale:**
- **Header-only** — a single `httplib.h`. Drop it in `vendor/` and include it. No separate build step.
- **HTTPS via OpenSSL** — same library handles both HTTP and HTTPS; just pass cert/key paths to `SSLServer`.
- **Cross-platform** — works identically on Windows, macOS, Linux.
- **Thread pool** — built-in concurrent request handling.
- **Actively maintained** — 13K+ GitHub stars, production use in many projects.

**Alternatives Rejected:**
- **Boost.Beast:** Requires pulling in all of Boost. Overkill.
- **Crow:** Separate build required; more complexity.
- **Pistache:** Linux-only.

---

## 4. TLS: OpenSSL

**Decision:** OpenSSL for all TLS operations.

**Rationale:**
- cpp-httplib's HTTPS support is built on OpenSSL — shared dependency, no extra library.
- Available on all target platforms via system packages or vcpkg.
- Supports both self-signed (auto-generated on first run) and CA-signed certificates.

**Self-signed cert generation:** Use `EVP_PKEY_keygen` + X509 APIs in C++ at startup if no cert is configured. Stored in `{ResourcePath}/reaclaw/certs/`.

---

## 5. Persistence: SQLite Amalgamation

**Decision:** SQLite, distributed as the official C amalgamation (`sqlite3.c` + `sqlite3.h`).

**Rationale:**
- **Zero infrastructure** — single file database; no server process.
- **Bundled as source** — two files added directly to `vendor/`. No external dependency.
- **ACID** — reliable transactions; safe for concurrent reads.
- **Fast** — catalog lookups, script caching, and execution history are read-heavy, low-volume workloads SQLite handles well.
- **Easy backup** — copy one file.

SQLite is always sufficient for this use case. ReaClaw is a single-machine REAPER extension; there is no multi-server scenario.

---

## 6. JSON: nlohmann/json

**Decision:** [nlohmann/json](https://github.com/nlohmann/json) (single header `json.hpp`)

**Rationale:**
- Header-only — drop in `vendor/` and include.
- Handles all API request/response serialization and config file parsing.
- Intuitive API: `json["key"] = value`.

**Config format:** JSON, not YAML. nlohmann/json handles it; a YAML parser (yaml-cpp) would require building a separate library.

---

## 7. Build System: CMake

**Decision:** CMake 3.20+

**Rationale:**
- De facto standard for cross-platform C++ projects.
- Supported by all major IDEs (Visual Studio, VS Code, CLion, Xcode).
- Works with MSVC, Clang, and GCC.
- vcpkg integration for OpenSSL via `CMAKE_TOOLCHAIN_FILE`.
- `install` target copies the built extension directly to the REAPER `UserPlugins` directory.

---

## 8. Threading Model: Command Queue

**Decision:** Background server thread pool with a main-thread command queue for non-threadsafe REAPER calls.

**Rationale:**
The REAPER SDK distinguishes threadsafe from non-threadsafe API functions. `Main_OnCommand` (action execution) must be called from REAPER's main thread. Most state reads (`CountTracks`, `GetTrack`, etc.) are threadsafe.

- cpp-httplib runs its thread pool on background threads.
- Threadsafe state reads are made directly from handler threads — zero added latency.
- Non-threadsafe calls are posted to a `std::queue<Command>` protected by `std::mutex`.
- A REAPER timer callback (registered via `plugin_register("timer", ...)`) drains the queue on the main thread at ~30fps.
- Each queued command carries a `std::promise<Result>`; the handler blocks on the `std::future` with a 5s timeout.

**Trade-off:** ~33ms maximum added latency for action execution (one timer tick). Acceptable for this use case.

---

## 9. REAPER API Binding Strategy

**Decision:** Load all needed REAPER API functions at startup via `rec->GetFunc(name)` using `#define REAPERAPI_IMPLEMENT` before including `reaper_plugin_functions.h`.

**Rationale:**
- This is the canonical approach used by SWS and other major REAPER extensions.
- Populates global function pointers for all REAPER API functions in one call (`REAPERAPI_LoadAPI`).
- Log a warning for any pointer that comes back null (indicates older REAPER version missing that API).

**Key functions used:**
- `kbd_enumerateActions`, `SectionFromUniqueID` — catalog
- `Main_OnCommand`, `Main_OnCommandEx` — execution (main thread only)
- `AddRemoveReaScript` — script registration
- `CountTracks`, `GetTrack`, `GetTrackName`, `GetSetMediaTrackInfo` — track state
- `GetProjectTimeSignature2`, `GetCursorPosition`, `GetPlayState` — project state
- `GetResourcePath`, `GetAppVersion`, `ShowConsoleMsg` — utility

---

## 10. Script Validation: Syntax Only

**Decision:** Validate Lua syntax before registration. No static analysis. No approval gate.

**Rationale:**
- The agent submitting a script is a trusted AI system — that's the entire premise of ReaClaw.
- Syntax validation exists to catch generation errors (malformed output, truncated response) before they reach REAPER, not to second-guess the agent's intent.
- Static analysis (warning on `os.execute`, etc.) is noise when the agent put it there intentionally.
- An approval gate defeats automation entirely.

**Implementation:** Shell out to `luac -p {file}` if available; lightweight fallback otherwise. Return error with line number on failure; do not register.

---

## 11. No LLM Client in the Extension

**Decision:** ReaClaw does not call any LLM API. The agent generates scripts; ReaClaw registers them.

**Rationale:**
- The agent calling ReaClaw is already an LLM. Having the extension call an LLM on behalf of an LLM is architectural redundancy.
- Removing the LLM client eliminates: outbound HTTP code, API key management in the extension, provider abstraction (Anthropic/OpenAI/etc.), and the associated config section.
- The correct flow: agent generates Lua → `POST /scripts/register` → ReaClaw validates syntax and calls `AddRemoveReaScript`.

**Narrow exception: §25 (issue #10).** Optional, off-by-default, loopback-only embedding
calls to a local Ollama for catalog *search ranking* — not a generative LLM, not agent-facing
generation, opt-in twice over. See §25 for the full reasoning on why this doesn't reopen the
door this decision closes.

---

## 12. Authentication: Two Modes Only

**Decision:** `none` or `api_key`. No mTLS.

**Rationale:**
- ReaClaw runs on the same machine as REAPER. The realistic auth scenarios are:
  - `none`: localhost-only access, trusted environment
  - `api_key`: home network or any case where a simple shared secret is sufficient
- mTLS (client certificates, CA setup) is never going to be used for a personal REAPER extension. It adds certificate management complexity with no practical benefit.

---

## 13. No Rate Limiting

**Decision:** No rate limiting.

**Rationale:**
- ReaClaw is a single-user tool. One person and their AI agent use it.
- Rate limiting defends against external attackers abusing the API. If attackers can reach your REAPER extension's port, rate limiting is not the right defense — network isolation is.
- Removing it eliminates middleware code, per-IP sliding window tracking, and config options.

---

## 14. Plugin Naming and Location

**Decision:** Plugin named `reaper_reaclaw` with platform-appropriate extension.

**Rationale:** REAPER extensions conventionally use the `reaper_` prefix (e.g., `reaper_mp3`, `reaper_sws`), signaling to REAPER that this is an extension rather than a VST/CLAP plugin.

**Output files:**
- Windows: `reaper_reaclaw.dll`
- macOS: `reaper_reaclaw.dylib`
- Linux: `reaper_reaclaw.so`

**Install path:**
- Windows: `%APPDATA%\REAPER\UserPlugins\`
- macOS: `~/Library/Application Support/REAPER/UserPlugins/`
- Linux: `~/.config/REAPER/UserPlugins/`

---

## 15. Config Location

**Decision:** `{GetResourcePath()}/reaclaw/config.json`

**Rationale:** `GetResourcePath()` returns the user's REAPER config directory on all platforms. Storing ReaClaw's config there keeps everything alongside other REAPER configs. ReaClaw creates the `reaclaw/` subdirectory and writes defaults if the file is missing.

---

## 16. API Coverage: Tiered

**Decision:** ReaClaw offers **tiered** coverage of REAPER, not a single uniform layer:

1. **Structured verbs** for the objects agents touch constantly — tracks
   (create/delete; writable name, color, folder depth, vol, pan, mute, solo,
   arm), track FX (add by name, enable, parameter get/set, delete), routing
   (sends add/delete + readable in `/state/tracks`), and selection.
2. **Action-runner** (`/execute/action`, `/execute/sequence`) over the full
   bundled + installed action catalog (`/catalog/search`) for the long tail.
3. **Lua escape hatch** (`/scripts/register`) for anything not exposed as a verb
   or action.

`GET /capabilities` advertises which tier covers what, so an agent can tell at a
glance what is directly supported vs. what needs an action or a script.

**Rationale:** A real session (the 36-track friction log, #9) showed that the
five-field track write was far too thin — naming, FX insertion, folders, color,
and routing all had to be coerced out of parameterless action IDs or hand-written
Lua, costing 146 calls and many dead ends. Mirroring the entire ~865-function
SDK as REST is the opposite mistake: a large, low-value maintenance surface.
Tiered coverage targets the common 20% with typed verbs while keeping the action
and Lua layers for everything else. Settled with Dave on 2026-06-20 (resolves the
open question in issue #7). See `ReaClaw_IDEAS.md` and the Phase 4 checklist.

**Graduated to verbs (Epic #16, v1.3.0):** markers/regions, tempo/time-signature
map (read + add) with beat↔time utilities, and envelope point writes now have
structured endpoints — the API-surface analysis (`ReaClaw_REAPER_API_ANALYSIS.md`
Part 4) and real usage showed these are session primitives reached for constantly,
so the cost of a thin verb is justified. Epic #16 also added undo grouping (every
mutation is now a single undoable step), FX presets + real-unit param metadata,
send extended props (mute/phase/mono/mode), project extras (dirty/length/notes),
and a MIDI-editor catalog section.

**Graduated to verbs (Epic #17, v1.4.0):** media items/takes/sources. Items moved
from read-only to full CRUD (create/move/split/trim/delete, position/length/fade/
vol/mute writes), with take properties (vol/pan/pitch/playrate/preserve-pitch) and
source metadata (file/type/length/sample-rate/channels) on reads. Also graduated:
track extras (phase, channel count, pan mode + dual-pan, rec input, MIDI hw out,
parent send), FX copy-to-track and online/offline, item selection write, and a
persistent per-project ext-state scratchpad (`/project/extstate`). Justified the
same way as Epic #16 — these are the objects *inside* a track that a real arranging
session manipulates constantly, so a thin typed verb beats coercing them out of Lua.

**Tier-C deferred within Epic #17 (still reach via action or script):** take FX
chains (`TakeFX_*`), MIDI note/event CRUD (Lua already covers it well), and
multi-project support. Lower frequency / higher complexity; the Lua escape hatch
is the right layer for now.

**Still not in scope as verbs (reach via action or script):** MIDI events,
render/freeze, project open/save. These may graduate later if usage warrants.

---

## 17. Audio Perception: built-in, exact-where-possible, tagged (Epic #18)

**Decision:** ReaClaw analyses audio **in-process**, built-in and always
available (no external tool), and **tags every measure** with a `method` and
`confidence` so the agent trusts each number appropriately:

- **Loudness / level — exact.** LUFS-I, RMS-I, peak, and true-peak come from
  REAPER's own offline analyser (`CalculateNormalization`, full decode). Tagged
  `offline_analysis`, confidence 1.0. Clipping is `derived` from true-peak.
- **Spectral balance — estimated.** A rough low/mid/high band-energy digest +
  spectral centroid, computed by decoding samples (`PCM_source::GetSamples`)
  through a small in-tree FFT. Tagged `estimated_dsp`, confidence 0.6 — a digest,
  not a calibrated analyzer.
- **Metering — introspection.** `/state/meters` reads REAPER's live meters
  (`Track_GetPeakInfo`/`Track_GetPeakHoldDB`); only meaningful while audio runs.

**Consequence vs. observation (from IDEAS).** Analysis endpoints are *independent
observations* the agent asks for. **Hints** are the *consequence of a specific
edit* and ride inline on that edit's mutating response (`hints[]`) — a small
hand-authored invariant set, distinct from (though sharing a channel with) the
future learned suggestions of Epic #5.

**Rationale:** "Most REAPER bridges are blind command pipes." The cheap, exact
loudness path already existed in the SDK, so it would be perverse to estimate it;
spectral has no built-in primitive, so a tagged estimate is the honest answer.
Token economy (IDEAS): prefer a number/digest over an image, targeted over total,
heavier feedback opt-in (`measures=` filter, `start`/`end` windowing).

**Open questions (carried, deliberately unresolved):**
- **DSP locus.** Analysis runs on the main thread (SDK safety) inside a 30 s
  budget; a long source can freeze the UI briefly or `408`. A dedicated
  out-of-process analyzer would fix both at a footprint/complexity cost — revisit
  if long-render analysis becomes common.
- **Onset / density** detection is deferred (transient analysis is higher
  complexity and outside #18's acceptance criteria).

**Visual perception & probes (Epic #19) — graduations:**
- **Pitch & key are built-in, not gated.** The 2026-06-20 "advanced optional"
  decision assumed musical attributes needed an external tool. In practice pitch
  (FFT dominant-fundamental) and key (chromagram + Krumhansl–Schmuckler) fall out
  of the *same decode + FFT* the spectral digest already uses, so they ship
  built-in as `estimated_dsp`. The optional external tool stays the path for
  detection that genuinely needs more — **tempo-from-audio** (`bpm-tools`'
  `bpm-tag`), which is `available:false` when absent. Tempo also has an exact
  `introspection` source: the project tempo at the item's position.
  - *Pitch caveat:* a harmonic product spectrum was tried first but collapses on
    pure tones (no 2f/3f energy → a subharmonic whose 3rd harmonic hits the peak
    wins). Reverted to plain peak-picking + a guarded sub-octave correction.
- **A probe is a flavour of the analysis surface, not a registry.** The open
  question ("first-class probe vs. action machinery") is resolved pragmatically:
  probes are a `/analysis/.../probe` sub-resource returning tagged data. A
  registerable, user-authored probe *library* (the "compounding" aspiration) is
  deferred until there's demand — building the registry now would be speculative.
- **A/B visual diff is deferred to the shared snapshot/state-diff layer. ✅ Shipped
  (issue #53, §24).** Both Epic #19's A/B diff and Epic #20's correction-mining
  needed session snapshots over time; that layer was built once and shared
  (ROADMAP §4), so the diff landed with it rather than inside #19. #19 shipped
  visualization, screenshot ergonomics, and probes; the diff was the one carried
  sliver — now done via `GET /snapshot/diff/visualize`.

---

## 18. Learned Suggestions & the Snapshot Layer (Epic #20)

**Decision:** ReaClaw learns from its own use — locally, opt-in, never phoning
home — and the learning is built on a **shared snapshot / state-diff layer** that
also retro-unlocks the #19 A/B diff.

- **Snapshot / state-diff layer (the cross-cutting prep).** A canonical,
  diff-stable JSON slice of project state is captured on the main thread, stored
  in `state_snapshots`, and diffed by a pure header-only recursive differ
  (`util/jsondiff.h`) into `[{path, op, from, to}]`. Built once, shared by #20's
  correction mining and #19's A/B visual diff. The snapshot is a *focused* slice
  (not the full `/state` payload) so diffs stay legible and stable across reads.
- **Learning model — pairwise association, not ML.** Each structured edit is an
  event; transitions (antecedent → consequent) by the same agent within a window
  accumulate counts; `confidence = P(consequent | antecedent)`, gated by
  `min_support` + `min_confidence`. This is a transparent, inspectable
  association-rule layer — no model files, no opaque weights. Heavier mining
  (multi-step, value-aware "corrected *to* Y") is deferred until the simple layer
  proves its worth; the acceptance criteria only need pairwise.
- **Local-first & opt-in are non-negotiable (from IDEAS Q8).** Off by default
  (`learning.enabled=false`); while off, `note()` is a no-op and nothing is
  recorded. All state is in the same local SQLite DB — there is **no network
  egress anywhere in the extension** (settled in §11: no LLM client, no outbound
  calls), so "nothing leaves the machine" is structural, not a promise.
- **Shared channel, distinct source.** Learned suggestions ride the same
  suggestion idea as #18's hints but are tagged `method:"learned"` (vs. the
  hand-authored invariants), so an agent can weight them differently. They are
  **advisory, never automatic** — ReaClaw suggests, the agent decides.

**Open / deferred:** value-aware corrections and multi-step workflow mining;
richer edit coverage (every mutating verb emits an event — currently the
high-traffic track/FX/send/item set); surfacing learned suggestions inline on
mutations (today they are a pull via `/suggestions`).

---

## 19. Programmatic Production: Offline-First Render Engine (Epic #32)

**Decision (direction; PoC proven 2026-06-24, not yet built as first-class verbs):**
ReaClaw is, beside control and perception, a **headless audio-production backend** —
a `composition spec → mastered file` function — and that production path is
**offline-first**.

- **Offline, not realtime.** Render is decoupled from the audio clock and runs
  faster than real time. A 7-track API-built composition rendered offline to a
  24-bit/44.1k WAV in 0.36 s for 8 s of audio (~20×+) on the headless Pi rig. The
  headless host needs **no GPU and no audio hardware** — audio device = *Dummy*;
  REAPER only needs an X server (e.g. Xvfb) to host the GUI process. The realtime
  path (PulseAudio null sink + `x11grab`) exists **only** for producing a *video* /
  for live human observation, and is incidental to production. (See `demos/`.)
- **Already works via the escape hatch; the epic makes it first-class.** Today
  rendering is driven by Lua (`GetSetProjectInfo_String` RENDER_* + action
  `41824`). The fiddly part is `RENDER_FORMAT` — REAPER encodes the codec as a
  base64 blob. A first-class `/render` verb MUST hide that behind named params
  (`{format, srate, bit_depth, channels, bounds, output, normalize?}`); callers
  never construct the blob. This is consistent with §16 (tiered coverage:
  structured verbs over the escape hatch for common operations).
- **Same trust + locality model.** Render/save/open inherit the existing stance —
  the agent is trusted (§10, no approval gate), and there is **no network egress**
  (§11), so a production pipeline stays local by construction. Output paths are
  caller-specified files on the local box; uploading anywhere is an explicit,
  separate step (e.g. the `gdrive` skill), never implicit in a render.
- **Long renders are jobs, not blocking calls.** A long project or a batch can
  exceed an HTTP timeout and would otherwise hold REAPER's main-thread command
  queue (§8); the render-job model (#35) returns a handle + pollable status. This
  is the settled answer to the long-standing "long-render UX" open question.

**Open / deferred:** the concrete `/render`, project save/load/open, and async-job
API contracts are designed when Epic #32 is picked up (children #33–#36); multi-
project handling for `/project/open` touches the deferred Tier-C area (§16).

**CI E2E render smoke test (#36) needs no REAPER license.** Confirmed live: a
completely fresh, never-before-run REAPER install (`~/opt/REAPER` inside a
throwaway CI container) launches cleanly with `-nosplash` and renders correctly
with no blocking evaluation/nag dialog — REAPER's eval mode is fully functional
for offline rendering, and an ephemeral CI container never accumulates the
30/60-day eval clock a persistent install would. This is why the smoke test
(`.github/workflows/ci.yml`'s `e2e-render-smoke` job, `demos/scripts/
ci_smoke_test.py`) downloads and installs REAPER fresh per run rather than
needing Dave's personal license baked into the runner image — avoiding both a
licensing concern and a shared-runner-image change.

**CI E2E smoke test needs `reaper.ini`'s `[verchk]` timestamp pre-seeded, or
REAPER's main thread wedges permanently on first launch.** Root-caused via an
instrumented `Executor::tick()` build (a temporary log line on every tick,
compiled and run on a throwaway debug pod against the real self-hosted
runner's image): `plugin_register("timer", ...)` succeeds and `tick()` fires
normally for the first few seconds, then **stops forever** — not slowdown,
complete silence — even with zero commands queued. `xwininfo` showed the
cause: on a truly virgin config with real internet egress, REAPER's own
outbound "check for updates" call succeeds and pops a modal "REAPER New
Version Notification" window. With no window manager ever running under
Xvfb, nothing dismisses it, and that dialog's modal message loop blocks
REAPER's main thread — and therefore the "timer" plugin hook that drives
`Executor::tick()` — indefinitely. Confirmed by dismissing the window by hand
via `xdotool`: `tick()` resumed immediately. A long-lived local install never
hits this because its `reaper.ini` already has a real cached check timestamp
from prior actual use — only a genuinely first-ever launch with working
network access triggers it. Fix: write `~/.config/REAPER/reaper.ini` with
`[verchk]\nlastt=<now>` *before* REAPER's first launch, so it believes it
just checked and skips the network call (and the dialog) entirely.

---

## 20. Dependency Policy: Deliberate & Tiered (issue #37)

ReaClaw stays deliberately thin: vendored core libraries only, a Lua escape hatch for the
long tail, and **no runtime dependency** on REAPER-side extensions. SWS was used *transiently*
to generate the native-action table (`tools/`) and is never shipped or required. Before leaning
on anything external we apply a deliberate policy, not ad-hoc decisions.

**Four kinds of dependency, not equal:**
1. **Vendored / build-time libs** linked into the `.so` (cpp-httplib, SQLite, OpenSSL,
   nlohmann/json) — ship inside us.
2. **Optional REAPER-side extensions** (SWS, ReaPack) — live in the user's REAPER, called via
   the SDK/Lua; we ship nothing but must feature-detect.
3. **Optional external tools/processes** (ffmpeg, a key/tempo analyzer) — user-installed,
   path-configured, skipped when absent.
4. **Network / cloud services** — **forbidden** (local-first; no phoning home; no LLM client, §11).

**Tiered rule:**
- **Tier 0 — Core (required, vendored):** the minimum to *be* ReaClaw. Pinned, license-cleared, in-tree.
- **Tier 1 — Always-on built-ins:** Tier 0 only, no external dep.
- **Tier 2 — Enhanced via optional REAPER extensions (e.g. SWS):** feature-detected
  (`CF_GetSWSVersion`), graceful fallback, **never required**. `/config/reaper` (#44) is the
  reference Tier-2 case.
- **Tier 3 — Enhanced via optional external tools:** opt-in, configured, skipped when absent.
- **Hard rule:** the core path must never require anything above Tier 0.

**Decision checklist for any new dependency:** necessity · optionality (core stays clean?) ·
**license compatibility** (SWS is GPL — calling its runtime-registered API differs legally from
linking it) · feature-detection + graceful degradation · cross-platform incl. aarch64 ·
version-drift handling · CI reproducibility (Linux k3s runners) · maintenance/abandonment risk ·
security surface (per `SECURITY.md`). Feature-detection is *reported* via `/capabilities` (#46)
so agents branch instead of probe-and-fail. Reference research: `docs/research/SWS_DEEP_DIVE.md`.

## 21. API Stability & Versioning Policy (issue #37)

ReaClaw follows [SemVer](https://semver.org/). This section makes the contract explicit.

- **PATCH (x.y.Z):** bug fixes, doc/internal changes; no API surface change.
- **MINOR (x.Y.0):** **additive** API changes — new endpoints, new optional request fields, new
  response fields, new `/capabilities` entries. Additive changes are *backward compatible* and
  are how ReaClaw grows; v1.3→v1.7 added large surface this way, and the full-coverage
  expansion (epic #45) continues to land as MINOR bumps.
- **MAJOR (X.0.0):** **breaking** changes only — removing/renaming an endpoint or field,
  changing a response shape or an error contract, or tightening validation in a
  backward-incompatible way.

**What "stable" means:** any endpoint documented in `docs/API.md` and advertised in
`GET /capabilities` is stable — it will not break except in a MAJOR release, with deprecation
notice first (see below). The Lua escape hatch (`/scripts/register`) and raw action IDs
(`/execute/action`) are inherently version-coupled to REAPER itself and carry no stability
guarantee beyond "the call is accepted."

**Deprecation:** before a breaking change, the old form is marked deprecated in `docs/API.md`
and (where practical) flagged via a `hints[]` entry on the response for at least one MINOR
release before removal in the next MAJOR.

**No 2.0 for additive growth.** Aggressively expanding coverage does **not** justify a major
bump — additive surface is correct as MINOR. A 2.0.0 is reserved for the day we choose to make
the cohesion fixes catalogued in `ReaClaw_COVERAGE_REPORT.md` §6.4 (response-envelope
normalization, relative/absolute icon symmetry, unified error/hints shapes).

---

## 22. Process Restart: `/proc` argv/environment replay, no SDK dependency (issue #77)

**Decision:** `POST /reaper/restart` recovers from a wedged main thread by killing and
relaunching the REAPER process, replaying REAPER's *own currently-running* `argv` and full
environment — read live from `/proc/self/cmdline` and `/proc/self/environ` at request time —
rather than reconstructing a launch command/environment by hand.

**Why this instead of capturing DISPLAY/XAUTHORITY/launch-flags at startup:**
Since ReaClaw is a shared library loaded *inside* REAPER's process, `getpid()` inside a
handler *is* REAPER's pid, and `/proc/self/{cmdline,environ}` are REAPER's own argv/environment
— guaranteed valid because REAPER is running successfully with them right now. This avoids the
exact failure mode issue #77 named (relaunching from a *different* shell context loses X auth —
independently reproduced during the #64/#77 friction-test work: a plain `DISPLAY=:0.0` relaunch
failed with "Invalid MIT-MAGIC-COOKIE-1 key" once the desktop session had logged out) without
inventing new config for a launch command that ReaClaw has no reliable way to guess anyway (it
doesn't control how it was launched — flags, working directory, etc. are a deployer choice).

**Why the critical path makes zero REAPER SDK calls:** the whole point is recovering from a
wedged main thread, so the restart mechanism cannot depend on `Executor::post` (which blocks on
that same main thread) or any non-threadsafe SDK call. Only the *optional* best-effort project
save (short 5s timeout, skipped entirely if the project has never been saved, to avoid ever
risking a save-as dialog) touches the SDK, and its failure/timeout doesn't block the restart.

**Linux only.** The mechanism is `/proc`-based; macOS/Windows return `501`, consistent with the
existing platform-support precedent in `screenshot.cpp`.

**No approval gate.** Consistent with §10/§12 (agent is trusted, no approval gates exist
anywhere else in the API) — any authenticated caller can trigger it, same as any other
mutating endpoint.

---

## 23. Async Render Jobs: explicit flag, in-memory registry, honest limitations (issue #35)

**Decision:** `POST /render` gains an `async: true` field — not an automatic short-render-
inline heuristic. When set, the endpoint returns `{job_id, status: "queued"}` immediately;
`GET /render/jobs/{id}` polls it, `GET /render/jobs` lists all tracked jobs, `DELETE
/render/jobs/{id}` cancels one. The default (no `async`, or `async: false`) is unchanged —
still fully synchronous, same response shape, same 300s timeout.

**Why an explicit flag over a heuristic:** mirrors the `async` field `POST /execute/action`
already has (`handlers/execute.cpp`). A caller-supplied flag is deterministic; guessing a
"short vs. long" threshold from project length is fragile and surprises callers near the
boundary. Consistency with an existing, working convention beats inventing a new one.

**Execution model — reuses `Executor::post`, doesn't reinvent it.** The async path does NOT
trigger the render via SWELL `SetTimer` (the mechanism `/execute/action`'s async mode uses).
It calls the exact same `Executor::post` path the synchronous render already used, just from
a detached worker thread instead of the HTTP thread — only the *waiting* moves off the HTTP
thread. This gets single-flight serialization for free: `Executor::tick()` drains its FIFO
queue one command at a time, so a second async render request simply sits `"queued"` behind
the first without any additional locking. (`SetTimer` was necessary for `/execute/action`
because that feature needed to survive being called from inside a nested modal loop; render
jobs have no such requirement — they're one atomic unit of work like any other Executor
command.)

**Confirmed live: async does NOT fix cross-endpoint starvation, only the HTTP-timeout
problem.** Built a project with a 1500s item (~200× realtime), fired `POST /execute/action`
5s into a 37s render — it timed out at 15s, never running, well before the render finished.
REAPER's main thread pumps **no message loop at all** during an offline render (`Main_OnCommand
(41824, 0)` is a tight, uninterruptible decode/encode loop) — not even the "timer" plugin hook
that drives `Executor::tick()`. So while a render is actually executing:
- Every other `Executor::post`-based endpoint (most of the API — state writes, sync actions,
  project ops) queues up and can time out, exactly as it did before async jobs existed.
- The async job model's real, honest value is narrower than it sounds: the calling HTTP
  connection doesn't have to stay open for the render's duration, and the caller gets a
  pollable handle instead of guessing a timeout. It does **not** make REAPER's API surface
  stay responsive during a render.
- Fixing that for real means chunking a render into segments with breathing room between
  them for `Executor::tick()` to drain other commands — a much larger change, deferred.
  Revisit if this becomes a real pain point (same "revisit if it becomes common" pattern as
  §17's DSP-locus tradeoff).

**Job storage — in-memory, bounded, not persisted.** A single global mutex guards a bounded
vector of jobs (`kMaxJobs = 200`, oldest *terminal* jobs evicted first — queued/running jobs
are never evicted), matching Executor's own single-mutex, in-memory style. Jobs don't survive
a ReaClaw/REAPER restart any more than Executor's own pending-command queue does; that's an
acceptable loss since the underlying render itself wouldn't survive a restart either.

**No numeric progress.** The REAPER SDK exposes no render-progress primitive (confirmed by
inspecting `reaper_plugin_functions.h` — no progress callback, no percent-complete query for
an in-flight `Main_OnCommand(41824)` render). A running job reports `elapsed_seconds` (honest,
measured) but no percentage (would have to be a guess). Consistent with §17's confidence-
tagging ethos: report what's actually known, don't manufacture a number that isn't.

**Cancellation — only before the render actually starts.** A job's status stays `"queued"`
for as long as it sits in Executor's FIFO queue (even if its worker thread has already called
`Executor::post` and is blocked waiting) and only flips to `"running"` at the instant
`Executor::tick()` dequeues it and starts `do_render` on the main thread. `DELETE` on a
`"queued"` job cancels it cleanly (the render lambda checks a `"cancelled"` sentinel as its
first action and skips `Main_OnCommand` entirely — never touches REAPER's render settings).
`DELETE` on a `"running"` job returns `409` — REAPER's SDK offers no safe way to abort an
in-flight offline render (no cancel API, no way to synthesize the render dialog's Cancel
button non-invasively). This is a real, documented v1 limitation, not an oversight.

---

## 24. A/B Visual Diff: frozen file reference, not a stored digest (issue #53)

**Decision:** `POST /snapshot` gains an optional `audio: {item: <index>} | {file: <path>}`
field (plus `start`/`end`). At capture time, an `item` reference is resolved to its **active
take's source file path** and frozen into the snapshot's JSON as `audio: {item, file, start,
end}` — not stored as a digest or PNG. `GET /snapshot/diff/visualize?from=&to=&type=` then
reuses the exact same decode+FFT pipeline as `GET /analysis/file/visualize` on both sides
(the `from` snapshot's frozen file, and the `to` side — current or another snapshot) and
diffs the two digests with the same `jsondiff` used by `/snapshot/diff`.

**Why a frozen file path, not a frozen digest/PNG.** The obvious alternative — compute and
store the digest (or even the PNG) at snapshot time — was rejected: it would require
decoding audio on every `POST /snapshot` (expensive, and wasted for the common case of a
snapshot nobody ever diffs), and would need three digests stored per snapshot (one per
visualization type) to support "paired visualizations (waveform/spectrum/loudness)" without
guessing which type will be requested later. A frozen **file path** costs nothing to store
and defers the (cheap, ~ms) decode to diff time, exactly once, for whichever type is asked
for — and it's the literal reuse the issue asked for ("reuse the existing
`/analysis/*/visualize` machinery"), not a parallel storage format.

**Why `item` is re-resolved live for `to=current` but `from` never is.** The `from` side is
explicitly historical — it always decodes the frozen path exactly as captured. The `to` side,
when `current`/omitted and the `from` audio was `item`-based, re-resolves that **same item
index** at diff time (a fresh `GetMediaItemTake_Source` lookup) — so if the item's take was
swapped to a different file since the snapshot, the diff picks that up. A `file`-based audio
target (no `item`) always redecodes the same literal path — meaningful when an agent
deliberately re-renders to the same output path between snapshots.

**Known limitation, stated plainly (not hidden):** `/analysis/*/visualize` — and therefore
this diff — analyses an item's **source file**, not the post-fader/FX/mix signal (same scope
as the rest of the analysis surface, §17). Track-level edits (volume, mute, FX, pan) that
don't touch the underlying source file produce **no diff** here, even though they'd change
what you hear. A true "did my mix edit change the sound" diff would need to render the master
at each snapshot — deliberately out of scope for v1 (turns every snapshot into a render,
which contradicts the layer's job of being cheap and always-on); revisit if this becomes a
real pain point, same pattern as other "revisit if it becomes common" deferrals in this doc.

**Index-based addressing accepts the same limitation as `/snapshot/diff`'s track diffs.** If
items are reordered/deleted between snapshot and diff, "item N" may no longer mean the same
physical item at `to=current` resolution time — an existing, already-accepted class of
limitation (track diff paths are also positional), not a new one introduced here.

---

## 25. Server-Side Semantic Search: a narrow, opt-in carve-out of §11 (issue #10)

**Decision:** `GET /catalog/search?semantic=true` ranks by embedding similarity via a local
Ollama instance, instead of (or as well as) the existing FTS5 keyword path. **Opt-in twice
over** — `config.semantic_search.enabled` must be `true` *and* the request must pass
`semantic=true` — and **loopback-only**: `ollama_url` is checked against an allowlist
(`127.0.0.1`/`localhost`/`::1`) before every call, rejecting instantly (no network attempt,
no timeout wait) if it isn't. Off by default, so a fresh install makes zero outbound calls.

**Why this doesn't contradict §11 ("No LLM Client in the Extension").** §11's rationale is
specifically about *generative* redundancy — "the agent calling ReaClaw is already an LLM,
calling another LLM on its behalf is architectural redundancy." An embedding model is a
different kind of tool entirely: it doesn't generate text, it produces a vector for *ranking
ReaClaw's own catalog* — a retrieval primitive the calling agent has no way to substitute for
locally without doing the exact same kind of call itself. This project already ships that
exact call — the MCP client (`mcp/reaclaw_mcp/client.py`, `_semantic_search`) has embedded
and ranked the action catalog via local Ollama since before this issue. This just makes the
same capability available server-side, for plain REST/MCP-less callers, reusing the MCP
client's approach (embed, L2-normalize, cosine-rank, cache) as the reference implementation.

**Why cache the embeddings as comma-separated text, not a BLOB.** `DB` (`src/db/db.h`) only
binds text params — adding blob-binding for one caller was more invasive than the storage
cost of plain-text floats (~77 MB for the full ~6700-action catalog at 768 dimensions,
trivial for a local SQLite file). Cached in a new `action_embeddings` table, keyed by
`(action id, section, model)`, invalidated automatically (rebuilt on next use) when the
catalog size or model changes — piggybacking on the same signature-comparison pattern
`reaper/catalog.cpp` already uses for its own rebuild-marker.

**Why build lazily, not at catalog-build time.** Semantic search is opt-in and most
installs will never enable it; embedding the full catalog on every REAPER startup (or every
catalog rebuild) would pay that cost for users who never call `semantic=true`. Built once,
on first semantic search, and reused until the catalog or model changes.

**Result flags (`interactive`, `mutates_state`, `requires_selection`) are name/category
heuristics, not guarantees** — `interactive` already existed (ellipsis/prompt/known-modal-id);
`mutates_state` defaults `true` except for REAPER's own `View:`/`Options:` prefixes (the
safer assumption when an agent is deciding whether an edit needs care); `requires_selection`
keys off REAPER's own "selected" naming convention. Consistent with this project's existing
"honest heuristic, not ML classifier" pattern (§17's confidence-tagging, catalog.cpp's modal
detection) — cheap, explainable, wrong at the margins, and documented as such.

---

## 26. External-Change Event Feed: `IReaperControlSurface`, in-memory ring, best-effort attribution (issue #31)

**Decision:** `GET /events?since=<cursor>` (poll) and `GET /events/stream` (Server-Sent
Events) expose a granular, attributed push feed built on an `IReaperControlSurface`
implementation registered via `plugin_register("csurf_inst", ...)` — REAPER calls its
virtuals inline, on the main thread, whenever project state changes **from any source**.
Complements the already-shipped `GET /state/changes` (cheap poll token) and `GET
/snapshot/diff` (fallback, works regardless of session boundaries): this is the no-poll,
attributed, granular path the issue asked for.

**Threading — no new synchronization primitive needed.** Control-surface callbacks fire on
REAPER's main thread, the same thread `Executor::tick()` already runs on — confirmed by the
existing async-action comment in `execute.cpp` about WM_TIMER/SetTimer compatibility with
REAPER's own modal loops. Events are appended to a bounded, mutex-guarded `std::deque`
(`src/reaper/csurf.cpp`, `kMaxEvents = 1000`) — the exact same mutex+queue shape `Executor`
already uses for its command queue, just written by the main thread and read by HTTP threads
instead of the reverse. In-memory only, not persisted — an event feed for the current
session, not a durable audit log; the ring and the `GetProjectStateChangeCount` counter both
reset on REAPER restart (consistent, not a new limitation).

**Attribution: `Executor::EditingGuard`, an RAII depth counter — best-effort, not guaranteed.**
A plain (non-atomic — main-thread-only) depth counter is incremented for the duration of
main-thread code running on ReaClaw's behalf: `tick()` wraps every queued command's
`execute()` with one, and `execute.cpp`'s async-action path (which fires via SWELL `SetTimer`,
bypassing the command queue entirely — see §"WM_TIMER" note there) wraps its own
`Main_OnCommand` call with a second, so both direct mutations *and* action dispatch (sync or
async) get tagged `source: "reaclaw"`; a control-surface callback firing synchronously inside
either call stack reads `Executor::is_reaclaw_editing()` to make the call.

**Confirmed live, and confirmed imperfect — documented honestly rather than oversold.**
Testing on the Pi rig found REAPER does **not** guarantee every control-surface notification
fires synchronously within the triggering SDK call — some batched/multi-property updates
showed a notification for ReaClaw's own edit landing *after* the guard had already been
released (observed with an isolated single-property `muted` toggle misattributed
`"external"`; the same property was correctly tagged `"reaclaw"` in a batched multi-property
update in a separate test — the deferral appears to depend on REAPER's own internal batching,
not the event kind). This is a REAPER SDK behavior, not a bug in the guard's scope. Net:
attribution is accurate for the large majority of events and always accurate for
`GET /state/changes`-style "something changed" detection; a caller that needs airtight
certainty for a specific edit should still corroborate with `GET /snapshot/diff` rather than
trust a single event's `source` in isolation. Revisit only if this proves to matter in
practice — matches this repo's existing "revisit if it becomes common" pattern (§17, §23).

**Scope boundary: core state-change virtuals only, not `Extended()`.** Implemented:
`SetTrackListChange`, `SetSurfaceVolume/Pan/Mute/Selected/Solo/RecArm`, `SetPlayState`,
`SetRepeatState`, `SetTrackTitle` — the exact set the issue named explicitly. Not
implemented: the much longer tail reachable through `Extended()` (`CSURF_EXT_SETFXCHANGE`,
`CSURF_EXT_SETFXPARAM`, `CSURF_EXT_SETPROJECTMARKERCHANGE`, etc.) — a deliberate v1
boundary, not an oversight. FX/marker changes still show up via the existing poll token and
snapshot diff; only the granular, attributed push path is narrower for now.

**SSE confirmed working over the existing `SSLServer` + thread pool.** This was flagged as an
open risk going in (untested combination of cpp-httplib's chunked-content-provider, TLS, and
the thread pool) — verified live: a long-lived `curl -N` connection streamed real-time `data:
{...}\n\n` frames correctly, including events generated by a concurrent API call mid-stream.
Shipped as a fully supported path, not experimental. Each connection is bounded to 10 minutes
(`kMaxStreamDuration` in `events.cpp`) so one client can't hold an httplib worker thread
forever; a client should reconnect with `since` set to the last seq it saw. No per-connection
limit beyond httplib's own pool size — single-user tool (§13), same stance as everywhere else.

---

## 27. Live Media Streaming: ReaClaw serves video/audio-out itself; audio-in rides REAPER's own ReaStream

**Decision:** `GET /stream/video` and `GET /stream/audio` (`src/handlers/stream_video.cpp`,
`src/handlers/stream_audio.cpp`) turn the existing ad-hoc realtime path (§19: "PulseAudio null
sink + `x11grab`... only for producing a video / for live human observation") into a
first-class, documented API surface — open the URL directly in a browser tab or a phone's
stock player, no extra software. `docs/NETWORK_AUDIO_NOTES.md` (2026-07-02) explored this and
parked it as wishlist; this supersedes that parking for the video/audio-out half. Audio-**in**
(`POST /state/tracks/{index}/reastream`) takes the opposite shape deliberately: rather than
ReaClaw owning a second network wire protocol, it drives REAPER's own bundled **ReaStream**
JSFX plugin (UDP audio+MIDI streaming) via the existing FX-parameter-mutation machinery
(`handlers/fx.cpp`) — "start streaming audio in" is just another mutation call, keeping the
two-family API pattern (§16) intact instead of ReaClaw becoming a UDP relay.

**One ffmpeg process per HTTP connection, not a shared capture fanned out to N viewers.**
Matches the single-user posture (§13) and avoids building a broadcaster/fan-out component this
codebase has no precedent for; `x11grab`/PulseAudio monitor sources tolerate multiple
independent readers fine if that's ever needed. Opening the stream URL *is* starting the
capture; disconnecting stops it. Each connection is bounded to
`streaming.max_duration_minutes` (default 10, config-driven rather than the hardcoded constant
`events.cpp` uses for its SSE bound — same shape, promoted to config here since a stream is a
heavier resource than an SSE poll loop), same reconnect-by-reopening model as `/events/stream`.

**Subprocess lifecycle: a small owned wrapper (`util/subprocess.h`), not the existing
block-until-exit `run()` helper.** `screenshot.cpp`'s one-shot capture blocks until ffmpeg
exits; a stream's ffmpeg lives for minutes and must be torn down deterministically — SIGTERM,
brief grace period, SIGKILL, reap — on client disconnect, the wall-clock bound, or extension
shutdown. `src/streaming/registry.h` tracks active streams for `GET /stream/status` and
`POST /stream/{id}/stop`, but deliberately never reaches across threads to touch a
`Subprocess` directly — it only sets a "stop requested" flag the owning handler's read loop
polls once per cycle, the same shape as `events.cpp`'s wall-clock check. `ReaClaw::shutdown()`
calls `Streaming::instance().shutdown_all()` **before** `Server::stop()`, because
`Server::stop()` only joins the accept-loop thread, not each connection's own worker thread —
without this, a REAPER extension unload/reload could leak a running ffmpeg child indefinitely.

**Auth: a narrow, explicit `?token=` carve-out for exactly two routes, not a global auth
change.** `Auth::check()` only reads the `Authorization: Bearer <key>` header — correct for
~80 JSON-API routes, but a browser `<img>`/`<audio>` tag or a phone's stock player opening a
stream URL directly can't set a custom header. Rather than widen `check()` itself (which would
put the API key into server logs/browser history for every route), `Auth::check_stream()` adds
a `?token=` fallback used only by a separate `auth_wrap_stream()` in `router.cpp`, applied only
to `GET /stream/video` and `GET /stream/audio`. Trade-off, accepted deliberately: a shared
stream URL carries the key in the clear in that URL — rotate `auth_key` if one ever leaks
outside the trusted user. `GET /stream/audio/devices`, `GET /stream/status`, and
`POST /stream/{id}/stop` stay on the normal header-only `auth_wrap`, since nothing calls those
as a bare URL.

**ReaStream's exact param layout is an explicit, flagged unknown — not assumed away.** Whether
ReaStream's IP/port/mode are exposed as normal automatable JSFX sliders (settable via
`TrackFX_SetParamNormalized`, same as any other plugin param) or live only in its custom
`@gfx`-drawn UI state (not reachable that way at all) was not verified against a live REAPER
instance while this was built. `handlers/reastream.cpp` best-effort-matches named fields
(`ip`/`port`/`mode`/`channel`/`ident`) against a candidate list of plausible slider names and
reports both the FX's actual `params` and any `unresolved` fields in every response, so a
caller can correct the mapping on first use rather than trust a silent, possibly-wrong write.
`docs/NETWORK_AUDIO_NOTES.md` records the three-step spike (add → dump params → adjust) that
should replace this guess with a verified mapping once run against a live instance.

---

## Summary

| Concern | Decision |
|---|---|
| Extension type | Native C++ REAPER extension |
| Language | C++17 |
| HTTP server | cpp-httplib (header-only) |
| TLS | OpenSSL |
| Database | SQLite amalgamation (bundled) |
| JSON | nlohmann/json (header-only) |
| Build | CMake 3.20+ |
| Threading | Background server + main-thread command queue |
| REAPER API | GetFunc binding via reaper_plugin_functions.h |
| Script validation | Lua syntax check only |
| LLM | None — agent generates, extension registers |
| Auth | none or api_key |
| Rate limiting | None |
| Config | JSON at GetResourcePath()/reaclaw/config.json |
| Plugin name | reaper_reaclaw.{dll,dylib,so} |
| API coverage | Tiered: structured verbs + action-runner + Lua escape hatch |
| Production/render | Offline-first headless render engine (Epic #32); `/render` hides RENDER_FORMAT; long renders are jobs; local-first |
| Dependencies | Tiered (0–3): vendored core required; SWS/external tools optional + feature-detected; network forbidden except one narrow, opt-in, loopback-only exception (§25) |
| Versioning | SemVer — additive = MINOR, breaking = MAJOR; documented+advertised endpoints are stable; no 2.0 for additive growth |
| Magic wand (Epic-adjacent, issue #10) | Three layers: Skill (`skill/reaclaw/`) + MCP server (`mcp/`, 18 tools) + server-side intent verbs/capabilities/recipes/semantic search |
| Live media streaming (§27) | Video/audio-out: ReaClaw serves its own ffmpeg-backed HTTP stream, one process per connection. Audio-in: REAPER's own ReaStream plugin, driven via the FX-parameter API — not a new wire protocol |

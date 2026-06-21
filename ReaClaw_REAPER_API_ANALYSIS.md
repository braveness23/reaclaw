# REAPER API Deep Dive: Gap Analysis vs ReaClaw

## Context

This is a research document, not an implementation plan. The goal is to map the full automation
surface of REAPER (as accessible from within a native C++ extension), compare it against what
ReaClaw exposes today, and identify every gap — so that future phase decisions are made from
complete knowledge rather than guesswork.

Sources: `src/server/router.cpp` (route list), `src/handlers/state.cpp` + others (confirmed API
calls), `src/reaper/api.cpp` (verified function bindings), `src/handlers/capabilities.cpp`
(coverage manifest), REAPER SDK documentation and training knowledge for the unexposed surface.

---

## Part 1 — REAPER's Full Automation Surface (from within an extension)

A native extension has access to every function REAPER exposes via `GetFunc`. The SDK
(`reaper_plugin_functions.h`) contains ~800+ function pointers. Below is the full domain map.

### 1. Transport & Playback

| Function | What it does | Thread-safe |
|---|---|---|
| `GetPlayState()` | Read transport state (playing/paused/recording bitmask) | Yes |
| `GetPlayPosition()` | Playback position (not cursor) | Yes |
| `GetCursorPosition()` | Edit cursor position | Yes |
| `SetEditCurPos2()` | Move edit cursor to position | Main only |
| `GetSet_LoopTimeRange2()` | Get/set loop start+end | Main only |
| `GetSetRepeatEx()` | Get/set loop enabled | Main only |
| `CSurf_OnPlay()` / `CSurf_OnStop()` / `CSurf_OnRecord()` | Transport control via surface API | Main only |
| `OnPlayButton()` / `OnStopButton()` / `OnRecordButton()` | Direct transport control | Main only |
| `GetPlayPosition2()` | Output (latency-compensated) position | Yes |

**ReaClaw coverage:** Reads GetPlayState, GetCursorPosition, loop state.  
**Gaps:** No write transport controls (play/stop/record) as explicit verbs. Currently reachable
only via `POST /execute/action` with action IDs (1007=play, 1016=stop, 1013=record) — functional
but opaque. No cursor-set endpoint. No loop-range write endpoint.

---

### 2. Project & File Management

| Function | What it does |
|---|---|
| `GetProjectPath()` | Current project file path |
| `GetProjectName()` | Filename without directory |
| `IsProjectDirty()` | Whether project has unsaved changes |
| `GetProjectLength()` | Total project length in seconds |
| `Main_openProject()` | Open a project file |
| `GetSetProjectNotes()` | Read/write project-level text notes block |
| `GetSetProjectInfo()` | Get/set project metadata (tempo, sample rate, etc.) |
| `GetSetProjectInfo_String()` | String project metadata (title, author, etc.) |
| `EnumProjects()` | Enumerate open projects (REAPER supports multiple) |
| `SelectProjectInstance()` | Switch active project |
| `Undo_BeginBlock2()` / `Undo_EndBlock2()` | Wrap operations in a named undo block |
| `Undo_CanUndo2()` / `Undo_CanRedo2()` | Query undo/redo availability |
| `Undo_DoUndo2()` / `Undo_DoRedo2()` | Execute undo/redo |
| `GetProjectExtState()` / `SetProjectExtState()` | Per-project key/value storage (extension data) |

**ReaClaw coverage:** Reads project path, name from GetProjectPath. Returns project BPM.  
**Gaps:**
- No `IsProjectDirty` endpoint — agent can't tell if session needs saving
- No `GetProjectLength` — agent doesn't know session duration
- No project-level metadata (title, author, sample rate)
- No `GetSetProjectNotes` — project notes are a useful agent-to-session communication channel
- No undo/redo endpoints — **all REST mutations are permanently unundo-able**; operations don't
  use `Undo_BeginBlock2/EndBlock2`, so the user can't Ctrl+Z a REST-triggered change
- No `GetProjectExtState/SetProjectExtState` — could store agent session context persistently
  in the project file itself (survives close/reopen, unlike SQLite)
- No multi-project awareness — REAPER supports multiple open projects; all ReaClaw calls use
  `nullptr` as project handle (active project only)

---

### 3. Time & Tempo Map

| Function | What it does |
|---|---|
| `GetProjectTimeSignature2()` | Current BPM + time sig at edit cursor |
| `CountTempoTimeSigMarkers()` | Total tempo map entries |
| `GetTempoTimeSigMarker()` | Read one tempo/time-sig entry by index |
| `SetTempoTimeSigMarker()` | Modify an existing tempo/time-sig entry |
| `AddTempoTimeSigMarker()` | Insert a new tempo map point |
| `DeleteTempoTimeSigMarker()` | Remove a tempo map point |
| `TimeMap2_timeToBeats()` / `TimeMap2_beatsToTime()` | Convert seconds ↔ beats |
| `TimeMap_timeToQN()` / `TimeMap_QNToTime()` | Convert seconds ↔ quarter notes |
| `TimeMap_GetDividedBpmAtTime()` | Effective BPM (accounting for time sig) at a position |

**ReaClaw coverage:** Returns the current BPM and time-signature numerator from
`GetProjectTimeSignature2` in `GET /state`. Single value, not the map.  
**Gaps:**
- No tempo map enumeration or write
- No time conversion utilities — agents have to do beat↔seconds math themselves
- No way to read BPM at a specific position (important for tempo-mapped sessions)
- Effectively: the agent sees "120 BPM, 4/4" but can't know that bar 32 switches to 6/8 at 90 BPM

---

### 4. Tracks (Core)

| Function | What it does | Thread-safe |
|---|---|---|
| `CountTracks()` / `GetTrack()` | Count + get track by index | Yes |
| `GetTrackName()` | Read name | Yes |
| `GetSetMediaTrackInfo()` | Get/set nearly every track property | Mixed |
| `GetSetMediaTrackInfo_String()` | Get/set string properties (P_NAME, etc.) | Mixed |
| `GetMediaTrackInfo_Value()` | Read numeric property (threadsafe variant) | Yes |
| `InsertTrackAtIndex()` | Create track | Main only |
| `DeleteTrack()` | Delete track | Main only |
| `SetTrackSelected()` | Select/deselect | Main only |
| `TrackList_AdjustWindows()` | Refresh mixer display | Main only |
| `GetParentTrack()` | Get folder parent (vs I_FOLDERDEPTH which is indirect) | Yes |
| `IsTrackSelected()` | Read selection state directly | Yes |
| `GetTrackColor()` | Read track color (with default vs custom awareness) | Yes |
| `SetTrackColor()` | Write track color | Main only |
| `GetTrackGUID()` | GUID as string (vs via GetSetMediaTrackInfo) | Yes |
| `GetTrackState()` | Returns mute/solo/etc. as bitmask | Yes |

Key `GetSetMediaTrackInfo` property strings (not all exposed by ReaClaw):

| Property | Description | ReaClaw |
|---|---|---|
| `P_NAME` | Track name | ✓ read+write |
| `D_VOL` | Volume linear | ✓ read+write |
| `D_PAN` | Pan -1..1 | ✓ read+write |
| `B_MUTE` | Mute | ✓ read+write |
| `I_SOLO` | Solo (0/1/2) | ✓ read+write |
| `I_RECARM` | Record arm | ✓ read+write |
| `I_FOLDERDEPTH` | Folder depth | ✓ read+write |
| `I_CUSTOMCOLOR` | Color (custom flag) | ✓ read+write |
| `GUID` | Track GUID | ✓ read only |
| `I_SELECTED` | Selection state | read via CountSelected |
| `P_PARTRACK` | Parent track pointer | **missing** |
| `I_NCHAN` | Track channel count | **missing** |
| `B_PHASE` | Phase invert | **missing** |
| `D_DUALPANL` / `D_DUALPANR` | Dual-pan L/R values | **missing** |
| `I_PANMODE` | Pan mode (stereo/dual/etc.) | **missing** |
| `B_HEIGHTOVERRIDE` | Custom track height | **missing** |
| `I_HEIGHTOVERRIDE` | Track height px | **missing** |
| `I_MIDIIN` / `I_MIDIOUT` | MIDI I/O routing | **missing** |
| `I_PERFFLAGS` | Performance flags (mute on mute etc.) | **missing** |
| `IP_TRACKNUMBER` | Track index (1-based) | read only (internal use) |

**ReaClaw coverage:** Strong. Create, delete, name, color, folder depth, vol, pan, mute, solo, arm
all read/write. Selection read+write.  
**Gaps:**
- `P_PARTRACK` — direct parent pointer (useful for folder hierarchy traversal beyond depth number)
- Phase invert (`B_PHASE`)
- Channel count (`I_NCHAN`) — important for multichannel/surround workflows
- Pan mode / dual pan (`I_PANMODE`, `D_DUALPANL`, `D_DUALPANR`)
- MIDI I/O per track (`I_MIDIIN`, `I_MIDIOUT`)
- Track height (`I_HEIGHTOVERRIDE`)

---

### 5. Track FX

| Function | What it does | ReaClaw |
|---|---|---|
| `TrackFX_GetCount()` | Count FX | ✓ |
| `TrackFX_GetFXName()` | FX name | ✓ |
| `TrackFX_GetEnabled()` / `TrackFX_SetEnabled()` | Enable/disable | ✓ |
| `TrackFX_AddByName()` | Add FX by name/ident | ✓ |
| `TrackFX_Delete()` | Remove FX | ✓ |
| `TrackFX_GetNumParams()` | Param count | ✓ |
| `TrackFX_GetParamName()` | Param name | ✓ |
| `TrackFX_GetParamNormalized()` / `TrackFX_SetParamNormalized()` | Param value 0..1 | ✓ |
| `TrackFX_FormatParamValue()` | Formatted display string | ✓ |
| `TrackFX_GetParam()` / `TrackFX_SetParam()` | Raw (non-normalized) param | **missing** |
| `TrackFX_GetParamEx()` | Min/max/step/midval metadata | **missing** |
| `TrackFX_GetParamIdent()` | Stable param ID string | **missing** |
| `TrackFX_GetPreset()` / `TrackFX_SetPreset()` | FX preset name read/write | **missing** |
| `TrackFX_NavigatePresets()` | Step through presets | **missing** |
| `TrackFX_GetPresetIndex()` / `TrackFX_SetPresetByIndex()` | Preset by index | **missing** |
| `TrackFX_CopyToTrack()` / `TrackFX_CopyToTake()` | Duplicate FX | **missing** |
| `TrackFX_GetChainVisible()` | Whether FX chain window is open | **missing** |
| `TrackFX_SetOpen()` | Open/close FX chain or individual plugin window | **missing** |
| `TrackFX_GetOffline()` / `TrackFX_SetOffline()` | Online/offline state | **missing** |
| `TrackFX_GetRecCount()` / `TrackFX_GetRecChainVisible()` | Input FX chain | **missing** |
| `TrackFX_GetPinMappings()` / `TrackFX_SetPinMappings()` | Channel pin routing | **missing** |
| `TrackFX_GetFXGUID()` | FX GUID (stable identity) | **missing** |
| `TrackFX_GetIOSize()` | FX I/O channel counts | **missing** |

**Gaps:**
- **FX Presets**: No read/write of preset names or index. Agents can't load "Bus Comp" preset.
- **Param metadata**: `TrackFX_GetParamEx` gives min/max/step which agents need to translate
  normalized values into musical units (e.g., 0.73 normalized → "200ms attack" in ms).
- **Stable param identity**: `TrackFX_GetParamIdent` returns strings like `PARAM_0001` that
  survive FX updates; normalized index can shift between plugin versions.
- **FX copy**: Can't duplicate a configured FX to another track without Lua.
- **Online/offline**: Can't offline render-heavy FX via REST.
- **Input FX chain**: Monitoring FX (record path) completely invisible.
- **Pin mappings**: Can't configure multichannel FX routing.

---

### 6. Track Routing & Sends

| Function | What it does | ReaClaw |
|---|---|---|
| `GetTrackNumSends()` | Count sends (category: 0=out, 1=recv, -1=hw) | ✓ (0 and 1 only) |
| `GetTrackSendInfo_Value()` | Read send property | ✓ (D_VOL, D_PAN) |
| `GetSetTrackSendInfo()` | Get/set send property (pointer or value) | partial |
| `SetTrackSendInfo_Value()` | Write send property | ✓ (D_VOL, D_PAN) |
| `CreateTrackSend()` | Create send | ✓ |
| `RemoveTrackSend()` | Delete send | ✓ |

Key send properties not yet read/written:

| Property | Description |
|---|---|
| `B_MUTE` | Send mute state |
| `B_PHASE` | Send phase invert |
| `B_MONO` | Mono send |
| `D_PAN` | Send pan — ✓ |
| `D_VOL` | Send volume — ✓ |
| `I_SRCCHAN` | Source start channel |
| `I_DSTCHAN` | Destination start channel |
| `I_MIDIFLAGS` | MIDI send flags |
| `P_SRCTRACK` / `P_DESTTRACK` | Source/dest track pointer |
| `I_SENDMODE` | Send mode (post-fader, pre-fader, pre-FX) |

Hardware sends (category `-1`) — not exposed at all:
- `GetSetTrackSendInfo(track, -1, idx, ...)` — hardware output routing

**Gaps:**
- Send `B_MUTE`, `B_PHASE`, `B_MONO` — can't mute/invert individual sends via REST
- Send mode (pre/post-fader) — critical routing property, not readable or writable
- Source/dest channel mapping (`I_SRCCHAN`, `I_DSTCHAN`) — essential for multichannel
- Hardware output routing completely unexposed
- Receive side is read (via `GetTrackNumSends(track, 1)`) but no per-receive detail in response

---

### 7. Media Items & Takes

This is the largest single gap in ReaClaw. The SDK offers full CRUD for items and takes; ReaClaw
exposes only minimal read.

| Function | What it does | ReaClaw |
|---|---|---|
| `CountMediaItems()` / `GetMediaItem()` | Enumerate | ✓ read |
| `GetMediaItemInfo_Value()` | Read item property | ✓ (position, length) |
| `GetMediaItemTrack()` | Parent track | ✓ read |
| `GetActiveTake()` / `GetTakeName()` | Active take, name | ✓ read |
| `SetMediaItemInfo_Value()` | **Write** item property | **missing** |
| `SetMediaItemInfo_String()` | **Write** item name | **missing** |
| `SetMediaItemSelected()` | Select/deselect item | **missing** |
| `AddMediaItemToTrack()` | Create blank item | **missing** |
| `DeleteTrackMediaItem()` | Delete item | **missing** |
| `SplitMediaItem()` | Split item at position | **missing** |
| `MoveMediaItemToTrack()` | Move item to different track | **missing** |
| `CreateNewMIDIItemInProj()` | Create MIDI item | **missing** |
| `GetTakeInfo_Value()` / `SetTakeInfo_Value()` | Take vol, pan, pitch, offset, rate | **missing** |
| `GetTake()` | Get take by index (not just active) | **missing** |
| `CountTakes()` | Number of takes on item | **missing** |
| `TakeIsMIDI()` | Check if take contains MIDI | **missing** |
| `GetItemProjectContext()` | Which project the item is in | **missing** |
| `GetMediaSourceFileName()` | Original file path of source | **missing** |
| `GetMediaSourceType()` | "WAV", "MP3", "MIDI", etc. | **missing** |
| `GetMediaSourceLength()` | Duration of source file | **missing** |
| `GetMediaSourceSampleRate()` / `GetMediaSourceNumChannels()` | Audio format | **missing** |
| `GetMediaItemTake_PCMSource()` | PCM source pointer for analysis | **missing** |

Key item properties via `SetMediaItemInfo_Value`:

| Property | Description |
|---|---|
| `D_POSITION` | Start position (seconds) |
| `D_LENGTH` | Duration (seconds) |
| `D_SNAPOFFSET` | Snap point offset |
| `D_FADEINLEN` / `D_FADEOUTLEN` | Fade lengths |
| `D_FADEINDIR` / `D_FADEOUTDIR` | Fade curves |
| `B_MUTE` | Item mute |
| `B_LOOPSRC` | Loop source |
| `B_UISEL` | GUI selection state |
| `D_VOL` | Item volume |
| `I_GROUPID` | Item group |
| `I_LASTY` / `I_LASTH` | Display position (read-only) |

Key take properties via `SetTakeInfo_Value`:

| Property | Description |
|---|---|
| `D_VOL` | Take volume |
| `D_PAN` | Take pan |
| `D_PITCH` | Semitone pitch shift |
| `B_PPITCH` | Preserve pitch on rate change |
| `D_PLAYRATE` | Playback rate |
| `D_STARTOFFS` | Source media start offset |
| `I_CHANMODE` | Channel mode (mono, stereo, etc.) |

**Gaps:** The entire item/take write layer is missing. Agents can observe items (where they are,
how long) but cannot create, move, trim, split, duplicate, or delete them without a Lua script.
Audio source metadata (file path, format, duration) is also completely absent from reads.

---

### 8. Take FX

| Function | What it does | ReaClaw |
|---|---|---|
| `TakeFX_GetCount()` | Count take FX | **missing** |
| `TakeFX_GetFXName()` | FX name | **missing** |
| `TakeFX_AddByName()` | Add FX | **missing** |
| `TakeFX_Delete()` | Remove FX | **missing** |
| `TakeFX_GetEnabled()` / `TakeFX_SetEnabled()` | Enable/disable | **missing** |
| `TakeFX_GetNumParams()` | Param count | **missing** |
| `TakeFX_GetParamNormalized()` / `TakeFX_SetParamNormalized()` | Param value | **missing** |
| `TakeFX_GetPreset()` / `TakeFX_SetPreset()` | Preset | **missing** |

**Gaps:** Take-level FX (pitch correction, time stretch, etc.) are completely invisible.

---

### 9. MIDI

| Function | What it does | ReaClaw |
|---|---|---|
| `MIDI_CountEvts()` | Count MIDI events on take | **missing** |
| `MIDI_GetNote()` / `MIDI_SetNote()` | Read/write note by index | **missing** |
| `MIDI_InsertNote()` | Add note | **missing** |
| `MIDI_DeleteNote()` | Remove note | **missing** |
| `MIDI_GetCC()` / `MIDI_SetCC()` | Control change events | **missing** |
| `MIDI_InsertCC()` / `MIDI_DeleteCC()` | Add/remove CC | **missing** |
| `MIDI_GetProjTimeFromPPQPos()` | Convert PPQ → seconds | **missing** |
| `MIDI_GetPPQPosFromProjTime()` | Convert seconds → PPQ | **missing** |
| `MIDI_GetScale()` | Read scale/root on take | **missing** |
| `MIDI_SetItemExtents()` | Set MIDI item length | **missing** |
| `MIDI_Sort()` | Sort events (needed after inserts) | **missing** |
| `MIDIEditor_GetActive()` | Get open MIDI editor | **missing** |
| `MIDIEditor_GetMode()` | Piano roll vs event list mode | **missing** |
| `MIDIEditor_GetTake()` | Take being edited | **missing** |
| `MIDIEditor_OnCommand()` | Execute MIDI editor action | **missing** |

**Gaps:** MIDI is entirely unreachable as structured verbs. An agent that wants to write a melody
into a MIDI item must generate Lua to do it. Not wrong (that's the design intent per
`GET /capabilities`), but the Lua path requires knowing the take pointer and PPQ math, which
agents find difficult without time conversion utilities.

---

### 10. Envelope / Automation

| Function | What it does | ReaClaw |
|---|---|---|
| `CountTrackEnvelopes()` / `GetTrackEnvelope()` | Enumerate | ✓ |
| `GetEnvelopeName()` | Envelope name | ✓ |
| `CountEnvelopePoints()` / `GetEnvelopePoint()` | Read points | ✓ (max 500) |
| `InsertEnvelopePoint()` | **Add** point | **missing** |
| `SetEnvelopePoint()` / `SetEnvelopePointEx()` | **Modify** point | **missing** |
| `DeleteEnvelopePoint()` | **Delete** point | **missing** |
| `Envelope_Evaluate()` | Query envelope value at time | **missing** |
| `GetSetEnvelopeInfo_Value()` | Envelope mode (trim/read/touch/latch/write) | **missing** |
| `GetSetEnvelopeState2()` | Bulk chunk read/write | **missing** |
| `GetEnvelopeScalingMode()` | How values scale | **missing** |
| `GetTrackEnvelopeByName()` | Find envelope by name | **missing** |

**Gaps:** Automation is read-only. Agents can see automation curves but cannot write them.
Envelope mode (arm/read/touch/latch/write) is not readable via REST.

---

### 11. Markers & Regions

Markers and regions are REAPER's primary session-organization primitives. ReaClaw has zero coverage.

| Function | What it does |
|---|---|
| `EnumProjectMarkers3()` | Enumerate all markers and regions |
| `AddProjectMarker2()` | Create marker or region |
| `SetProjectMarker4()` | Modify marker/region (name, position, color) |
| `DeleteProjectMarker()` | Delete by index |
| `GetLastMarkerAndCurRegion()` | Marker/region at current cursor |
| `CountProjectMarkers()` | Count markers and regions |

**Gaps:** No `/state/markers` endpoint exists. Agents can't read the session's chapter structure,
can't navigate to markers via REST (must use action IDs for "Go to marker N"), can't create or
label regions for rendering. Markers are how producers organize sessions (verse, chorus, bridge,
etc.) — their absence means agents have no concept of song structure.

---

### 12. Selection

| Function | What it does | ReaClaw |
|---|---|---|
| `CountSelectedTracks()` / `GetSelectedTrack()` | Selected tracks | ✓ read |
| `SetTrackSelected()` | Select/deselect track | ✓ write |
| `CountSelectedMediaItems()` / `GetSelectedMediaItem()` | Selected items | ✓ read |
| `SetMediaItemSelected()` | Select/deselect item | **missing** |
| `SelectAllMediaItems()` | Select all items on track | **missing** |
| `GetCursorContext()` | Which context has focus (arrange/MIDI/etc.) | **missing** |

**Gaps:** Item selection write is missing. No way to select specific items by position range or
track scope via REST.

---

### 13. Actions & Commands (All Sections)

| Function | What it does | ReaClaw |
|---|---|---|
| `Main_OnCommand()` | Execute main-section action | ✓ |
| `Main_OnCommandEx()` | Execute with project context | **missing** |
| `kbd_enumerateActions()` | Enumerate bound actions | ✓ (main only) |
| `kbd_getTextFromCmd()` | Action name from ID | ✓ (main only) |
| `SectionFromUniqueID()` | Get section (0=main, 100=MIDI editor, etc.) | partial |
| `MIDIEditor_OnCommand()` | MIDI editor action | **missing** |
| `NamedCommandLookup()` | ID from name string | ✓ |

REAPER has multiple action sections. ReaClaw only uses section 0 (Main):
- Section 0: Main (exposed)
- Section 100: MIDI Editor (~400 actions — **not indexed, not executable**)
- Section 200: Media Explorer (**not indexed, not executable**)
- Section 32060: MIDI Event List Editor (**not indexed**)

**Gaps:**
- MIDI Editor actions not catalogued or executable
- `Main_OnCommandEx` takes a project context — important when multiple projects are open
- No way to invoke section-specific actions programmatically

---

### 14. Audio Analysis & Metering

| Function / Approach | Notes | ReaClaw |
|---|---|---|
| `Track_GetPeakInfo()` | Peak level for track audio output (real-time) | **missing** |
| `Track_GetPeakHoldDB()` | Peak hold value | **missing** |
| `GetMediaItemTake_PCMSource()` + PCM_Source_* | Access raw PCM for offline analysis | **missing** |
| `Render` actions + temp file | Bounce to temp file, then read with DSP library | **missing** |
| Plugin-to-extension messaging | Query analyzer plugin state via VST chunk | **missing** |

**Note:** REAPER has no built-in loudness/spectral analysis SDK call. Audio analysis requires one
of: a DSP library embedded in the extension (e.g., KissFFT for spectrum), reading from a metering
plugin via VST chunk, or offline render to a temp file followed by analysis.

**Gaps:** No metering data whatsoever. This is the Q1 item from `ReaClaw_IDEAS.md`. The design
decision (basic built-in / advanced optional) is already settled — this just hasn't been built.
`Track_GetPeakInfo` gives real-time peak/RMS with zero DSP overhead; it's threadsafe and would
be straightforward to add to `GET /state/tracks`.

---

### 15. Undo/Redo

| Function | What it does | ReaClaw |
|---|---|---|
| `Undo_BeginBlock2()` | Start named undo block | **not used** |
| `Undo_EndBlock2()` | Close undo block + name it | **not used** |
| `Undo_CanUndo2()` | Check if undo is available | **missing** |
| `Undo_CanRedo2()` | Check if redo is available | **missing** |
| `Undo_DoUndo2()` | Execute undo | **missing** |
| `Undo_DoRedo2()` | Execute redo | **missing** |

**Gaps:** Every mutation ReaClaw makes (via `GetSetMediaTrackInfo`, `InsertTrackAtIndex`,
`TrackFX_AddByName`, `CreateTrackSend`, etc.) happens *outside* an undo block. The user cannot
Ctrl+Z a change made via the REST API. This is the single most significant architectural gap
for production use. Wrapping all mutation handlers in `Undo_BeginBlock2 / Undo_EndBlock2` would
fix this.

---

### 16. Extension Hooks & Inter-Extension Communication

| Hook | What it does | ReaClaw |
|---|---|---|
| `plugin_register("timer", ...)` | ~30fps main-thread callback | ✓ |
| `plugin_register("hookcommand2", ...)` | Intercept action execution | ✓ (used for menu items) |
| `plugin_register("hookcustommenu", ...)` | Inject into menus | ✓ (Extensions menu) |
| `plugin_register("toggleaction", ...)` | Toggle state for custom actions | ✓ (server start/stop) |
| `plugin_register("projectconfig", ...)` | Per-project persistent config | **not used** |
| `plugin_register("csurf_inst", ...)` | Control surface plugin | **not used** |
| `GetExtState()` / `SetExtState()` | Global cross-extension key/value store | **not used** |

**Note:** `GetExtState/SetExtState` is a lightweight in-memory key/value store shared across all
extensions. An agent could use it to signal state to other extensions (e.g., SWS, Reaper-Keys).

---

## Part 2 — What ReaClaw Currently Covers (Confirmed from Code)

**Verified routes** (from `src/server/router.cpp`):

```
GET  /health
GET  /capabilities
GET  /catalog
GET  /catalog/search
GET  /catalog/categories
GET  /catalog/{id}
GET  /state                       ← transport, BPM, cursor, loop, track count
GET  /state/tracks                ← all tracks with FX, sends, colors, folder depth
GET  /state/items                 ← all items: position, length, track, take name (read only)
GET  /state/selection             ← selected tracks + items
GET  /state/automation            ← envelopes on selected track (read only)
POST /state/tracks                ← create and/or batch update tracks
POST /state/tracks/{index}        ← update single track
DELETE /state/tracks/{index}
POST /state/tracks/{index}/fx     ← add FX by name
GET  /state/tracks/{index}/fx/{slot}
POST /state/tracks/{index}/fx/{slot}
DELETE /state/tracks/{index}/fx/{slot}
POST /state/tracks/{index}/sends
DELETE /state/tracks/{index}/sends/{send}
POST /state/selection
POST /execute/action
POST /execute/sequence
POST /scripts/register
GET  /scripts/cache
GET  /scripts/{id}
DELETE /scripts/{id}
GET  /history
```

**Verified REAPER API calls used** (from source code):
- Transport reads: `GetPlayState`, `GetCursorPosition`, `GetProjectTimeSignature2`,
  `GetSet_LoopTimeRange2`, `GetSetRepeatEx`
- Track reads: `CountTracks`, `GetTrack`, `GetTrackName`, `GetSetMediaTrackInfo`,
  `GetSetMediaTrackInfo_String`, `GetMediaTrackInfo_Value`
- Track writes: `InsertTrackAtIndex`, `DeleteTrack`, `SetTrackSelected`,
  `TrackList_AdjustWindows`
- FX: Full read+write suite (`TrackFX_GetCount`, `TrackFX_AddByName`, `TrackFX_Delete`,
  `TrackFX_GetParamNormalized`, `TrackFX_SetParamNormalized`, `TrackFX_FormatParamValue`, etc.)
- Routing: `CreateTrackSend`, `RemoveTrackSend`, `GetTrackSendInfo_Value`,
  `SetTrackSendInfo_Value`, `GetSetTrackSendInfo`
- Items (read only): `CountMediaItems`, `GetMediaItem`, `GetMediaItemInfo_Value`,
  `GetMediaItemTrack`, `GetActiveTake`, `GetTakeName`
- Selection: `CountSelectedTracks`, `GetSelectedTrack`, `CountSelectedMediaItems`,
  `GetSelectedMediaItem`
- Envelopes (read only): `CountTrackEnvelopes`, `GetTrackEnvelope`, `GetEnvelopeName`,
  `CountEnvelopePoints`, `GetEnvelopePoint`
- Catalog: `kbd_enumerateActions`, `kbd_getTextFromCmd`, `SectionFromUniqueID`,
  `NamedCommandLookup`
- Scripts: `AddRemoveReaScript`
- Utility: `GetResourcePath`, `GetAppVersion`, `ShowConsoleMsg`, `GetProjectPath`,
  `Main_OnCommand`

---

## Part 3 — Gap Summary: Missing or Underexploited

Organized by impact and effort:

### Tier A — High impact, straightforward (SDK support confirmed, thin layer needed)

| Gap | SDK Function(s) | Notes |
|---|---|---|
| **Undo grouping** | `Undo_BeginBlock2/EndBlock2` | Wrap every mutation handler. Currently all REST mutations are unundo-able. Single biggest quality-of-life gap. |
| **Markers & regions** | `AddProjectMarker2`, `EnumProjectMarkers3`, `SetProjectMarker4`, `DeleteProjectMarker` | Zero coverage. Essential for session structure. `/state/markers` GET+POST+DELETE. |
| **Track peak metering** | `Track_GetPeakInfo()` | Real-time level per track. Threadsafe. Add to `/state/tracks` or dedicated `/state/meters`. First step of Q1 (audio analysis). |
| **Project extras** | `IsProjectDirty`, `GetProjectLength`, `GetSetProjectNotes` | Cheap reads. Dirty flag tells agent when to prompt save; length lets agent reason about time; notes = agent scratch pad in project file. |
| **FX presets** | `TrackFX_GetPreset`, `TrackFX_SetPreset`, `TrackFX_NavigatePresets` | Very common workflow: "load the Slate VBC preset named Bus Comp". Currently impossible via REST. |
| **FX param metadata** | `TrackFX_GetParamEx` | Returns min/max/step so agent can reason in real units, not normalized 0..1. Add to `/state/tracks/{i}/fx/{slot}` response. |
| **Envelope automation write** | `InsertEnvelopePoint`, `SetEnvelopePoint`, `DeleteEnvelopePoint` | Read exists; write layer missing. Essential for mixing automation. |
| **Send extended props** | `B_MUTE`, `B_PHASE`, `B_MONO`, `I_SENDMODE` | Can create and volume/pan sends; can't mute them or change pre/post-fader mode. |
| **Tempo map** | `CountTempoTimeSigMarkers`, `GetTempoTimeSigMarker`, `AddTempoTimeSigMarker` | Currently only current-position BPM. Full map is essential for non-uniform tempo sessions. |
| **Time utilities** | `TimeMap2_timeToBeats`, `TimeMap2_beatsToTime` | Beat↔seconds conversion. Agents need this math constantly. |
| **MIDI section catalog** | `kbd_enumerateActions(section=100)` | ~400 MIDI editor actions not indexed. Add `?section=midi_editor` to `/catalog` and `/catalog/search`. |

### Tier B — High impact, more surface (still well-supported by SDK)

| Gap | SDK Function(s) | Notes |
|---|---|---|
| **Item writes** | `SetMediaItemInfo_Value`, `AddMediaItemToTrack`, `DeleteTrackMediaItem`, `SplitMediaItem`, `MoveMediaItemToTrack` | Currently read-only. Agents can observe items but cannot create, move, or trim them. Full CRUD at `/state/items`. |
| **Audio source metadata** | `GetMediaSourceFileName`, `GetMediaSourceType`, `GetMediaSourceLength`, `GetMediaSourceSampleRate` | Agents can see item position/length but not *what* the source is. Crucial for understanding a session. |
| **Take properties** | `GetTakeInfo_Value`/`SetTakeInfo_Value` (D_VOL, D_PAN, D_PITCH, D_PLAYRATE) | Take-level vol/pan/pitch/rate not exposed. |
| **Track extras** | `B_PHASE`, `I_NCHAN`, `D_DUALPANL/R`, `I_PANMODE`, `I_MIDIIN/OUT` | Phase invert, channel count, dual pan, MIDI routing all missing from track reads/writes. |
| **FX copy** | `TrackFX_CopyToTrack` | Can't duplicate a configured FX chain without Lua. |
| **FX online/offline** | `TrackFX_GetOffline`/`SetOffline` | Can't offline a heavy plugin without Lua. |
| **Item selection write** | `SetMediaItemSelected` | Can select tracks via REST but not items. |
| **Project ext state** | `GetProjectExtState`/`SetProjectExtState` | Per-project persistent agent scratchpad. Survives project close/reopen, unlike SQLite. |
| **Undo query** | `Undo_CanUndo2`, `Undo_DoUndo2` | Once undo blocks exist, expose undo/redo as verbs. |

### Tier C — Specific use cases (useful when needed, lower general frequency)

| Gap | Notes |
|---|---|
| Take FX (`TakeFX_*`) | Item-level FX chains (pitch correction, time stretch on individual takes) |
| MIDI note CRUD | `MIDI_InsertNote`, `MIDI_DeleteNote`, `MIDI_GetNote`, etc. High complexity; Lua already covers well |
| Multi-project support | `EnumProjects` + project-scoped calls; most sessions are single-project |
| Hardware routing | Category -1 sends; advanced studio routing |
| Input FX chain | `TrackFX_GetRecCount`; monitoring/record path FX |
| FX pin mappings | Multichannel routing through individual FX |
| Control surface hook | `csurf_inst` — could mirror REST state changes to physical controllers |
| `GetExtState/SetExtState` | Cross-extension signaling (e.g., tell SWS something changed) |
| `GetCursorContext` | Know whether focus is arrange, MIDI editor, etc. |

### Tier D — Not meaningfully achievable via SDK today

| Item | Reason |
|---|---|
| Real-time waveform/spectral data | No PCM buffer access in SDK; requires render-to-file + external DSP |
| Hardware I/O level metering | `Track_GetPeakInfo` gives mix bus level, not hardware meters |
| GUI window control | SWELL/Win32 available to extensions but not via REST |
| Latency/buffer/device config | REAPER's audio preferences are not SDK-accessible |
| ReaScript console | Not an SDK concept; only available via UI |

---

## Part 4 — The Three Tiers Revisited

`GET /capabilities` correctly identifies items/takes, MIDI, markers, tempo map, envelope writes,
render, and project open/save as "via script or action." The question is whether some of these
should graduate to structured verbs given their frequency and the complexity of doing them in Lua.

**Should graduate to structured verbs (Tier A/B above):**
- Markers & regions — session navigation primitive; used constantly
- Envelope automation write — mixing core; awkward as Lua
- Item position/length writes — fundamental content manipulation

**Fine as Lua/action (the design intent is correct):**
- MIDI note editing — complex domain; Lua API is well-designed for it
- Render/freeze — stateful, long-running, action-based
- Project open/save — action-based is fine (`40025` = save)
- Tempo map edits — infrequent; Lua is adequate

**Missing from the tier manifest entirely (should add to `GET /capabilities`):**
- Markers & regions (currently not mentioned)
- Tempo map (currently not mentioned)
- Undo grouping status (agents should know whether mutations are undo-safe)

---

## Verification

This document is a pure research artifact — no code changes to verify. Findings were cross-checked
against:
1. `src/server/router.cpp` — complete route list
2. `src/handlers/*.cpp` — confirmed REAPER API calls
3. `src/reaper/api.cpp` — `k_required_fns[]` list
4. `src/handlers/capabilities.cpp` — coverage manifest
5. REAPER SDK documentation and `reaper_plugin_functions.h` API surface (training knowledge;
   vendor/reaper-sdk is not committed to this repo — see `vendor/README.md`)

#pragma once

#include <json.hpp>

// REAPER opaque types (full defs come from reaper_plugin_functions.h in the .cpp)
struct MediaTrack;
struct MediaItem;

namespace ReaClaw::Hints {

// Epic #18 (Q3) — consequence-aware hints.
//
// After a mutating edit, surface the *consequence of that specific edit* against
// the current session as a small set of hand-authored invariants. A hint is
// `{code, severity, message}` (severity: "info" | "warn"). These are attached to
// the mutating response under a `"hints"` array — distinct from an independent
// state observation the agent asks for on its own.
//
// All functions are main-thread only (they read the REAPER SDK) and return an
// (possibly empty) JSON array. Callers merge the result into their response.

// Invariants about a track and what an edit to it implies (muted/soloed-away,
// rec-armed with no input, near-silent fader, phase flipped, audio dead-ends).
nlohmann::json for_track(MediaTrack* track, int track_index);

// Track invariants plus FX-specific consequence of the slot just touched
// (offline / bypassed, instrument-less MIDI chain).
nlohmann::json for_fx(MediaTrack* track, int track_index, int fx_slot);

// Track invariants plus the consequence of a send just created/edited to dest
// (routes nowhere, destination muted).
nlohmann::json for_send(MediaTrack* src, int src_index, MediaTrack* dest);

// Track invariants plus item/take consequence (empty item = silent placeholder;
// MIDI take on a track with no virtual instrument = silent).
nlohmann::json for_item(MediaItem* item, int item_index);

}  // namespace ReaClaw::Hints

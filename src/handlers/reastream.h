#pragma once

#include <httplib.h>

namespace ReaClaw::Handlers {

// POST /state/tracks/{index}/reastream — audio/MIDI-in via REAPER's own
// bundled ReaStream plugin (UDP audio+MIDI streaming), rather than ReaClaw
// owning a new wire protocol: "start streaming audio in" becomes just another
// FX-parameter mutation, reusing the same TrackFX_AddByName / param-write
// machinery as POST .../fx (see handlers/fx.cpp, handlers/fx_internal.h).
// Body: { mode?, ip?, port?, ident?, channel?, enabled? } — all optional,
// normalized 0..1 slider writes.
//
// IMPORTANT: ReaStream's exact slider layout (which index/name is IP vs.
// port vs. mode — or whether some of those live only in its custom @gfx UI
// state rather than as automatable sliders at all) has NOT been verified
// against a live REAPER instance; this handler's field→slider mapping is a
// best-effort guess (see reastream.cpp). The response always includes the
// FX's actual `params` and any `unresolved` fields so a caller can correct
// the guess on first use — see docs/NETWORK_AUDIO_NOTES.md for the spike
// that should replace this comment once run.
void handle_reastream_configure(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

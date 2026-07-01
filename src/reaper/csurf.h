#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <json.hpp>

namespace ReaClaw::Csurf {

// Issue #31 — the push event feed. Registers an IReaperControlSurface
// instance ("csurf_inst") so REAPER calls us inline whenever project state
// changes *from any source* — a human at the GUI, a control surface, another
// API client, or ReaClaw's own edits. Complements the already-shipped cheap
// poll token (GET /state/changes) and snapshot-diff fallback (GET
// /snapshot/diff): this is the granular, attributed, no-poll-needed path.
//
// Events land in a bounded in-memory ring (main-thread writes only — the
// control-surface callbacks all fire on REAPER's main thread; HTTP threads
// only ever read, under the same lock). Deliberately not persisted — an
// event feed for the current REAPER session, not a durable audit log.

void init();      // register the control surface at extension load
void shutdown();  // unregister at extension unload

// One captured event. `source` is "reaclaw" (fired while
// Executor::is_reaclaw_editing() was true) or "external" (everything else).
struct Event {
    int64_t seq = 0;
    std::string ts;
    std::string kind;
    std::string track_guid;  // empty if not track-scoped
    int track_index = -1;    // -1 if not track-scoped or not resolvable
    nlohmann::json value;
    std::string source;
};

// Thread-safe: callable from any thread. Returns events with seq > `since`,
// oldest first, capped at `limit`. `since=0` returns from the oldest
// currently-retained event (the ring is bounded — see kMaxEvents in the .cpp).
std::vector<Event> events_since(int64_t since, int limit);

// The most recent seq issued (0 if no events yet) — the cursor a caller
// should pass as `since` on its *next* poll to avoid re-reading this batch.
int64_t head_cursor();

}  // namespace ReaClaw::Csurf

#include "reaper/csurf.h"

#include "handlers/common.h"
#include "reaper/executor.h"
#include "util/logging.h"

#include <deque>
#include <mutex>

#include <reaper_plugin_functions.h>

namespace ReaClaw::Csurf {

namespace {

// Bounded ring — an event feed for the current session, not a durable log.
// At REAPER's ~30fps callback ceiling, 1000 events is minutes of headroom
// even under sustained editing; a caller polling every few seconds will
// never see a gap in practice.
constexpr size_t kMaxEvents = 1000;

std::mutex s_mutex;
std::deque<Event> s_events;
int64_t s_next_seq = 1;

std::string track_guid(MediaTrack* track) {
    if (!track || !GetSetMediaTrackInfo)
        return "";
    auto* g = static_cast<GUID*>(GetSetMediaTrackInfo(track, "GUID", nullptr));
    return g ? Handlers::guid_to_string(g) : "";
}

int track_index(MediaTrack* track) {
    if (!track || !GetMediaTrackInfo_Value)
        return -1;
    double idx = GetMediaTrackInfo_Value(track, "IP_TRACKNUMBER");
    // IP_TRACKNUMBER: 0 = master, >0 = 1-based track number, -1 = not found.
    // Report as a 0-based index matching /state/tracks (master excluded
    // there too), so -1 covers "not a normal track" here as well.
    return idx > 0 ? static_cast<int>(idx) - 1 : -1;
}

// Append one event to the ring (main-thread only — every caller is a
// control-surface virtual, which REAPER only ever calls on the main thread).
void push(const std::string& kind, MediaTrack* track, nlohmann::json value) {
    std::lock_guard<std::mutex> lk(s_mutex);
    Event e;
    e.seq = s_next_seq++;
    e.ts = Handlers::now_iso();
    e.kind = kind;
    e.track_guid = track_guid(track);
    e.track_index = track_index(track);
    e.value = std::move(value);
    e.source = Executor::is_reaclaw_editing() ? "reaclaw" : "external";
    s_events.push_back(std::move(e));
    if (s_events.size() > kMaxEvents)
        s_events.pop_front();
}

// The IReaperControlSurface implementation. REAPER calls these virtuals
// inline, synchronously, on the main thread, whenever project state changes
// from any source. Deliberately covers the core state-change surface the
// issue names explicitly (track list/volume/pan/mute/solo/recarm/selected/
// title, play/repeat state) — the many CSURF_EXT_* codes reachable through
// Extended() (FX add/change/param, marker changes, etc.) are a documented,
// deliberate v1 boundary, not an oversight; see ReaClaw_TECH_DECISIONS.md §26.
class ReaClawSurface : public IReaperControlSurface {
   public:
    const char* GetTypeString() override {
        return "REACLAW";
    }
    const char* GetDescString() override {
        return "ReaClaw event feed (issue #31)";
    }
    const char* GetConfigString() override {
        return "";
    }

    void SetTrackListChange() override {
        push("track_list_change", nullptr, nlohmann::json::object());
    }
    void SetSurfaceVolume(MediaTrack* track, double volume) override {
        push("track_volume", track, {{"volume_db", Handlers::vol_to_db(volume)}});
    }
    void SetSurfacePan(MediaTrack* track, double pan) override {
        push("track_pan", track, {{"pan", pan}});
    }
    void SetSurfaceMute(MediaTrack* track, bool mute) override {
        push("track_mute", track, {{"muted", mute}});
    }
    void SetSurfaceSelected(MediaTrack* track, bool selected) override {
        push("track_selected", track, {{"selected", selected}});
    }
    void SetSurfaceSolo(MediaTrack* track, bool solo) override {
        // trackid == master track means "solo state changed somewhere" per
        // the SDK, not that the master itself is soloed — track_index will
        // resolve to -1 for the master, which callers should read as "see
        // GET /state/tracks for who's actually soloed now."
        push("track_solo", track, {{"soloed", solo}});
    }
    void SetSurfaceRecArm(MediaTrack* track, bool recarm) override {
        push("track_recarm", track, {{"armed", recarm}});
    }
    void SetPlayState(bool play, bool pause, bool rec) override {
        push("play_state", nullptr, {{"playing", play}, {"paused", pause}, {"recording", rec}});
    }
    void SetRepeatState(bool rep) override {
        push("repeat_state", nullptr, {{"enabled", rep}});
    }
    void SetTrackTitle(MediaTrack* track, const char* title) override {
        push("track_title", track, {{"name", std::string(title ? title : "")}});
    }
};

ReaClawSurface s_surface;

}  // namespace

void init() {
    if (plugin_register)
        plugin_register("csurf_inst", static_cast<void*>(&s_surface));
}

void shutdown() {
    if (plugin_register)
        plugin_register("-csurf_inst", static_cast<void*>(&s_surface));
}

std::vector<Event> events_since(int64_t since, int limit) {
    std::lock_guard<std::mutex> lk(s_mutex);
    std::vector<Event> out;
    for (const auto& e : s_events) {
        if (e.seq <= since)
            continue;
        out.push_back(e);
        if (static_cast<int>(out.size()) >= limit)
            break;
    }
    return out;
}

int64_t head_cursor() {
    std::lock_guard<std::mutex> lk(s_mutex);
    return s_events.empty() ? 0 : s_events.back().seq;
}

}  // namespace ReaClaw::Csurf

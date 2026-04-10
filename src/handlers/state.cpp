#include "handlers/state.h"

#include "app.h"
#include "handlers/common.h"
#include "reaper/executor.h"
#include "util/logging.h"

#include <httplib.h>

#include <json.hpp>

// REAPER SDK — extern declarations (REAPERAPI_IMPLEMENT only in reaper/api.cpp)
#include <chrono>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>

#include <reaper_plugin_functions.h>

namespace ReaClaw::Handlers {

namespace {

// ---------------------------------------------------------------------------
// 1-second TTL cache for read-heavy state endpoints.
// Reduces REAPER API call frequency when agents poll rapidly.
// ---------------------------------------------------------------------------

struct StateCacheEntry {
    nlohmann::json data;
    std::chrono::steady_clock::time_point expires;
};

std::unordered_map<std::string, StateCacheEntry> s_state_cache;
std::mutex s_cache_mutex;
constexpr int kStateCacheTTL_ms = 1000;

// Returns cached data if fresh, or nlohmann::json() (null) on miss/expiry.
nlohmann::json state_cache_get(const std::string& key) {
    std::lock_guard<std::mutex> lk(s_cache_mutex);
    auto it = s_state_cache.find(key);
    if (it == s_state_cache.end())
        return nlohmann::json();
    if (std::chrono::steady_clock::now() > it->second.expires)
        return nlohmann::json();
    return it->second.data;
}

void state_cache_set(const std::string& key, const nlohmann::json& data) {
    std::lock_guard<std::mutex> lk(s_cache_mutex);
    s_state_cache[key] = {
            data, std::chrono::steady_clock::now() + std::chrono::milliseconds(kStateCacheTTL_ms)};
}

nlohmann::json track_to_json(MediaTrack* track, int index) {
    if (!track)
        return nlohmann::json::object();

    char name[256] = {};
    GetTrackName(track, name, sizeof(name));

    double vol = 1.0, pan = 0.0;
    if (auto* v = static_cast<double*>(GetSetMediaTrackInfo(track, "D_VOL", nullptr)))
        vol = *v;
    if (auto* p = static_cast<double*>(GetSetMediaTrackInfo(track, "D_PAN", nullptr)))
        pan = *p;

    bool muted = false;
    bool soloed = false;
    bool armed = false;
    if (auto* m = static_cast<bool*>(GetSetMediaTrackInfo(track, "B_MUTE", nullptr)))
        muted = *m;
    if (auto* s = static_cast<int*>(GetSetMediaTrackInfo(track, "I_SOLO", nullptr)))
        soloed = *s != 0;
    if (auto* a = static_cast<int*>(GetSetMediaTrackInfo(track, "I_RECARM", nullptr)))
        armed = *a != 0;

    nlohmann::json fx_arr = nlohmann::json::array();
    int fx_count = TrackFX_GetCount(track);
    for (int fx = 0; fx < fx_count; fx++) {
        char fx_name[256] = {};
        TrackFX_GetFXName(track, fx, fx_name, sizeof(fx_name));
        bool enabled = TrackFX_GetEnabled(track, fx);
        fx_arr.push_back({{"slot", fx}, {"name", fx_name}, {"enabled", enabled}});
    }

    std::string guid_str;
    if (auto* g = static_cast<GUID*>(GetSetMediaTrackInfo(track, "GUID", nullptr))) {
        guid_str = guid_to_string(g);
    }

    int send_count = GetTrackNumSends(track, 1);

    return {{"index", index},
            {"guid", guid_str},
            {"name", name[0] ? name : ""},
            {"muted", muted},
            {"soloed", soloed},
            {"armed", armed},
            {"volume_db", vol_to_db(vol)},
            {"pan", pan},
            {"fx", fx_arr},
            {"send_count", send_count}};
}

nlohmann::json read_project_state() {
    double bpm = 120.0, ts_num = 4.0;
    GetProjectTimeSignature2(nullptr, &bpm, &ts_num);

    double cursor = GetCursorPosition();
    int play_raw = GetPlayState();
    bool playing = (play_raw & 1) != 0;
    bool paused = (play_raw & 2) != 0;
    bool recording = (play_raw & 4) != 0;
    bool loop_enabled = GetSetRepeatEx(nullptr, -1) != 0;

    double loop_start = 0.0, loop_end = 0.0;
    GetSet_LoopTimeRange2(nullptr, false, false, &loop_start, &loop_end, false);

    char proj_path[4096] = {};
    GetProjectPath(proj_path, sizeof(proj_path));

    std::string path_str(proj_path);
    std::string proj_name;
    auto sep = path_str.rfind('/');
#ifdef _WIN32
    {
        auto sep2 = path_str.rfind('\\');
        if (sep2 != std::string::npos && (sep == std::string::npos || sep2 > sep))
            sep = sep2;
    }
#endif
    if (sep != std::string::npos) {
        proj_name = path_str.substr(sep + 1);
        if (proj_name.size() > 4 && proj_name.substr(proj_name.size() - 4) == ".rpp")
            proj_name = proj_name.substr(0, proj_name.size() - 4);
    } else {
        proj_name = path_str;
    }

    int ts_n = static_cast<int>(ts_num);
    return {{"project",
             {{"name", proj_name},
              {"path", path_str},
              {"bpm", bpm},
              {"time_signature", std::to_string(ts_n) + "/4"},
              {"cursor_position", cursor}}},
            {"transport",
             {{"playing", playing},
              {"recording", recording},
              {"paused", paused},
              {"loop_enabled", loop_enabled},
              {"loop_start", loop_start},
              {"loop_end", loop_end}}},
            {"track_count", CountTracks(nullptr)}};
}

}  // namespace

// GET /state
void handle_state(const httplib::Request& req, httplib::Response& res) {
    (void)req;
    auto cached = state_cache_get("state");
    if (!cached.is_null()) {
        json_ok(res, cached);
        return;
    }
    auto data = read_project_state();
    state_cache_set("state", data);
    json_ok(res, data);
}

// GET /state/tracks
void handle_state_tracks(const httplib::Request& req, httplib::Response& res) {
    (void)req;
    auto cached = state_cache_get("tracks");
    if (!cached.is_null()) {
        json_ok(res, cached);
        return;
    }
    int n = CountTracks(nullptr);
    nlohmann::json tracks = nlohmann::json::array();
    for (int i = 0; i < n; i++) {
        tracks.push_back(track_to_json(GetTrack(nullptr, i), i));
    }
    nlohmann::json data = {{"tracks", tracks}};
    state_cache_set("tracks", data);
    json_ok(res, data);
}

// POST /state/tracks/:index
void handle_state_set_track(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    try {
        index = std::stoi(req.path_params.at("index"));
    } catch (...) {
        json_error(res, 400, "Track index must be a numeric integer", "BAD_REQUEST");
        return;
    }

    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (...) {
        json_error(res, 400, "Invalid JSON body", "BAD_REQUEST");
        return;
    }

    // All writes go through the command queue (main-thread safety)
    auto result = Executor::post([index, body]() -> nlohmann::json {
        int n = CountTracks(nullptr);
        if (index < 0 || index >= n)
            return {{"_not_found", true}};
        MediaTrack* track = GetTrack(nullptr, index);
        if (!track)
            return {{"_not_found", true}};

        if (body.contains("muted") && body["muted"].is_boolean()) {
            bool m = body["muted"].get<bool>();
            GetSetMediaTrackInfo(track, "B_MUTE", &m);
        }
        if (body.contains("soloed") && body["soloed"].is_boolean()) {
            int s = body["soloed"].get<bool>() ? 1 : 0;
            GetSetMediaTrackInfo(track, "I_SOLO", &s);
        }
        if (body.contains("armed") && body["armed"].is_boolean()) {
            int a = body["armed"].get<bool>() ? 1 : 0;
            GetSetMediaTrackInfo(track, "I_RECARM", &a);
        }
        if (body.contains("volume_db") && body["volume_db"].is_number()) {
            double v = db_to_vol(body["volume_db"].get<double>());
            GetSetMediaTrackInfo(track, "D_VOL", &v);
        }
        if (body.contains("pan") && body["pan"].is_number()) {
            double p = body["pan"].get<double>();
            p = std::max(-1.0, std::min(1.0, p));
            GetSetMediaTrackInfo(track, "D_PAN", &p);
        }

        return track_to_json(track, index);
    });

    if (result.contains("_timeout")) {
        json_error(res, 408, "Main thread timeout", "TIMEOUT");
        return;
    }
    if (result.contains("_error")) {
        json_error(res, 500, result["_error"].get<std::string>(), "INTERNAL_ERROR");
        return;
    }
    if (result.contains("_not_found")) {
        json_error(res, 404, "Track index out of range", "NOT_FOUND");
        return;
    }

    // Invalidate track cache so the next read reflects the write immediately.
    {
        std::lock_guard<std::mutex> lk(s_cache_mutex);
        s_state_cache.erase("tracks");
        s_state_cache.erase("state");
    }

    Log::info("Track " + std::to_string(index) + " updated");
    json_ok(res, result);
}

// GET /state/items
void handle_state_items(const httplib::Request& req, httplib::Response& res) {
    (void)req;
    auto cached = state_cache_get("items");
    if (!cached.is_null()) {
        json_ok(res, cached);
        return;
    }

    int n = CountMediaItems(nullptr);
    nlohmann::json items = nlohmann::json::array();

    for (int i = 0; i < n; i++) {
        MediaItem* item = GetMediaItem(nullptr, i);
        if (!item)
            continue;

        double pos = GetMediaItemInfo_Value(item, "D_POSITION");
        double length = GetMediaItemInfo_Value(item, "D_LENGTH");

        MediaTrack* track = GetMediaItemTrack(item);
        int track_idx = -1;
        if (track) {
            track_idx = static_cast<int>(GetMediaTrackInfo_Value(track, "IP_TRACKNUMBER")) - 1;
        }

        std::string take_name;
        MediaItem_Take* take = GetActiveTake(item);
        if (take) {
            if (const char* tn = GetTakeName(take))
                take_name = tn;
        }

        items.push_back({{"index", i},
                         {"position", pos},
                         {"length", length},
                         {"track_index", track_idx},
                         {"take_name", take_name}});
    }

    nlohmann::json data = {{"items", items}, {"total", n}};
    state_cache_set("items", data);
    json_ok(res, data);
}

// GET /state/selection
void handle_state_selection(const httplib::Request& req, httplib::Response& res) {
    (void)req;

    int n_tracks = CountSelectedTracks(nullptr);
    nlohmann::json sel_tracks = nlohmann::json::array();
    for (int i = 0; i < n_tracks; i++) {
        MediaTrack* t = GetSelectedTrack(nullptr, i);
        if (!t)
            continue;
        int idx = static_cast<int>(GetMediaTrackInfo_Value(t, "IP_TRACKNUMBER")) - 1;
        char name[256] = {};
        GetTrackName(t, name, sizeof(name));
        sel_tracks.push_back({{"index", idx}, {"name", name}});
    }

    int n_items = CountSelectedMediaItems(nullptr);
    nlohmann::json sel_items = nlohmann::json::array();
    for (int i = 0; i < n_items; i++) {
        MediaItem* item = GetSelectedMediaItem(nullptr, i);
        if (!item)
            continue;
        double pos = GetMediaItemInfo_Value(item, "D_POSITION");
        double len = GetMediaItemInfo_Value(item, "D_LENGTH");
        MediaTrack* t = GetMediaItemTrack(item);
        int tidx = t ? static_cast<int>(GetMediaTrackInfo_Value(t, "IP_TRACKNUMBER")) - 1 : -1;
        sel_items.push_back(
                {{"index", i}, {"position", pos}, {"length", len}, {"track_index", tidx}});
    }

    json_ok(res, {{"tracks", sel_tracks}, {"items", sel_items}});
}

// GET /state/automation
void handle_state_automation(const httplib::Request& req, httplib::Response& res) {
    (void)req;

    MediaTrack* track = GetSelectedTrack(nullptr, 0);
    if (!track) {
        json_ok(res, {{"track_index", -1}, {"envelopes", nlohmann::json::array()}});
        return;
    }

    int track_idx = static_cast<int>(GetMediaTrackInfo_Value(track, "IP_TRACKNUMBER")) - 1;
    int n_env = CountTrackEnvelopes(track);
    nlohmann::json envelopes = nlohmann::json::array();

    for (int e = 0; e < n_env; e++) {
        TrackEnvelope* env = GetTrackEnvelope(track, e);
        if (!env)
            continue;

        char env_name[256] = {};
        GetEnvelopeName(env, env_name, sizeof(env_name));

        int n_pts = CountEnvelopePoints(env);
        nlohmann::json points = nlohmann::json::array();
        for (int p = 0; p < n_pts && p < 500; p++) {
            double tp = 0.0, val = 0.0, tens = 0.0;
            int sh = 0;
            bool sel = false;
            if (GetEnvelopePoint(env, p, &tp, &val, &sh, &tens, &sel)) {
                points.push_back({{"time", tp}, {"value", val}, {"shape", sh}, {"selected", sel}});
            }
        }

        envelopes.push_back({{"name", env_name}, {"point_count", n_pts}, {"points", points}});
    }

    json_ok(res, {{"track_index", track_idx}, {"envelopes", envelopes}});
}

}  // namespace ReaClaw::Handlers

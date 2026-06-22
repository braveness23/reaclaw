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
#include <functional>
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

// Drop cached state so the next read reflects a write immediately.
void invalidate_state_cache() {
    std::lock_guard<std::mutex> lk(s_cache_mutex);
    s_state_cache.erase("tracks");
    s_state_cache.erase("state");
    s_state_cache.erase("items");
}

// Run a mutating body inside a REAPER undo block so the change lands as one
// coherent, user-undoable step (Edit > Undo "<desc>"). Main-thread only — call
// from inside an Executor::post() lambda. When the body reports a validation
// error (it mutated nothing), the block is closed with extraflags 0 so REAPER
// creates no undo point, keeping the history clean on no-ops.
nlohmann::json with_undo(const char* desc, const std::function<nlohmann::json()>& body) {
    Undo_BeginBlock2(nullptr);
    nlohmann::json r = body();
    const bool changed = !(r.contains("_not_found") || r.contains("_bad_request") ||
                           r.contains("_error"));
    Undo_EndBlock2(nullptr, desc, changed ? -1 : 0);
    return r;
}

// Parse "#RRGGBB" into a REAPER native color with the custom-color flag set.
// Returns -1 if the string is not a valid 7-char hex color.
int parse_hex_color(const std::string& s) {
    if (s.size() != 7 || s[0] != '#')
        return -1;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    int v[6];
    for (int i = 0; i < 6; i++) {
        v[i] = nib(s[i + 1]);
        if (v[i] < 0)
            return -1;
    }
    int r = v[0] * 16 + v[1], g = v[2] * 16 + v[3], b = v[4] * 16 + v[5];
    return ColorToNative(r, g, b) | 0x1000000;
}

// Apply any present track properties from a JSON object. Main-thread only
// (touches the REAPER SDK). Silently ignores fields that are absent or wrongly
// typed, so the same helper serves create, single update, and batch update.
void apply_track_props(MediaTrack* track, const nlohmann::json& b) {
    if (b.contains("name") && b["name"].is_string()) {
        std::string nm = b["name"].get<std::string>();
        GetSetMediaTrackInfo_String(track, "P_NAME", const_cast<char*>(nm.c_str()), true);
    }
    if (b.contains("color")) {
        if (b["color"].is_null()) {
            int zero = 0;  // clear custom color → track uses the default
            GetSetMediaTrackInfo(track, "I_CUSTOMCOLOR", &zero);
        } else if (b["color"].is_string()) {
            int native = parse_hex_color(b["color"].get<std::string>());
            if (native >= 0)
                GetSetMediaTrackInfo(track, "I_CUSTOMCOLOR", &native);
        }
    }
    if (b.contains("folder_depth") && b["folder_depth"].is_number_integer()) {
        int fd = b["folder_depth"].get<int>();
        GetSetMediaTrackInfo(track, "I_FOLDERDEPTH", &fd);
    }
    if (b.contains("muted") && b["muted"].is_boolean()) {
        bool m = b["muted"].get<bool>();
        GetSetMediaTrackInfo(track, "B_MUTE", &m);
    }
    if (b.contains("soloed") && b["soloed"].is_boolean()) {
        int s = b["soloed"].get<bool>() ? 1 : 0;
        GetSetMediaTrackInfo(track, "I_SOLO", &s);
    }
    if (b.contains("armed") && b["armed"].is_boolean()) {
        int a = b["armed"].get<bool>() ? 1 : 0;
        GetSetMediaTrackInfo(track, "I_RECARM", &a);
    }
    if (b.contains("volume_db") && b["volume_db"].is_number()) {
        double v = db_to_vol(b["volume_db"].get<double>());
        GetSetMediaTrackInfo(track, "D_VOL", &v);
    }
    if (b.contains("pan") && b["pan"].is_number()) {
        double p = std::max(-1.0, std::min(1.0, b["pan"].get<double>()));
        GetSetMediaTrackInfo(track, "D_PAN", &p);
    }
}

// Apply any present send properties (category 0 = track sends) from a JSON
// object: vol/pan plus the extended props (mute, phase, mono, send mode).
// Main-thread only. Silently ignores absent or wrongly-typed fields.
void apply_send_props(MediaTrack* src, int sidx, const nlohmann::json& b) {
    if (b.contains("volume_db") && b["volume_db"].is_number()) {
        double v = db_to_vol(b["volume_db"].get<double>());
        SetTrackSendInfo_Value(src, 0, sidx, "D_VOL", v);
    }
    if (b.contains("pan") && b["pan"].is_number()) {
        double p = std::max(-1.0, std::min(1.0, b["pan"].get<double>()));
        SetTrackSendInfo_Value(src, 0, sidx, "D_PAN", p);
    }
    if (b.contains("muted") && b["muted"].is_boolean())
        SetTrackSendInfo_Value(src, 0, sidx, "B_MUTE", b["muted"].get<bool>() ? 1.0 : 0.0);
    if (b.contains("phase") && b["phase"].is_boolean())
        SetTrackSendInfo_Value(src, 0, sidx, "B_PHASE", b["phase"].get<bool>() ? 1.0 : 0.0);
    if (b.contains("mono") && b["mono"].is_boolean())
        SetTrackSendInfo_Value(src, 0, sidx, "B_MONO", b["mono"].get<bool>() ? 1.0 : 0.0);
    // Send mode: 0 = post-fader (default), 1 = pre-fx, 2 = pre-fader/post-fx.
    if (b.contains("mode") && b["mode"].is_number_integer())
        SetTrackSendInfo_Value(
                src, 0, sidx, "I_SENDMODE", static_cast<double>(b["mode"].get<int>()));
}

// Resolve an FX param reference ({"index":N} or {"name":"Threshold"}) to a
// 0-based param index, or -1 if not found. Main-thread only.
int resolve_fx_param(MediaTrack* track, int fx, const nlohmann::json& p) {
    if (p.contains("index") && p["index"].is_number_integer())
        return p["index"].get<int>();
    if (p.contains("name") && p["name"].is_string()) {
        std::string want = p["name"].get<std::string>();
        int n = TrackFX_GetNumParams(track, fx);
        for (int i = 0; i < n; i++) {
            char nm[256] = {};
            TrackFX_GetParamName(track, fx, i, nm, sizeof(nm));
            if (want == nm)
                return i;
        }
    }
    return -1;
}

// Apply an array of {index|name, value} normalized (0..1) param sets. Main thread.
void apply_fx_params(MediaTrack* track, int fx, const nlohmann::json& params) {
    if (!params.is_array())
        return;
    for (const auto& p : params) {
        if (!p.is_object() || !p.contains("value") || !p["value"].is_number())
            continue;
        int idx = resolve_fx_param(track, fx, p);
        if (idx < 0)
            continue;
        double v = std::max(0.0, std::min(1.0, p["value"].get<double>()));
        TrackFX_SetParamNormalized(track, fx, idx, v);
    }
}

// JSON snapshot of one FX slot's parameters: index, name, normalized value,
// formatted display string, and the real-unit range (min/max/mid + current raw
// value from TrackFX_GetParamEx) so an agent can reason in real units instead
// of guessing what a normalized 0..1 maps to.
nlohmann::json fx_params_json(MediaTrack* track, int fx) {
    nlohmann::json arr = nlohmann::json::array();
    int n = TrackFX_GetNumParams(track, fx);
    for (int i = 0; i < n; i++) {
        char nm[256] = {};
        TrackFX_GetParamName(track, fx, i, nm, sizeof(nm));
        double norm = TrackFX_GetParamNormalized(track, fx, i);
        char fmt[256] = {};
        TrackFX_FormatParamValue(track, fx, i, norm, fmt, sizeof(fmt));
        double pmin = 0.0, pmax = 1.0, pmid = 0.5;
        double raw = TrackFX_GetParamEx(track, fx, i, &pmin, &pmax, &pmid);
        arr.push_back({{"index", i},
                       {"name", nm},
                       {"value", norm},
                       {"formatted", fmt},
                       {"raw", raw},
                       {"min", pmin},
                       {"max", pmax},
                       {"mid", pmid}});
    }
    return arr;
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

    // Folder nesting: 1 = folder parent (children follow), 0 = normal track,
    // negative = closes that many folder levels (last track in folder).
    int folder_depth = 0;
    if (auto* fd = static_cast<int*>(GetSetMediaTrackInfo(track, "I_FOLDERDEPTH", nullptr)))
        folder_depth = *fd;

    // Custom track color: I_CUSTOMCOLOR carries the OS-native color OR'd with
    // 0x1000000 when a custom color is set, or 0 when the track uses the default.
    nlohmann::json color = nullptr;
    if (auto* c = static_cast<int*>(GetSetMediaTrackInfo(track, "I_CUSTOMCOLOR", nullptr))) {
        if (*c & 0x1000000) {
            int r = 0, g = 0, b = 0;
            ColorFromNative(*c & 0xFFFFFF, &r, &g, &b);
            char hex[8];
            snprintf(hex, sizeof(hex), "#%02X%02X%02X", r & 0xFF, g & 0xFF, b & 0xFF);
            color = hex;
        }
    }

    int send_count = GetTrackNumSends(track, 1);

    // Outgoing sends (category 0) with destination + level, so routing is
    // verifiable from the API instead of only via the GUI.
    nlohmann::json sends = nlohmann::json::array();
    int nsends = GetTrackNumSends(track, 0);
    for (int s = 0; s < nsends; s++) {
        double sv = GetTrackSendInfo_Value(track, 0, s, "D_VOL");
        double sp = GetTrackSendInfo_Value(track, 0, s, "D_PAN");
        bool s_mute = GetTrackSendInfo_Value(track, 0, s, "B_MUTE") != 0.0;
        bool s_phase = GetTrackSendInfo_Value(track, 0, s, "B_PHASE") != 0.0;
        bool s_mono = GetTrackSendInfo_Value(track, 0, s, "B_MONO") != 0.0;
        int s_mode = static_cast<int>(GetTrackSendInfo_Value(track, 0, s, "I_SENDMODE"));
        int dest_idx = -1;
        if (auto* dt = static_cast<MediaTrack*>(
                    GetSetTrackSendInfo(track, 0, s, "P_DESTTRACK", nullptr)))
            dest_idx = static_cast<int>(GetMediaTrackInfo_Value(dt, "IP_TRACKNUMBER")) - 1;
        sends.push_back({{"index", s},
                         {"dest_track", dest_idx},
                         {"volume_db", vol_to_db(sv)},
                         {"pan", sp},
                         {"muted", s_mute},
                         {"phase", s_phase},
                         {"mono", s_mono},
                         {"mode", s_mode}});
    }

    return {{"index", index},
            {"guid", guid_str},
            {"name", name[0] ? name : ""},
            {"muted", muted},
            {"soloed", soloed},
            {"armed", armed},
            {"volume_db", vol_to_db(vol)},
            {"pan", pan},
            {"folder_depth", folder_depth},
            {"color", color},
            {"fx", fx_arr},
            {"send_count", send_count},
            {"sends", sends}};
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
        return with_undo("ReaClaw: update track", [&]() -> nlohmann::json {
            int n = CountTracks(nullptr);
            if (index < 0 || index >= n)
                return {{"_not_found", true}};
            MediaTrack* track = GetTrack(nullptr, index);
            if (!track)
                return {{"_not_found", true}};

            apply_track_props(track, body);
            return track_to_json(track, index);
        });
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

namespace {

// Parse a numeric path param; returns false (and writes a 400) if not an int.
bool path_int(const httplib::Request& req, httplib::Response& res, const char* key, int& out) {
    try {
        out = std::stoi(req.path_params.at(key));
        return true;
    } catch (...) {
        json_error(res, 400, std::string(key) + " must be a numeric integer", "BAD_REQUEST");
        return false;
    }
}

bool parse_body(const httplib::Request& req, httplib::Response& res, nlohmann::json& out) {
    try {
        out = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
        return true;
    } catch (...) {
        json_error(res, 400, "Invalid JSON body", "BAD_REQUEST");
        return false;
    }
}

// Map the executor result's internal markers to an HTTP error; returns true if
// an error was written (caller should return), false if the result is success.
bool executor_error(httplib::Response& res, const nlohmann::json& result) {
    if (result.contains("_timeout")) {
        json_error(res, 408, "Main thread timeout", "TIMEOUT");
        return true;
    }
    if (result.contains("_error")) {
        json_error(res, 500, result["_error"].get<std::string>(), "INTERNAL_ERROR");
        return true;
    }
    if (result.contains("_not_found")) {
        json_error(res, 404, "Track index out of range", "NOT_FOUND");
        return true;
    }
    if (result.contains("_bad_request")) {
        std::string m = result.value("_message", "Bad request");
        json_error(res, 400, m, "BAD_REQUEST");
        return true;
    }
    return false;
}

}  // namespace

// POST /state/tracks — create and/or batch-update tracks.
// Body: { "create": [ {spec}, ... ], "update": [ {"index":i, ...}, ... ] }
// A spec accepts name, color, folder_depth, muted, soloed, armed, volume_db, pan.
void handle_state_tracks_post(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    if (!parse_body(req, res, body))
        return;
    bool has_create = body.contains("create") && body["create"].is_array();
    bool has_update = body.contains("update") && body["update"].is_array();
    if (!has_create && !has_update) {
        json_error(res, 400, "Provide a 'create' and/or 'update' array", "BAD_REQUEST");
        return;
    }

    auto result = Executor::post([body, has_create, has_update]() -> nlohmann::json {
        return with_undo("ReaClaw: edit tracks", [&]() -> nlohmann::json {
            nlohmann::json created = nlohmann::json::array();
            nlohmann::json updated = nlohmann::json::array();
            if (has_create) {
                for (const auto& spec : body["create"]) {
                    if (!spec.is_object())
                        continue;
                    int idx = CountTracks(nullptr);
                    InsertTrackAtIndex(idx, true);
                    MediaTrack* t = GetTrack(nullptr, idx);
                    if (t) {
                        apply_track_props(t, spec);
                        created.push_back(track_to_json(t, idx));
                    }
                }
            }
            if (has_update) {
                for (const auto& u : body["update"]) {
                    if (!u.is_object() || !u.contains("index") || !u["index"].is_number_integer())
                        continue;
                    int i = u["index"].get<int>();
                    if (i < 0 || i >= CountTracks(nullptr))
                        continue;
                    MediaTrack* t = GetTrack(nullptr, i);
                    if (t) {
                        apply_track_props(t, u);
                        updated.push_back(track_to_json(t, i));
                    }
                }
            }
            TrackList_AdjustWindows(false);
            return {{"created", created}, {"updated", updated}};
        });
    });

    if (executor_error(res, result))
        return;
    invalidate_state_cache();
    Log::info("Tracks: created " + std::to_string(result["created"].size()) + ", updated " +
              std::to_string(result["updated"].size()));
    json_ok(res, result);
}

// DELETE /state/tracks/{index}
void handle_state_delete_track(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    if (!path_int(req, res, "index", index))
        return;
    auto result = Executor::post([index]() -> nlohmann::json {
        return with_undo("ReaClaw: delete track", [&]() -> nlohmann::json {
            if (index < 0 || index >= CountTracks(nullptr))
                return {{"_not_found", true}};
            MediaTrack* t = GetTrack(nullptr, index);
            if (!t)
                return {{"_not_found", true}};
            DeleteTrack(t);
            TrackList_AdjustWindows(false);
            return {{"deleted", index}};
        });
    });
    if (executor_error(res, result))
        return;
    invalidate_state_cache();
    Log::info("Track " + std::to_string(index) + " deleted");
    json_ok(res, result);
}

// POST /state/tracks/{index}/fx — add an FX by name.
// Body: { "name": "ReaComp", "enabled": true, "params": [ {index|name, value} ] }
void handle_state_add_fx(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    if (!path_int(req, res, "index", index))
        return;
    nlohmann::json body;
    if (!parse_body(req, res, body))
        return;
    if (!body.contains("name") || !body["name"].is_string()) {
        json_error(res, 400, "Missing required field: name", "BAD_REQUEST");
        return;
    }
    std::string fx_name = body["name"].get<std::string>();
    auto result = Executor::post([index, fx_name, body]() -> nlohmann::json {
        return with_undo("ReaClaw: add FX", [&]() -> nlohmann::json {
            if (index < 0 || index >= CountTracks(nullptr))
                return {{"_not_found", true}};
            MediaTrack* t = GetTrack(nullptr, index);
            if (!t)
                return {{"_not_found", true}};
            int slot = TrackFX_AddByName(t, fx_name.c_str(), false, -1);
            if (slot < 0)
                return {{"_bad_request", true}, {"_message", "FX not found: " + fx_name}};
            if (body.contains("enabled") && body["enabled"].is_boolean())
                TrackFX_SetEnabled(t, slot, body["enabled"].get<bool>());
            if (body.contains("params"))
                apply_fx_params(t, slot, body["params"]);
            char resolved[256] = {};
            TrackFX_GetFXName(t, slot, resolved, sizeof(resolved));
            return {{"track", index},
                    {"slot", slot},
                    {"name", resolved},
                    {"enabled", TrackFX_GetEnabled(t, slot)}};
        });
    });
    if (executor_error(res, result))
        return;
    invalidate_state_cache();
    Log::info("Track " + std::to_string(index) + " add FX: " + fx_name);
    json_ok(res, result);
}

// GET /state/tracks/{index}/fx/{slot} — list an FX slot's parameters.
void handle_state_get_fx(const httplib::Request& req, httplib::Response& res) {
    int index = 0, slot = 0;
    if (!path_int(req, res, "index", index) || !path_int(req, res, "slot", slot))
        return;
    auto result = Executor::post([index, slot]() -> nlohmann::json {
        if (index < 0 || index >= CountTracks(nullptr))
            return {{"_not_found", true}};
        MediaTrack* t = GetTrack(nullptr, index);
        if (!t)
            return {{"_not_found", true}};
        if (slot < 0 || slot >= TrackFX_GetCount(t))
            return {{"_bad_request", true}, {"_message", "FX slot out of range"}};
        char nm[256] = {};
        TrackFX_GetFXName(t, slot, nm, sizeof(nm));
        return {{"track", index},
                {"slot", slot},
                {"name", nm},
                {"enabled", TrackFX_GetEnabled(t, slot)},
                {"params", fx_params_json(t, slot)}};
    });
    if (executor_error(res, result))
        return;
    json_ok(res, result);
}

// POST /state/tracks/{index}/fx/{slot} — set FX enabled state and/or params.
void handle_state_set_fx(const httplib::Request& req, httplib::Response& res) {
    int index = 0, slot = 0;
    if (!path_int(req, res, "index", index) || !path_int(req, res, "slot", slot))
        return;
    nlohmann::json body;
    if (!parse_body(req, res, body))
        return;
    auto result = Executor::post([index, slot, body]() -> nlohmann::json {
        return with_undo("ReaClaw: set FX", [&]() -> nlohmann::json {
            if (index < 0 || index >= CountTracks(nullptr))
                return {{"_not_found", true}};
            MediaTrack* t = GetTrack(nullptr, index);
            if (!t)
                return {{"_not_found", true}};
            if (slot < 0 || slot >= TrackFX_GetCount(t))
                return {{"_bad_request", true}, {"_message", "FX slot out of range"}};
            if (body.contains("enabled") && body["enabled"].is_boolean())
                TrackFX_SetEnabled(t, slot, body["enabled"].get<bool>());
            if (body.contains("params"))
                apply_fx_params(t, slot, body["params"]);
            char nm[256] = {};
            TrackFX_GetFXName(t, slot, nm, sizeof(nm));
            return {{"track", index},
                    {"slot", slot},
                    {"name", nm},
                    {"enabled", TrackFX_GetEnabled(t, slot)},
                    {"params", fx_params_json(t, slot)}};
        });
    });
    if (executor_error(res, result))
        return;
    invalidate_state_cache();
    json_ok(res, result);
}

// DELETE /state/tracks/{index}/fx/{slot}
void handle_state_delete_fx(const httplib::Request& req, httplib::Response& res) {
    int index = 0, slot = 0;
    if (!path_int(req, res, "index", index) || !path_int(req, res, "slot", slot))
        return;
    auto result = Executor::post([index, slot]() -> nlohmann::json {
        return with_undo("ReaClaw: delete FX", [&]() -> nlohmann::json {
            if (index < 0 || index >= CountTracks(nullptr))
                return {{"_not_found", true}};
            MediaTrack* t = GetTrack(nullptr, index);
            if (!t)
                return {{"_not_found", true}};
            if (slot < 0 || slot >= TrackFX_GetCount(t))
                return {{"_bad_request", true}, {"_message", "FX slot out of range"}};
            bool ok = TrackFX_Delete(t, slot);
            return {{"deleted", ok}, {"track", index}, {"slot", slot}};
        });
    });
    if (executor_error(res, result))
        return;
    invalidate_state_cache();
    json_ok(res, result);
}

// POST /state/tracks/{index}/sends — add a send to another track.
// Body: { "to_track": j, "volume_db": x, "pan": y }
void handle_state_add_send(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    if (!path_int(req, res, "index", index))
        return;
    nlohmann::json body;
    if (!parse_body(req, res, body))
        return;
    if (!body.contains("to_track") || !body["to_track"].is_number_integer()) {
        json_error(res, 400, "Missing required field: to_track", "BAD_REQUEST");
        return;
    }
    int to_track = body["to_track"].get<int>();
    auto result = Executor::post([index, to_track, body]() -> nlohmann::json {
        return with_undo("ReaClaw: add send", [&]() -> nlohmann::json {
            int n = CountTracks(nullptr);
            if (index < 0 || index >= n || to_track < 0 || to_track >= n)
                return {{"_not_found", true}};
            MediaTrack* src = GetTrack(nullptr, index);
            MediaTrack* dst = GetTrack(nullptr, to_track);
            if (!src || !dst)
                return {{"_not_found", true}};
            int sidx = CreateTrackSend(src, dst);
            if (sidx < 0)
                return {{"_bad_request", true}, {"_message", "Failed to create send"}};
            apply_send_props(src, sidx, body);
            double sv = GetTrackSendInfo_Value(src, 0, sidx, "D_VOL");
            double sp = GetTrackSendInfo_Value(src, 0, sidx, "D_PAN");
            return {{"track", index},
                    {"send_index", sidx},
                    {"dest_track", to_track},
                    {"volume_db", vol_to_db(sv)},
                    {"pan", sp}};
        });
    });
    if (executor_error(res, result))
        return;
    invalidate_state_cache();
    Log::info("Track " + std::to_string(index) + " send -> " + std::to_string(to_track));
    json_ok(res, result);
}

// POST /state/tracks/{index}/sends/{send} — update an existing send's
// properties: { volume_db, pan, muted, phase, mono, mode }.
void handle_state_set_send(const httplib::Request& req, httplib::Response& res) {
    int index = 0, send = 0;
    if (!path_int(req, res, "index", index) || !path_int(req, res, "send", send))
        return;
    nlohmann::json body;
    if (!parse_body(req, res, body))
        return;
    auto result = Executor::post([index, send, body]() -> nlohmann::json {
        return with_undo("ReaClaw: set send", [&]() -> nlohmann::json {
            if (index < 0 || index >= CountTracks(nullptr))
                return {{"_not_found", true}};
            MediaTrack* t = GetTrack(nullptr, index);
            if (!t)
                return {{"_not_found", true}};
            if (send < 0 || send >= GetTrackNumSends(t, 0))
                return {{"_bad_request", true}, {"_message", "Send index out of range"}};
            apply_send_props(t, send, body);
            return {{"track", index},
                    {"send_index", send},
                    {"volume_db", vol_to_db(GetTrackSendInfo_Value(t, 0, send, "D_VOL"))},
                    {"pan", GetTrackSendInfo_Value(t, 0, send, "D_PAN")},
                    {"muted", GetTrackSendInfo_Value(t, 0, send, "B_MUTE") != 0.0},
                    {"phase", GetTrackSendInfo_Value(t, 0, send, "B_PHASE") != 0.0},
                    {"mono", GetTrackSendInfo_Value(t, 0, send, "B_MONO") != 0.0},
                    {"mode", static_cast<int>(GetTrackSendInfo_Value(t, 0, send, "I_SENDMODE"))}};
        });
    });
    if (executor_error(res, result))
        return;
    invalidate_state_cache();
    json_ok(res, result);
}

// DELETE /state/tracks/{index}/sends/{send}
void handle_state_delete_send(const httplib::Request& req, httplib::Response& res) {
    int index = 0, send = 0;
    if (!path_int(req, res, "index", index) || !path_int(req, res, "send", send))
        return;
    auto result = Executor::post([index, send]() -> nlohmann::json {
        return with_undo("ReaClaw: delete send", [&]() -> nlohmann::json {
            if (index < 0 || index >= CountTracks(nullptr))
                return {{"_not_found", true}};
            MediaTrack* t = GetTrack(nullptr, index);
            if (!t)
                return {{"_not_found", true}};
            if (send < 0 || send >= GetTrackNumSends(t, 0))
                return {{"_bad_request", true}, {"_message", "Send index out of range"}};
            bool ok = RemoveTrackSend(t, 0, send);
            return {{"deleted", ok}, {"track", index}, {"send_index", send}};
        });
    });
    if (executor_error(res, result))
        return;
    invalidate_state_cache();
    json_ok(res, result);
}

// POST /state/selection — set the track selection.
// Body: { "tracks": [i, ...] } or { "tracks": "all" } or { "tracks": "none" }
void handle_state_set_selection(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    if (!parse_body(req, res, body))
        return;
    if (!body.contains("tracks")) {
        json_error(res, 400, "Missing required field: tracks", "BAD_REQUEST");
        return;
    }
    auto result = Executor::post([body]() -> nlohmann::json {
        int n = CountTracks(nullptr);
        for (int i = 0; i < n; i++)
            SetTrackSelected(GetTrack(nullptr, i), false);

        const auto& sel = body["tracks"];
        if (sel.is_string()) {
            std::string s = sel.get<std::string>();
            if (s == "all")
                for (int i = 0; i < n; i++)
                    SetTrackSelected(GetTrack(nullptr, i), true);
            // "none" leaves everything deselected
        } else if (sel.is_array()) {
            for (const auto& e : sel) {
                if (!e.is_number_integer())
                    continue;
                int i = e.get<int>();
                if (i >= 0 && i < n)
                    SetTrackSelected(GetTrack(nullptr, i), true);
            }
        }

        nlohmann::json selected = nlohmann::json::array();
        for (int i = 0; i < n; i++)
            if (GetMediaTrackInfo_Value(GetTrack(nullptr, i), "I_SELECTED") != 0.0)
                selected.push_back(i);
        return {{"selected_tracks", selected}};
    });
    if (executor_error(res, result))
        return;
    invalidate_state_cache();
    json_ok(res, result);
}

// GET /state/tracks/{index}/fx/{slot}/preset — current preset + count.
void handle_fx_get_preset(const httplib::Request& req, httplib::Response& res) {
    int index = 0, slot = 0;
    if (!path_int(req, res, "index", index) || !path_int(req, res, "slot", slot))
        return;
    auto result = Executor::post([index, slot]() -> nlohmann::json {
        if (index < 0 || index >= CountTracks(nullptr))
            return {{"_not_found", true}};
        MediaTrack* t = GetTrack(nullptr, index);
        if (!t)
            return {{"_not_found", true}};
        if (slot < 0 || slot >= TrackFX_GetCount(t))
            return {{"_bad_request", true}, {"_message", "FX slot out of range"}};
        char pname[512] = {};
        TrackFX_GetPreset(t, slot, pname, sizeof(pname));
        int total = 0;
        int cur = TrackFX_GetPresetIndex(t, slot, &total);
        return {{"track", index},
                {"slot", slot},
                {"preset", pname},
                {"preset_index", cur},
                {"preset_count", total}};
    });
    if (executor_error(res, result))
        return;
    json_ok(res, result);
}

// POST /state/tracks/{index}/fx/{slot}/preset — load a preset.
// Body: { "name": "Bus Comp" }  OR  { "navigate": -1 | 1 }  (prev/next preset).
void handle_fx_set_preset(const httplib::Request& req, httplib::Response& res) {
    int index = 0, slot = 0;
    if (!path_int(req, res, "index", index) || !path_int(req, res, "slot", slot))
        return;
    nlohmann::json body;
    if (!parse_body(req, res, body))
        return;
    bool has_name = body.contains("name") && body["name"].is_string();
    bool has_nav = body.contains("navigate") && body["navigate"].is_number_integer();
    if (!has_name && !has_nav) {
        json_error(res, 400, "Provide 'name' or 'navigate'", "BAD_REQUEST");
        return;
    }
    auto result = Executor::post([index, slot, body, has_name, has_nav]() -> nlohmann::json {
        return with_undo("ReaClaw: load FX preset", [&]() -> nlohmann::json {
            if (index < 0 || index >= CountTracks(nullptr))
                return {{"_not_found", true}};
            MediaTrack* t = GetTrack(nullptr, index);
            if (!t)
                return {{"_not_found", true}};
            if (slot < 0 || slot >= TrackFX_GetCount(t))
                return {{"_bad_request", true}, {"_message", "FX slot out of range"}};
            bool ok = false;
            if (has_name)
                ok = TrackFX_SetPreset(t, slot, body["name"].get<std::string>().c_str());
            else
                ok = TrackFX_NavigatePresets(t, slot, body["navigate"].get<int>());
            if (!ok)
                return {{"_bad_request", true}, {"_message", "Preset not found / not changed"}};
            char pname[512] = {};
            TrackFX_GetPreset(t, slot, pname, sizeof(pname));
            int total = 0;
            int cur = TrackFX_GetPresetIndex(t, slot, &total);
            return {{"track", index},
                    {"slot", slot},
                    {"preset", pname},
                    {"preset_index", cur},
                    {"preset_count", total}};
        });
    });
    if (executor_error(res, result))
        return;
    invalidate_state_cache();
    json_ok(res, result);
}

// POST /state/tracks/{index}/automation — write envelope points.
// Body: { "envelope": "Volume", "points": [ {time, value, shape?, tension?} ],
//         "clear_range": [start, end]? }
// `value` is the envelope's native value (e.g. linear volume, pan -1..1). The
// envelope is addressed by name (GetTrackEnvelopeByName); the FX/parameter
// envelopes use names like "Volume", "Pan", "Mute".
void handle_automation_write(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    if (!path_int(req, res, "index", index))
        return;
    nlohmann::json body;
    if (!parse_body(req, res, body))
        return;
    if (!body.contains("envelope") || !body["envelope"].is_string()) {
        json_error(res, 400, "Missing required field: envelope", "BAD_REQUEST");
        return;
    }
    if (!body.contains("points") || !body["points"].is_array()) {
        json_error(res, 400, "Missing required field: points (array)", "BAD_REQUEST");
        return;
    }
    auto result = Executor::post([index, body]() -> nlohmann::json {
        return with_undo("ReaClaw: write automation", [&]() -> nlohmann::json {
            if (index < 0 || index >= CountTracks(nullptr))
                return {{"_not_found", true}};
            MediaTrack* t = GetTrack(nullptr, index);
            if (!t)
                return {{"_not_found", true}};
            std::string env_name = body["envelope"].get<std::string>();
            TrackEnvelope* env = GetTrackEnvelopeByName(t, env_name.c_str());
            if (!env)
                return {{"_bad_request", true},
                        {"_message",
                         "Envelope not found: " + env_name +
                                 " (the parameter may need an active envelope first)"}};
            if (body.contains("clear_range") && body["clear_range"].is_array() &&
                body["clear_range"].size() == 2) {
                double cs = body["clear_range"][0].get<double>();
                double ce = body["clear_range"][1].get<double>();
                DeleteEnvelopePointRange(env, cs, ce);
            }
            int written = 0;
            for (const auto& p : body["points"]) {
                if (!p.is_object() || !p.contains("time") || !p.contains("value"))
                    continue;
                double time = p["time"].get<double>();
                double value = p["value"].get<double>();
                int shape = p.value("shape", 0);
                double tension = p.value("tension", 0.0);
                bool noSort = true;  // sort once at the end for efficiency
                if (InsertEnvelopePoint(env, time, value, shape, tension, false, &noSort))
                    written++;
            }
            Envelope_SortPoints(env);
            return {{"track", index}, {"envelope", env_name}, {"points_written", written}};
        });
    });
    if (executor_error(res, result))
        return;
    invalidate_state_cache();
    Log::info("Track " + std::to_string(index) + " automation write");
    json_ok(res, result);
}

}  // namespace ReaClaw::Handlers

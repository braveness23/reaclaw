#include "handlers/snapshot.h"

#include "app.h"
#include "handlers/common.h"
#include "handlers/visualize.h"
#include "reaper/executor.h"
#include "util/jsondiff.h"

#include <httplib.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include <reaper_plugin_functions.h>

#include <json.hpp>

namespace ReaClaw::Handlers {

namespace {

// One track's mutable, diff-worthy state. Deliberately a focused slice (not the
// full /state/tracks payload) so diffs stay legible and stable.
nlohmann::json track_snapshot(MediaTrack* t) {
    char name[256] = {};
    GetTrackName(t, name, sizeof(name));

    nlohmann::json color = nullptr;
    int ccol = static_cast<int>(GetMediaTrackInfo_Value(t, "I_CUSTOMCOLOR"));
    if (ccol & 0x1000000) {
        int r = 0, g = 0, b = 0;
        ColorFromNative(ccol & 0xFFFFFF, &r, &g, &b);
        char hex[8];
        snprintf(hex, sizeof(hex), "#%02X%02X%02X", r & 0xFF, g & 0xFF, b & 0xFF);
        color = hex;
    }

    nlohmann::json fx = nlohmann::json::array();
    int nfx = TrackFX_GetCount(t);
    for (int i = 0; i < nfx; i++) {
        char fxname[256] = {};
        TrackFX_GetFXName(t, i, fxname, sizeof(fxname));
        fx.push_back({{"name", fxname},
                      {"enabled", TrackFX_GetEnabled(t, i)},
                      {"offline", TrackFX_GetOffline(t, i)}});
    }

    nlohmann::json sends = nlohmann::json::array();
    int nsends = GetTrackNumSends(t, 0);
    for (int s = 0; s < nsends; s++) {
        sends.push_back({{"volume_db", vol_to_db(GetTrackSendInfo_Value(t, 0, s, "D_VOL"))},
                         {"muted", GetTrackSendInfo_Value(t, 0, s, "B_MUTE") != 0.0}});
    }

    return {{"name", name},
            {"volume_db", vol_to_db(GetMediaTrackInfo_Value(t, "D_VOL"))},
            {"pan", GetMediaTrackInfo_Value(t, "D_PAN")},
            {"muted", GetMediaTrackInfo_Value(t, "B_MUTE") != 0.0},
            {"soloed", GetMediaTrackInfo_Value(t, "I_SOLO") != 0.0},
            {"armed", GetMediaTrackInfo_Value(t, "I_RECARM") != 0.0},
            {"color", color},
            {"fx", fx},
            {"sends", sends},
            {"items", CountTrackMediaItems(t)}};
}

bool exec_error(httplib::Response& res, const nlohmann::json& result) {
    if (result.contains("_timeout")) {
        json_error(res, 408, "Snapshot timed out", "TIMEOUT");
        return true;
    }
    if (result.contains("_error")) {
        json_error(res, 500, result["_error"].get<std::string>(), "INTERNAL_ERROR");
        return true;
    }
    if (result.contains("_bad_request")) {
        json_error(res, 400, result.value("_message", "Bad request"), "BAD_REQUEST");
        return true;
    }
    if (result.contains("_not_found")) {
        json_error(res, 404, result.value("_message", "Not found"), "NOT_FOUND");
        return true;
    }
    return false;
}

// Load a stored snapshot's JSON by id; returns null json + ok=false if missing.
nlohmann::json load_snapshot(int64_t id, bool& ok) {
    std::string js = g_db.scalar_text("SELECT json FROM state_snapshots WHERE id = ?1",
                                      {std::to_string(id)});
    if (js.empty()) {
        ok = false;
        return nullptr;
    }
    ok = true;
    return nlohmann::json::parse(js, nullptr, false);
}

// Issue #53 — resolve an optional POST /snapshot `audio` target ({item} or
// {file}) to a frozen file path, so the snapshot can be A/B-visualized later
// even if the item is since reordered/deleted. Main-thread only (item lookup
// needs the SDK). Returns null if no audio target was requested; returns
// {_bad_request:true, _message} if `item` doesn't resolve to an audio source.
nlohmann::json resolve_audio_ref(const nlohmann::json& audio_req, double start, double end) {
    if (audio_req.is_null())
        return nullptr;
    if (audio_req.contains("item")) {
        int index = audio_req["item"].get<int>();
        if (index < 0 || index >= CountMediaItems(nullptr))
            return {{"_bad_request", true}, {"_message", "audio.item index out of range"}};
        MediaItem* it = GetMediaItem(nullptr, index);
        MediaItem_Take* take = it ? GetActiveTake(it) : nullptr;
        PCM_source* src = take ? GetMediaItemTake_Source(take) : nullptr;
        if (!src)
            return {{"_bad_request", true},
                    {"_message", "audio.item has no audio source (empty item or MIDI take)"}};
        char file[4096] = {};
        GetMediaSourceFileName(src, file, sizeof(file));
        return {{"item", index}, {"file", std::string(file)}, {"start", start}, {"end", end}};
    }
    if (audio_req.contains("file")) {
        return {{"file", audio_req["file"].get<std::string>()}, {"start", start}, {"end", end}};
    }
    return {{"_bad_request", true}, {"_message", "audio must have an 'item' or 'file' field"}};
}

}  // namespace

// Build the canonical snapshot (main-thread only).
nlohmann::json capture_state() {
    char pname[512] = {};
    GetProjectName(nullptr, pname, sizeof(pname));
    nlohmann::json tracks = nlohmann::json::array();
    int n = CountTracks(nullptr);
    for (int i = 0; i < n; i++) {
        nlohmann::json tj = track_snapshot(GetTrack(nullptr, i));
        tj["index"] = i;
        tracks.push_back(tj);
    }
    return {{"project",
             {{"name", pname},
              {"bpm", Master_GetTempo()},
              {"change_count", GetProjectStateChangeCount(nullptr)}}},
            {"tracks", tracks}};
}

// POST /snapshot {label?, audio?: {item|file}, start?, end?}
void handle_snapshot_create(const httplib::Request& req, httplib::Response& res) {
    std::string label;
    nlohmann::json audio_req;
    double astart = 0.0, aend = 0.0;
    if (!req.body.empty()) {
        auto b = nlohmann::json::parse(req.body, nullptr, false);
        if (b.is_object()) {
            if (b.contains("label") && b["label"].is_string())
                label = b["label"].get<std::string>();
            if (b.contains("audio") && b["audio"].is_object())
                audio_req = b["audio"];
            astart = b.value("start", 0.0);
            aend = b.value("end", 0.0);
        }
    }
    auto snap = Executor::post([audio_req, astart, aend]() -> nlohmann::json {
        nlohmann::json s = capture_state();
        if (!audio_req.is_null()) {
            nlohmann::json audio = resolve_audio_ref(audio_req, astart, aend);
            if (audio.contains("_bad_request"))
                return audio;
            s["audio"] = audio;
        }
        return s;
    });
    if (exec_error(res, snap))
        return;

    std::string taken_at = now_iso();
    g_db.query("INSERT INTO state_snapshots (taken_at, label, json) VALUES (?1, ?2, ?3)",
               {taken_at, label, snap.dump()});
    int64_t id = g_db.last_insert_rowid();
    nlohmann::json resp = {{"id", id},
                           {"taken_at", taken_at},
                           {"label", label},
                           {"summary", {{"track_count", snap["tracks"].size()}}}};
    if (snap.contains("audio"))
        resp["audio"] = snap["audio"];
    json_ok(res, resp);
}

// GET /snapshot  — list stored snapshots (newest first).
void handle_snapshot_list(const httplib::Request& req, httplib::Response& res) {
    (void)req;
    auto rows = g_db.query(
            "SELECT id, taken_at, label, "
            "json_array_length(json_extract(json,'$.tracks')) AS tracks "
            "FROM state_snapshots ORDER BY id DESC LIMIT 200");
    nlohmann::json arr = nlohmann::json::array();
    for (auto& r : rows) {
        int64_t id = 0;
        int tracks = 0;
        try {
            id = std::stoll(r.at("id"));
        } catch (...) {
        }
        try {
            tracks = std::stoi(r.at("tracks"));
        } catch (...) {
        }
        arr.push_back({{"id", id},
                       {"taken_at", r.count("taken_at") ? r.at("taken_at") : ""},
                       {"label", r.count("label") ? r.at("label") : ""},
                       {"track_count", tracks}});
    }
    json_ok(res, {{"snapshots", arr}});
}

// GET /snapshot/{id}
void handle_snapshot_get(const httplib::Request& req, httplib::Response& res) {
    int64_t id = 0;
    try {
        id = std::stoll(req.path_params.at("id"));
    } catch (...) {
        json_error(res, 400, "id must be an integer", "BAD_REQUEST");
        return;
    }
    bool ok = false;
    nlohmann::json snap = load_snapshot(id, ok);
    if (!ok) {
        json_error(res, 404, "No snapshot with id " + std::to_string(id), "NOT_FOUND");
        return;
    }
    std::string taken_at = g_db.scalar_text("SELECT taken_at FROM state_snapshots WHERE id = ?1",
                                            {std::to_string(id)});
    json_ok(res, {{"id", id}, {"taken_at", taken_at}, {"state", snap}});
}

// DELETE /snapshot/{id}
void handle_snapshot_delete(const httplib::Request& req, httplib::Response& res) {
    int64_t id = 0;
    try {
        id = std::stoll(req.path_params.at("id"));
    } catch (...) {
        json_error(res, 400, "id must be an integer", "BAD_REQUEST");
        return;
    }
    bool ok = false;
    load_snapshot(id, ok);
    if (!ok) {
        json_error(res, 404, "No snapshot with id " + std::to_string(id), "NOT_FOUND");
        return;
    }
    g_db.query("DELETE FROM state_snapshots WHERE id = ?1", {std::to_string(id)});
    json_ok(res, {{"deleted", id}});
}

// GET /snapshot/diff?from=<id>&to=<id|current>
// `to` defaults to a fresh live capture, so "what changed since snapshot N" is
// one call. Returns a flat list of {path, op, from?, to?} changes.
void handle_snapshot_diff(const httplib::Request& req, httplib::Response& res) {
    auto fit = req.params.find("from");
    if (fit == req.params.end()) {
        json_error(res, 400, "Missing required query param: from=<snapshot id>", "BAD_REQUEST");
        return;
    }
    int64_t from_id = 0;
    try {
        from_id = std::stoll(fit->second);
    } catch (...) {
        json_error(res, 400, "from must be an integer snapshot id", "BAD_REQUEST");
        return;
    }
    bool ok = false;
    nlohmann::json from_state = load_snapshot(from_id, ok);
    if (!ok) {
        json_error(res, 404, "No snapshot with id " + std::to_string(from_id), "NOT_FOUND");
        return;
    }

    auto tit = req.params.find("to");
    std::string to_label;
    nlohmann::json to_state;
    if (tit == req.params.end() || tit->second == "current" || tit->second == "live") {
        auto snap = Executor::post([]() -> nlohmann::json {
            return capture_state();
        });
        if (exec_error(res, snap))
            return;
        to_state = snap;
        to_label = "current";
    } else {
        int64_t to_id = 0;
        try {
            to_id = std::stoll(tit->second);
        } catch (...) {
            json_error(res, 400, "to must be an integer id, 'current', or omitted", "BAD_REQUEST");
            return;
        }
        bool ok2 = false;
        to_state = load_snapshot(to_id, ok2);
        if (!ok2) {
            json_error(res, 404, "No snapshot with id " + std::to_string(to_id), "NOT_FOUND");
            return;
        }
        to_label = std::to_string(to_id);
    }

    nlohmann::json changes = jsondiff::diff(from_state, to_state);
    json_ok(res,
            {{"from", from_id},
             {"to", to_label},
             {"change_count", changes.size()},
             {"changes", changes}});
}

// GET /snapshot/diff/visualize?from=<id>&to=<id|current>&type=&width=&height=&image=
//
// Issue #53 — the A/B visual diff. Both snapshots must have been captured
// with an `audio` target (POST /snapshot's optional `audio:{item|file}`).
// The `from` side always uses its frozen file+window; the `to` side, when
// `current`/omitted and the `from` audio was item-based, re-resolves that
// same item *now* (picking up a changed source/take), otherwise reuses the
// same file path fresh (a plain `file`-based audio target, or a `to`
// snapshot's own frozen reference). Reuses build_file_visualization (the
// same core behind GET /analysis/file/visualize) for both sides, then diffs
// the two digests with the same jsondiff used by /snapshot/diff.
void handle_snapshot_diff_visualize(const httplib::Request& req, httplib::Response& res) {
    auto fit = req.params.find("from");
    if (fit == req.params.end()) {
        json_error(res, 400, "Missing required query param: from=<snapshot id>", "BAD_REQUEST");
        return;
    }
    int64_t from_id = 0;
    try {
        from_id = std::stoll(fit->second);
    } catch (...) {
        json_error(res, 400, "from must be an integer snapshot id", "BAD_REQUEST");
        return;
    }
    bool ok = false;
    nlohmann::json from_state = load_snapshot(from_id, ok);
    if (!ok) {
        json_error(res, 404, "No snapshot with id " + std::to_string(from_id), "NOT_FOUND");
        return;
    }
    if (!from_state.contains("audio")) {
        json_error(res,
                   400,
                   "Snapshot " + std::to_string(from_id) +
                           " has no audio target — capture one with "
                           "audio:{item|file} in POST /snapshot",
                   "BAD_REQUEST");
        return;
    }
    nlohmann::json from_audio = from_state["audio"];

    auto tit = req.params.find("to");
    std::string to_label;
    nlohmann::json to_audio;
    bool resolve_to_live_item = false;
    if (tit == req.params.end() || tit->second == "current" || tit->second == "live") {
        to_label = "current";
        if (from_audio.contains("item")) {
            resolve_to_live_item = true;
        } else {
            to_audio = from_audio;  // same literal file, re-decoded fresh below
        }
    } else {
        int64_t to_id = 0;
        try {
            to_id = std::stoll(tit->second);
        } catch (...) {
            json_error(res, 400, "to must be an integer id, 'current', or omitted", "BAD_REQUEST");
            return;
        }
        bool ok2 = false;
        nlohmann::json to_state = load_snapshot(to_id, ok2);
        if (!ok2) {
            json_error(res, 404, "No snapshot with id " + std::to_string(to_id), "NOT_FOUND");
            return;
        }
        if (!to_state.contains("audio")) {
            json_error(res,
                       400,
                       "Snapshot " + std::to_string(to_id) +
                               " has no audio target — capture one with "
                               "audio:{item|file} in POST /snapshot",
                       "BAD_REQUEST");
            return;
        }
        to_audio = to_state["audio"];
        to_label = std::to_string(to_id);
    }

    std::string type_str = req.params.count("type") ? req.params.find("type")->second : "spectrum";
    int width = 640, height = 200;
    try {
        if (req.params.count("width"))
            width = std::clamp(std::stoi(req.params.find("width")->second), 160, 1024);
        if (req.params.count("height"))
            height = std::clamp(std::stoi(req.params.find("height")->second), 80, 512);
    } catch (...) {
    }
    bool image = !(req.params.count("image") && req.params.find("image")->second == "none");

    auto result = Executor::post(
            [from_audio, to_audio, resolve_to_live_item, type_str, width, height, image]() mutable
                    -> nlohmann::json {
                if (resolve_to_live_item) {
                    nlohmann::json item_ref = {{"item", from_audio["item"]}};
                    nlohmann::json resolved = resolve_audio_ref(
                            item_ref, from_audio.value("start", 0.0), from_audio.value("end", 0.0));
                    if (resolved.contains("_bad_request"))
                        return resolved;
                    to_audio = resolved;
                }
                nlohmann::json from_visual = build_file_visualization(
                        from_audio["file"].get<std::string>(),
                        type_str,
                        from_audio.value("start", 0.0),
                        from_audio.value("end", 0.0),
                        width,
                        height,
                        image);
                if (from_visual.contains("_bad_request") || from_visual.contains("_not_found"))
                    return from_visual;
                nlohmann::json to_visual = build_file_visualization(
                        to_audio["file"].get<std::string>(),
                        type_str,
                        to_audio.value("start", 0.0),
                        to_audio.value("end", 0.0),
                        width,
                        height,
                        image);
                if (to_visual.contains("_bad_request") || to_visual.contains("_not_found"))
                    return to_visual;
                return {{"from_visual", from_visual}, {"to_visual", to_visual}};
            },
            30);
    if (exec_error(res, result))
        return;

    nlohmann::json digest_delta = jsondiff::diff(result["from_visual"]["digest"],
                                                 result["to_visual"]["digest"]);
    json_ok(res,
            {{"from", from_id},
             {"to", to_label},
             {"type", result["from_visual"]["type"]},
             {"images", {{"from", result["from_visual"]}, {"to", result["to_visual"]}}},
             {"digest_delta", digest_delta}});
}

}  // namespace ReaClaw::Handlers

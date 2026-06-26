#include "handlers/snapshot.h"

#include "app.h"
#include "handlers/common.h"
#include "reaper/executor.h"
#include "util/jsondiff.h"

#include <httplib.h>

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

// POST /snapshot {label?}
void handle_snapshot_create(const httplib::Request& req, httplib::Response& res) {
    std::string label;
    if (!req.body.empty()) {
        auto b = nlohmann::json::parse(req.body, nullptr, false);
        if (b.is_object() && b.contains("label") && b["label"].is_string())
            label = b["label"].get<std::string>();
    }
    auto snap = Executor::post([]() -> nlohmann::json {
        return capture_state();
    });
    if (exec_error(res, snap))
        return;

    std::string taken_at = now_iso();
    g_db.query("INSERT INTO state_snapshots (taken_at, label, json) VALUES (?1, ?2, ?3)",
               {taken_at, label, snap.dump()});
    int64_t id = g_db.last_insert_rowid();
    json_ok(res,
            {{"id", id},
             {"taken_at", taken_at},
             {"label", label},
             {"summary", {{"track_count", snap["tracks"].size()}}}});
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

}  // namespace ReaClaw::Handlers

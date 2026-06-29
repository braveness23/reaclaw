#include "handlers/project.h"

#include "handlers/colorutil.h"
#include "handlers/common.h"
#include "reaper/executor.h"
#include "util/logging.h"

#include <httplib.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <reaper_plugin_functions.h>

#include <json.hpp>

namespace ReaClaw::Handlers {

namespace {

// Shared executor-result → HTTP error mapping (mirrors state.cpp).
bool exec_error(httplib::Response& res, const nlohmann::json& result) {
    if (result.contains("_timeout")) {
        json_error(res, 408, "Main thread timeout", "TIMEOUT");
        return true;
    }
    if (result.contains("_error")) {
        json_error(res, 500, result["_error"].get<std::string>(), "INTERNAL_ERROR");
        return true;
    }
    if (result.contains("_not_found")) {
        json_error(res, 404, "Not found", "NOT_FOUND");
        return true;
    }
    if (result.contains("_bad_request")) {
        json_error(res, 400, result.value("_message", "Bad request"), "BAD_REQUEST");
        return true;
    }
    return false;
}

bool query_double(const httplib::Request& req, const char* key, double& out) {
    auto it = req.params.find(key);
    if (it == req.params.end())
        return false;
    try {
        out = std::stod(it->second);
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

// GET /undo — what the undo/redo stack would do next (descriptions or null).
void handle_undo_state(const httplib::Request& req, httplib::Response& res) {
    (void)req;
    const char* u = Undo_CanUndo2(nullptr);
    const char* r = Undo_CanRedo2(nullptr);
    json_ok(res,
            {{"can_undo", u ? nlohmann::json(u) : nlohmann::json(nullptr)},
             {"can_redo", r ? nlohmann::json(r) : nlohmann::json(nullptr)}});
}

// POST /undo — perform one undo step.
void handle_undo(const httplib::Request& req, httplib::Response& res) {
    (void)req;
    auto result = Executor::post([]() -> nlohmann::json {
        const char* desc = Undo_CanUndo2(nullptr);
        std::string label = desc ? desc : "";
        bool ok = Undo_DoUndo2(nullptr) != 0;
        return {{"undone", ok}, {"description", label}};
    });
    if (exec_error(res, result))
        return;
    json_ok(res, result);
}

// POST /redo — perform one redo step.
void handle_redo(const httplib::Request& req, httplib::Response& res) {
    (void)req;
    auto result = Executor::post([]() -> nlohmann::json {
        const char* desc = Undo_CanRedo2(nullptr);
        std::string label = desc ? desc : "";
        bool ok = Undo_DoRedo2(nullptr) != 0;
        return {{"redone", ok}, {"description", label}};
    });
    if (exec_error(res, result))
        return;
    json_ok(res, result);
}

// GET /project — dirty flag (prompt-save signal), length, and notes scratchpad.
void handle_project_get(const httplib::Request& req, httplib::Response& res) {
    (void)req;
    bool dirty = IsProjectDirty(nullptr) != 0;
    double length = GetProjectLength(nullptr);
    std::vector<char> buf(1 << 16, 0);
    GetSetProjectNotes(nullptr, false, buf.data(), static_cast<int>(buf.size()));
    json_ok(res, {{"dirty", dirty}, {"length", length}, {"notes", std::string(buf.data())}});
}

// POST /project/notes — set the project notes (agent scratchpad in the .rpp).
// Body: { "notes": "..." }
void handle_project_notes(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (...) {
        json_error(res, 400, "Invalid JSON body", "BAD_REQUEST");
        return;
    }
    if (!body.contains("notes") || !body["notes"].is_string()) {
        json_error(res, 400, "Missing required field: notes", "BAD_REQUEST");
        return;
    }
    std::string notes = body["notes"].get<std::string>();
    auto result = Executor::post([notes]() -> nlohmann::json {
        std::vector<char> buf(notes.begin(), notes.end());
        buf.push_back('\0');
        GetSetProjectNotes(nullptr, true, buf.data(), static_cast<int>(buf.size()));
        MarkProjectDirty(nullptr);
        return {{"ok", true}, {"length", static_cast<int>(notes.size())}};
    });
    if (exec_error(res, result))
        return;
    json_ok(res, result);
}

// GET /state/markers — all markers and regions.
void handle_markers_get(const httplib::Request& req, httplib::Response& res) {
    (void)req;
    nlohmann::json markers = nlohmann::json::array();
    bool isrgn = false;
    double pos = 0.0, rgnend = 0.0;
    const char* name = nullptr;
    int markrgnidx = 0, color = 0;
    for (int idx = 0;
         EnumProjectMarkers3(nullptr, idx, &isrgn, &pos, &rgnend, &name, &markrgnidx, &color);
         ++idx) {
        markers.push_back({{"enum_index", idx},
                           {"id", markrgnidx},
                           {"is_region", isrgn},
                           {"position", pos},
                           {"region_end", isrgn ? rgnend : pos},
                           {"name", name ? name : ""},
                           {"color", native_color_to_hex(color)}});
    }
    json_ok(res, {{"markers", markers}});
}

// POST /state/markers — add a marker or region.
// Body: { position, name?, is_region?, region_end?, color?("#RRGGBB"), id? }
void handle_markers_post(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (...) {
        json_error(res, 400, "Invalid JSON body", "BAD_REQUEST");
        return;
    }
    if (!body.contains("position") || !body["position"].is_number()) {
        json_error(res, 400, "Missing required field: position", "BAD_REQUEST");
        return;
    }
    auto result = Executor::post([body]() -> nlohmann::json {
        Undo_BeginBlock2(nullptr);
        bool isrgn = body.value("is_region", false);
        double pos = body["position"].get<double>();
        double rgnend = body.value("region_end", pos);
        std::string name = body.value("name", std::string());
        int wantidx = body.contains("id") && body["id"].is_number_integer() ? body["id"].get<int>()
                                                                            : -1;
        int color = 0;
        if (body.contains("color") && body["color"].is_string()) {
            int c = parse_hex_color(body["color"].get<std::string>());
            if (c >= 0)
                color = c;
        }
        int id = AddProjectMarker2(nullptr, isrgn, pos, rgnend, name.c_str(), wantidx, color);
        Undo_EndBlock2(nullptr, isrgn ? "ReaClaw: add region" : "ReaClaw: add marker", -1);
        return {{"id", id}, {"is_region", isrgn}, {"position", pos}};
    });
    if (exec_error(res, result))
        return;
    Log::info("Marker/region added");
    json_ok(res, result);
}

// DELETE /state/markers/{id}?is_region=true|false
void handle_markers_delete(const httplib::Request& req, httplib::Response& res) {
    int id = 0;
    try {
        id = std::stoi(req.path_params.at("id"));
    } catch (...) {
        json_error(res, 400, "Marker id must be a numeric integer", "BAD_REQUEST");
        return;
    }
    bool is_region = false;
    auto it = req.params.find("is_region");
    if (it != req.params.end())
        is_region = (it->second == "true" || it->second == "1");
    auto result = Executor::post([id, is_region]() -> nlohmann::json {
        Undo_BeginBlock2(nullptr);
        bool ok = DeleteProjectMarker(nullptr, id, is_region);
        Undo_EndBlock2(nullptr, "ReaClaw: delete marker/region", ok ? -1 : 0);
        if (!ok)
            return {{"_not_found", true}};
        return {{"deleted", true}, {"id", id}, {"is_region", is_region}};
    });
    if (exec_error(res, result))
        return;
    json_ok(res, result);
}

// GET /state/tempo — the full tempo / time-signature map.
void handle_tempo_get(const httplib::Request& req, httplib::Response& res) {
    (void)req;
    nlohmann::json markers = nlohmann::json::array();
    int n = CountTempoTimeSigMarkers(nullptr);
    for (int i = 0; i < n; i++) {
        double timepos = 0.0, beatpos = 0.0, bpm = 0.0;
        int measurepos = 0, num = 0, denom = 0;
        bool linear = false;
        if (GetTempoTimeSigMarker(
                    nullptr, i, &timepos, &measurepos, &beatpos, &bpm, &num, &denom, &linear)) {
            markers.push_back({{"index", i},
                               {"time", timepos},
                               {"measure", measurepos},
                               {"beat", beatpos},
                               {"bpm", bpm},
                               {"timesig_num", num},
                               {"timesig_denom", denom},
                               {"linear", linear}});
        }
    }
    json_ok(res, {{"markers", markers}, {"count", n}});
}

// POST /state/tempo — add a tempo / time-signature marker.
// Body: { time, bpm, timesig_num?, timesig_denom?, linear? }
void handle_tempo_post(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (...) {
        json_error(res, 400, "Invalid JSON body", "BAD_REQUEST");
        return;
    }
    if (!body.contains("time") || !body["time"].is_number() || !body.contains("bpm") ||
        !body["bpm"].is_number()) {
        json_error(res, 400, "Required fields: time, bpm", "BAD_REQUEST");
        return;
    }
    auto result = Executor::post([body]() -> nlohmann::json {
        Undo_BeginBlock2(nullptr);
        double time = body["time"].get<double>();
        double bpm = body["bpm"].get<double>();
        int num = body.value("timesig_num", 0);
        int denom = body.value("timesig_denom", 0);
        bool linear = body.value("linear", false);
        bool ok = AddTempoTimeSigMarker(nullptr, time, bpm, num, denom, linear);
        Undo_EndBlock2(nullptr, "ReaClaw: add tempo marker", ok ? -1 : 0);
        if (!ok)
            return {{"_bad_request", true}, {"_message", "Failed to add tempo marker"}};
        return {{"ok", true}, {"time", time}, {"bpm", bpm}};
    });
    if (exec_error(res, result))
        return;
    json_ok(res, result);
}

// GET /time — beat <-> time conversion.
//   ?time=SECONDS              -> beats/measure for that time
//   ?beats=BEATS[&measure=M]   -> seconds for that beat position
void handle_time_convert(const httplib::Request& req, httplib::Response& res) {
    double time = 0.0, beats = 0.0;
    bool have_time = query_double(req, "time", time);
    bool have_beats = query_double(req, "beats", beats);
    if (have_time) {
        int measures = 0, cml = 0, cdenom = 0;
        double fullbeats = 0.0;
        double beat_in_measure = TimeMap2_timeToBeats(
                nullptr, time, &measures, &cml, &fullbeats, &cdenom);
        json_ok(res,
                {{"time", time},
                 {"full_beats", fullbeats},
                 {"beat_in_measure", beat_in_measure},
                 {"measure", measures},
                 {"measure_length_beats", cml},
                 {"timesig_denom", cdenom}});
        return;
    }
    if (have_beats) {
        double m = 0.0;
        bool have_measure = query_double(req, "measure", m);
        int measure = static_cast<int>(m);
        double seconds = TimeMap2_beatsToTime(nullptr, beats, have_measure ? &measure : nullptr);
        json_ok(res, {{"beats", beats}, {"time", seconds}});
        return;
    }
    json_error(res, 400, "Provide ?time= or ?beats=", "BAD_REQUEST");
}

// GET /project/extstate?section=S[&key=K]
//   With section+key: the stored value (or null if unset).
//   With section only: all key/value pairs stored under that section.
// Per-project ext state lives inside the .rpp, so it survives close/reopen
// (unlike the SQLite scratchpad, which is global to the ReaClaw install).
void handle_extstate_get(const httplib::Request& req, httplib::Response& res) {
    auto sit = req.params.find("section");
    if (sit == req.params.end() || sit->second.empty()) {
        json_error(res, 400, "Missing required query param: section", "BAD_REQUEST");
        return;
    }
    std::string section = sit->second;
    auto kit = req.params.find("key");
    bool has_key = kit != req.params.end();
    std::string key = has_key ? kit->second : "";

    auto result = Executor::post([section, key, has_key]() -> nlohmann::json {
        if (has_key) {
            std::vector<char> buf(1 << 16, 0);
            int got = GetProjExtState(nullptr,
                                      section.c_str(),
                                      key.c_str(),
                                      buf.data(),
                                      static_cast<int>(buf.size()));
            return {{"section", section},
                    {"key", key},
                    {"value",
                     got > 0 ? nlohmann::json(std::string(buf.data())) : nlohmann::json(nullptr)}};
        }
        nlohmann::json values = nlohmann::json::object();
        for (int idx = 0;; idx++) {
            std::vector<char> kbuf(4096, 0), vbuf(1 << 16, 0);
            if (!EnumProjExtState(nullptr,
                                  section.c_str(),
                                  idx,
                                  kbuf.data(),
                                  static_cast<int>(kbuf.size()),
                                  vbuf.data(),
                                  static_cast<int>(vbuf.size())))
                break;
            values[std::string(kbuf.data())] = std::string(vbuf.data());
        }
        return {{"section", section}, {"values", values}};
    });
    if (exec_error(res, result))
        return;
    json_ok(res, result);
}

// POST /project/extstate — store a value. Body: { section, key, value }.
void handle_extstate_post(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (...) {
        json_error(res, 400, "Invalid JSON body", "BAD_REQUEST");
        return;
    }
    if (!body.contains("section") || !body["section"].is_string() || !body.contains("key") ||
        !body["key"].is_string() || !body.contains("value") || !body["value"].is_string()) {
        json_error(res, 400, "Required string fields: section, key, value", "BAD_REQUEST");
        return;
    }
    std::string section = body["section"].get<std::string>();
    std::string key = body["key"].get<std::string>();
    std::string value = body["value"].get<std::string>();
    auto result = Executor::post([section, key, value]() -> nlohmann::json {
        SetProjExtState(nullptr, section.c_str(), key.c_str(), value.c_str());
        MarkProjectDirty(nullptr);
        return {{"ok", true}, {"section", section}, {"key", key}};
    });
    if (exec_error(res, result))
        return;
    json_ok(res, result);
}

// DELETE /project/extstate?section=S&key=K — remove one stored value (storing an
// empty value deletes the key in REAPER's project ext state).
void handle_extstate_delete(const httplib::Request& req, httplib::Response& res) {
    auto sit = req.params.find("section");
    auto kit = req.params.find("key");
    if (sit == req.params.end() || sit->second.empty() || kit == req.params.end() ||
        kit->second.empty()) {
        json_error(res, 400, "Required query params: section, key", "BAD_REQUEST");
        return;
    }
    std::string section = sit->second, key = kit->second;
    auto result = Executor::post([section, key]() -> nlohmann::json {
        SetProjExtState(nullptr, section.c_str(), key.c_str(), "");
        MarkProjectDirty(nullptr);
        return {{"deleted", true}, {"section", section}, {"key", key}};
    });
    if (exec_error(res, result))
        return;
    json_ok(res, result);
}

// ---------------------------------------------------------------------------
// Issue #34 — project lifecycle: new / open / save / reset
// ---------------------------------------------------------------------------

namespace {

// Return the project's current filename (empty string if never saved).
// Must be called from the main thread.
std::string project_filename() {
    if (!GetSetProjectInfo_String)
        return "";
    std::vector<char> buf(4096, 0);
    GetSetProjectInfo_String(nullptr, "PROJECT_FILENAME", buf.data(), false);
    return std::string(buf.data());
}

// Silently discard unsaved changes by saving to a throwaway file, clearing
// the dirty flag so the subsequent new/open call skips the save-changes dialog.
// Must be called from the main thread.
static const char* k_discard_tmp = "/tmp/reaclaw_discard.rpp";
void discard_changes() {
    if (Main_SaveProjectEx)
        Main_SaveProjectEx(nullptr, k_discard_tmp, 0);
    std::remove(k_discard_tmp);
}

// In-place project reset: delete every regular track (and their items/envelopes),
// all markers/regions, all extra tempo markers, then zero the time selection,
// cursor, repeat flag, and project notes. Used by both /project/new (as a
// fallback) and /project/reset. Must be called from the main thread.
void reset_project_state(bool clear_notes, bool clear_name) {
    // 1. Delete all regular tracks (items and envelopes go with them).
    if (DeleteTrack && GetTrack)
        while (CountTracks && CountTracks(nullptr) > 0)
            DeleteTrack(GetTrack(nullptr, 0));

    // 2. Delete all markers and regions.
    if (DeleteProjectMarkerByIndex)
        while (DeleteProjectMarkerByIndex(nullptr, 0)) {
        }

    // 3. Strip extra tempo markers; reset marker 0 to 120 BPM, 4/4.
    if (CountTempoTimeSigMarkers && DeleteTempoTimeSigMarker && SetTempoTimeSigMarker) {
        int n = CountTempoTimeSigMarkers(nullptr);
        for (int i = n - 1; i >= 1; --i)
            DeleteTempoTimeSigMarker(nullptr, i);
        SetTempoTimeSigMarker(nullptr, 0, 0.0, -1, 0.0, 120.0, 4, 4, false);
    }

    // 4. Clear time selection + loop range.
    if (GetSet_LoopTimeRange) {
        double s = 0.0, e = 0.0;
        GetSet_LoopTimeRange(true, false, &s, &e, false);  // time selection
        GetSet_LoopTimeRange(true, true, &s, &e, false);   // loop range
    }

    // 5. Disable repeat.
    if (GetSetRepeat)
        GetSetRepeat(0);

    // 6. Cursor to 0.
    if (SetEditCurPos)
        SetEditCurPos(0.0, false, false);

    // 7. Optionally clear project notes.
    if (clear_notes && GetSetProjectNotes) {
        std::vector<char> empty(2, 0);
        GetSetProjectNotes(nullptr, true, empty.data(), static_cast<int>(empty.size()));
    }

    // 8. Optionally reset project name.
    if (clear_name && GetSetProjectInfo_String) {
        std::vector<char> empty(2, 0);
        GetSetProjectInfo_String(nullptr, "PROJECT_NAME", empty.data(), true);
    }
}

}  // namespace

// POST /project/new
// Body: { discard_changes?: false }
// Opens a new blank project from REAPER's default template without a GUI modal.
// Returns 409 if the current project is dirty and discard_changes is not true.
void handle_project_new(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (...) {
        body = nlohmann::json::object();
    }
    bool discard = body.value("discard_changes", false);

    if (!discard && IsProjectDirty && IsProjectDirty(nullptr)) {
        json_error(res,
                   409,
                   "Project has unsaved changes; send discard_changes:true to proceed",
                   "UNSAVED_CHANGES");
        return;
    }

    auto result = Executor::post([discard]() -> nlohmann::json {
        if (discard && IsProjectDirty && IsProjectDirty(nullptr))
            discard_changes();

        if (Main_openProject) {
            Main_openProject("");
        } else {
            reset_project_state(true, true);
        }
        return {{"ok", true}};
    });
    if (exec_error(res, result))
        return;
    Log::info("POST /project/new");
    json_ok(res, result);
}

// POST /project/open
// Body: { path, discard_changes?: false }
// Opens a .rpp file, replacing the current project. Returns 409 on dirty
// project (without discard_changes:true) and 400 if path is missing or the
// file does not exist. Multi-project (tab mode) is not supported; the file
// replaces the current project.
void handle_project_open(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (...) {
        json_error(res, 400, "Invalid JSON body", "BAD_REQUEST");
        return;
    }
    if (!body.contains("path") || !body["path"].is_string() ||
        body["path"].get<std::string>().empty()) {
        json_error(res, 400, "Missing required field: path", "BAD_REQUEST");
        return;
    }
    std::string path = body["path"].get<std::string>();
    bool discard = body.value("discard_changes", false);

    {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            json_error(res, 400, "File not found: " + path, "BAD_REQUEST");
            return;
        }
    }

    if (!discard && IsProjectDirty && IsProjectDirty(nullptr)) {
        json_error(res,
                   409,
                   "Project has unsaved changes; send discard_changes:true to proceed",
                   "UNSAVED_CHANGES");
        return;
    }

    auto result = Executor::post([path, discard]() -> nlohmann::json {
        if (discard && IsProjectDirty && IsProjectDirty(nullptr))
            discard_changes();

        if (!Main_openProject)
            return {{"_error", "Main_openProject not available"}};
        Main_openProject(path.c_str());
        return {{"ok", true}, {"path", path}};
    });
    if (exec_error(res, result))
        return;
    Log::info("POST /project/open: " + path);
    json_ok(res, result);
}

// POST /project/save
// Body: { path? }
// Saves the current project. If path is provided, saves to that file and
// updates the project's current filename (save-as). If path is omitted, saves
// to the current project filename; returns 400 if the project has never been
// saved (no filename yet). path should be an absolute filesystem path.
void handle_project_save(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (...) {
        body = nlohmann::json::object();
    }

    std::string path = body.value("path", std::string());

    auto result = Executor::post([path]() -> nlohmann::json {
        if (path.empty()) {
            // In-place save: require an existing filename.
            std::string cur = project_filename();
            if (cur.empty())
                return {{"_bad_request", true},
                        {"_message", "Project has never been saved; provide a path field"}};
            if (Main_SaveProjectEx)
                Main_SaveProjectEx(nullptr, nullptr, 0);
            return {{"ok", true}, {"path", cur}};
        }

        // Save-as: save to the given path and update the project filename (&8).
        if (!Main_SaveProjectEx)
            return {{"_error", "Main_SaveProjectEx not available"}};
        Main_SaveProjectEx(nullptr, path.c_str(), 8);

        // Confirm the file was written.
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
            return {{"_error", "Save appeared to succeed but file not found: " + path}};
        return {{"ok", true}, {"path", path}};
    });
    if (exec_error(res, result))
        return;
    Log::info("POST /project/save: " + path);
    json_ok(res, result);
}

// POST /project/reset
// Body: { discard_changes?: false, keep_master?: true }
// Blanks the current project in-place: deletes all tracks/items/envelopes,
// all markers and regions, all extra tempo markers (resets to 120 BPM 4/4),
// clears time selection and loop, parks cursor at 0, clears project notes.
// keep_master (default true) leaves master track FX/vol/pan untouched.
// Returns 409 if dirty and discard_changes is not true.
void handle_project_reset(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (...) {
        body = nlohmann::json::object();
    }
    bool discard = body.value("discard_changes", false);

    if (!discard && IsProjectDirty && IsProjectDirty(nullptr)) {
        json_error(res,
                   409,
                   "Project has unsaved changes; send discard_changes:true to proceed",
                   "UNSAVED_CHANGES");
        return;
    }

    auto result = Executor::post([]() -> nlohmann::json {
        reset_project_state(true, false);
        return {{"ok", true},
                {"tracks", CountTracks ? CountTracks(nullptr) : 0},
                {"markers", 0},
                {"tempo_markers",
                 CountTempoTimeSigMarkers ? CountTempoTimeSigMarkers(nullptr) : 1}};
    });
    if (exec_error(res, result))
        return;
    Log::info("POST /project/reset");
    json_ok(res, result);
}

}  // namespace ReaClaw::Handlers

#include "handlers/transport.h"

#include "handlers/common.h"
#include "reaper/executor.h"
#include "util/logging.h"

#include <httplib.h>

#include <json.hpp>

// REAPER SDK — extern declarations (REAPERAPI_IMPLEMENT only in reaper/api.cpp)
#include <string>

#include <reaper_plugin_functions.h>

namespace ReaClaw::Handlers {

// POST /transport
// Body: { "action": "play" | "stop" | "pause" | "record" }
//
// Backed by CSurf_On* rather than Main_OnCommand action IDs so the state-change
// semantics are unambiguous and version-stable. All four verbs are main-thread
// only and dispatched through the executor.
void handle_transport_action(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try {
        body = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
    } catch (...) {
        json_error(res, 400, "Invalid JSON body", "BAD_REQUEST");
        return;
    }
    if (!body.contains("action") || !body["action"].is_string()) {
        json_error(res, 400, "Missing required field: action (play|stop|pause|record)", "BAD_REQUEST");
        return;
    }
    std::string action = body["action"].get<std::string>();
    if (action != "play" && action != "stop" && action != "pause" && action != "record") {
        json_error(res, 400, "action must be one of: play, stop, pause, record", "BAD_REQUEST");
        return;
    }

    auto result = Executor::post([action]() -> nlohmann::json {
        if (action == "play") {
            if (CSurf_OnPlay)
                CSurf_OnPlay();
        } else if (action == "stop") {
            if (CSurf_OnStop)
                CSurf_OnStop();
        } else if (action == "pause") {
            if (CSurf_OnPause)
                CSurf_OnPause();
        } else if (action == "record") {
            if (CSurf_OnRecord)
                CSurf_OnRecord();
        }
        int ps = GetPlayState ? GetPlayState() : 0;
        double pos = GetPlayPosition ? GetPlayPosition() : 0.0;
        return {{"action", action},
                {"transport",
                 {{"playing", (ps & 1) != 0},
                  {"recording", (ps & 4) != 0},
                  {"paused", (ps & 2) != 0},
                  {"position", pos}}}};
    });

    if (result.contains("_timeout")) {
        json_error(res, 408, "Main thread timeout", "TIMEOUT");
        return;
    }
    if (result.contains("_error")) {
        json_error(res, 500, result["_error"].get<std::string>(), "INTERNAL_ERROR");
        return;
    }

    Log::info("Transport: " + action);
    json_ok(res, result);
}

// POST /transport/cursor
// Body: { "position": <seconds> }
//
// Moves the edit cursor (and optionally the play cursor) to the given project
// position in seconds. Uses SetEditCurPos with the moveview and seekplay flags
// defaulting to false so the arrange view and playback position are not
// disturbed unless requested.
void handle_transport_cursor(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try {
        body = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
    } catch (...) {
        json_error(res, 400, "Invalid JSON body", "BAD_REQUEST");
        return;
    }
    if (!body.contains("position") || !body["position"].is_number()) {
        json_error(res, 400, "Missing required field: position (seconds)", "BAD_REQUEST");
        return;
    }
    double pos = body["position"].get<double>();
    if (pos < 0.0) {
        json_error(res, 400, "position must be >= 0", "BAD_REQUEST");
        return;
    }
    bool moveview = body.value("moveview", false);
    bool seekplay = body.value("seekplay", false);

    auto result = Executor::post([pos, moveview, seekplay]() -> nlohmann::json {
        if (SetEditCurPos)
            SetEditCurPos(pos, moveview, seekplay);
        double actual = GetCursorPosition ? GetCursorPosition() : pos;
        return {{"position", actual}};
    });

    if (result.contains("_timeout")) {
        json_error(res, 408, "Main thread timeout", "TIMEOUT");
        return;
    }
    if (result.contains("_error")) {
        json_error(res, 500, result["_error"].get<std::string>(), "INTERNAL_ERROR");
        return;
    }

    json_ok(res, result);
}

// POST /transport/loop
// Body: { "start": <seconds>, "end": <seconds>, "enabled": <bool> }
//
// Sets the project loop/repeat range and optionally enables or disables looping.
// All fields are optional: omit start/end to leave the range unchanged; omit
// enabled to leave the repeat toggle unchanged.
void handle_transport_loop(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try {
        body = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
    } catch (...) {
        json_error(res, 400, "Invalid JSON body", "BAD_REQUEST");
        return;
    }

    bool has_range = body.contains("start") && body.contains("end");
    bool has_enabled = body.contains("enabled") && body["enabled"].is_boolean();

    if (!has_range && !has_enabled) {
        json_error(res, 400, "Provide 'start'+'end' and/or 'enabled'", "BAD_REQUEST");
        return;
    }

    double start = has_range ? body["start"].get<double>() : 0.0;
    double end = has_range ? body["end"].get<double>() : 0.0;
    if (has_range && end <= start) {
        json_error(res, 400, "end must be greater than start", "BAD_REQUEST");
        return;
    }
    bool enabled_val = has_enabled ? body["enabled"].get<bool>() : false;

    auto result = Executor::post([has_range, start, end, has_enabled, enabled_val]() -> nlohmann::json {
        if (has_range && GetSet_LoopTimeRange2) {
            double s = start, e = end;
            GetSet_LoopTimeRange2(nullptr, true, false, &s, &e, false);
        }
        if (has_enabled && GetSetRepeatEx)
            GetSetRepeatEx(nullptr, enabled_val ? 1 : 0);

        double lo = 0.0, hi = 0.0;
        if (GetSet_LoopTimeRange2)
            GetSet_LoopTimeRange2(nullptr, false, false, &lo, &hi, false);
        int rep = GetSetRepeatEx ? GetSetRepeatEx(nullptr, -1) : 0;
        return {{"start", lo}, {"end", hi}, {"enabled", rep != 0}};
    });

    if (result.contains("_timeout")) {
        json_error(res, 408, "Main thread timeout", "TIMEOUT");
        return;
    }
    if (result.contains("_error")) {
        json_error(res, 500, result["_error"].get<std::string>(), "INTERNAL_ERROR");
        return;
    }

    json_ok(res, result);
}

}  // namespace ReaClaw::Handlers

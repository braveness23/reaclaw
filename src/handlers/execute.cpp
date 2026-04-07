#include "handlers/execute.h"
#include "handlers/common.h"
#include "reaper/executor.h"
#include "app.h"

#include <httplib.h>
#include <json.hpp>

// REAPER SDK — extern declarations (REAPERAPI_IMPLEMENT only in reaper/api.cpp)
#include <reaper_plugin_functions.h>

#include <cmath>
#include <string>

namespace ReaClaw::Handlers {

namespace {

void log_history(const std::string& type, const std::string& target_id,
                 const std::string& ag, const std::string& status,
                 const std::string& err_msg = "") {
    if (!g_config.log_all_executions) return;
    g_db.query(
        "INSERT INTO execution_history(type, target_id, agent_id, status, error) "
        "VALUES(?1, ?2, ?3, ?4, ?5)",
        {type, target_id, ag, status, err_msg});
}

// Resolve a JSON id value (int or string) to a REAPER command id.
int resolve_cmd_id(const nlohmann::json& id_val) {
    if (id_val.is_number_integer()) return id_val.get<int>();
    if (id_val.is_string()) {
        std::string name = id_val.get<std::string>();
        return NamedCommandLookup(name.c_str());
    }
    return 0;
}

// Lightweight post-execution feedback snapshot (threadsafe reads).
nlohmann::json build_feedback() {
    int play_raw = GetPlayState();
    int n = CountTracks(nullptr);
    nlohmann::json tracks = nlohmann::json::array();
    for (int i = 0; i < n; i++) {
        MediaTrack* t = GetTrack(nullptr, i);
        if (!t) continue;
        char name[256] = {};
        GetTrackName(t, name, sizeof(name));
        double vol = 1.0, pan = 0.0;
        if (auto* v = static_cast<double*>(GetSetMediaTrackInfo(t, "D_VOL", nullptr))) vol = *v;
        if (auto* p = static_cast<double*>(GetSetMediaTrackInfo(t, "D_PAN", nullptr))) pan = *p;
        bool muted = false;
        if (auto* m = static_cast<bool*>(GetSetMediaTrackInfo(t, "B_MUTE", nullptr))) muted = *m;
        tracks.push_back({
            {"index", i}, {"name", name}, {"muted", muted},
            {"volume_db", vol_to_db(vol)}, {"pan", pan}
        });
    }
    return {
        {"transport", {
            {"playing",   (play_raw & 1) != 0},
            {"recording", (play_raw & 4) != 0},
            {"paused",    (play_raw & 2) != 0}
        }},
        {"tracks", tracks}
    };
}

}  // namespace

// POST /execute/action
void handle_execute_action(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try { body = nlohmann::json::parse(req.body); } catch (...) {
        json_error(res, 400, "Invalid JSON body", "BAD_REQUEST");
        return;
    }
    if (!body.contains("id")) {
        json_error(res, 400, "Missing required field: id", "BAD_REQUEST");
        return;
    }

    bool        want_feedback = body.value("feedback", false);
    std::string ag            = agent_id(req);
    std::string id_str        = body["id"].is_number()
                                ? std::to_string(body["id"].get<int>())
                                : body["id"].get<std::string>();

    auto result = Executor::post([body]() -> nlohmann::json {
        int cmd_id = resolve_cmd_id(body["id"]);
        if (cmd_id == 0) return {{"_not_found", true}};
        Main_OnCommand(cmd_id, 0);
        return {{"ok", true}};
    });

    if (result.contains("_timeout")) {
        log_history("action", id_str, ag, "timeout");
        json_error(res, 408, "Main thread timeout", "TIMEOUT");
        return;
    }
    if (result.contains("_error")) {
        std::string err = result["_error"].get<std::string>();
        log_history("action", id_str, ag, "failed", err);
        json_error(res, 500, err, "INTERNAL_ERROR");
        return;
    }
    if (result.contains("_not_found")) {
        json_error(res, 404, "Action ID could not be resolved", "NOT_FOUND");
        return;
    }

    log_history("action", id_str, ag, "success");

    // Track execution stats for registered scripts (string IDs)
    if (body["id"].is_string()) {
        g_db.query(
            "UPDATE scripts SET execution_count = execution_count + 1, "
            "last_executed = datetime('now') WHERE id = ?1",
            {id_str});
    }

    nlohmann::json resp = {
        {"status",      "success"},
        {"action_id",   body["id"]},
        {"executed_at", now_iso()}
    };
    if (want_feedback) resp["feedback"] = build_feedback();
    json_ok(res, resp);
}

// POST /execute/sequence
void handle_execute_sequence(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try { body = nlohmann::json::parse(req.body); } catch (...) {
        json_error(res, 400, "Invalid JSON body", "BAD_REQUEST");
        return;
    }
    if (!body.contains("steps") || !body["steps"].is_array()) {
        json_error(res, 400, "Missing or invalid field: steps", "BAD_REQUEST");
        return;
    }

    bool        feedback_between = body.value("feedback_between_steps", false);
    bool        stop_on_failure  = body.value("stop_on_failure", true);
    std::string ag               = agent_id(req);
    auto&       steps            = body["steps"];
    int         n_steps          = static_cast<int>(steps.size());

    if (n_steps > 100) {
        json_error(res, 400, "Sequences are limited to 100 steps", "BAD_REQUEST");
        return;
    }

    nlohmann::json step_results   = nlohmann::json::array();
    int            steps_completed = 0;
    bool           had_failure     = false;

    for (int i = 0; i < n_steps; i++) {
        auto& step    = steps[i];
        std::string label   = step.value("label", "step_" + std::to_string(i));
        std::string step_id = step.contains("id") ?
            (step["id"].is_number() ? std::to_string(step["id"].get<int>())
                                    : step["id"].get<std::string>())
            : "";

        if (had_failure && stop_on_failure) {
            step_results.push_back({{"label", label}, {"status", "skipped"}});
            continue;
        }

        auto step_result = Executor::post([step]() -> nlohmann::json {
            if (!step.contains("id")) return {{"_bad_request", true}};
            int cmd_id = resolve_cmd_id(step["id"]);
            if (cmd_id == 0) return {{"_not_found", true}};
            Main_OnCommand(cmd_id, 0);
            return {{"ok", true}};
        });

        nlohmann::json entry = {{"label", label}, {"action_id", step.value("id", 0)}};

        if (step_result.contains("_timeout")) {
            entry["status"] = "timeout";
            had_failure = true;
            log_history("sequence", step_id, ag, "timeout");
        } else if (step_result.contains("_not_found") || step_result.contains("_bad_request")) {
            entry["status"] = "failed";
            entry["error"]  = "action not found or invalid id";
            had_failure = true;
            log_history("sequence", step_id, ag, "failed", "not found");
        } else if (step_result.contains("_error")) {
            std::string err = step_result["_error"].get<std::string>();
            entry["status"] = "failed";
            entry["error"]  = err;
            had_failure = true;
            log_history("sequence", step_id, ag, "failed", err);
        } else {
            entry["status"] = "success";
            steps_completed++;
            log_history("sequence", step_id, ag, "success");
        }

        if (feedback_between) entry["feedback"] = build_feedback();
        step_results.push_back(entry);
    }

    json_ok(res, {
        {"status",          had_failure ? "failed" : "success"},
        {"steps_completed", steps_completed},
        {"steps",           step_results}
    });
}

}  // namespace ReaClaw::Handlers

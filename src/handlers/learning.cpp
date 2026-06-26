#include "handlers/learning.h"

#include "app.h"
#include "handlers/common.h"

#include <httplib.h>

#include <string>

#include <json.hpp>

namespace ReaClaw::Learning {

namespace {

// Writable track fields worth learning transitions over (mirrors the track
// writable_fields surface). Keeps note_track_fields from logging noise.
const char* kTrackFields[] = {"name",
                              "color",
                              "folder_depth",
                              "volume_db",
                              "pan",
                              "muted",
                              "soloed",
                              "armed",
                              "phase",
                              "n_channels",
                              "pan_mode",
                              "rec_input",
                              "midi_hw_out",
                              "main_send"};

}  // namespace

bool enabled() {
    return g_config.learning_enabled;
}

void note(const std::string& agent, const std::string& action) {
    if (!enabled() || action.empty())
        return;

    // The previous edit by this agent, and how long ago (seconds). Read before
    // inserting the new event so it's the actual predecessor.
    auto prev = g_db.query(
            "SELECT action, (julianday('now') - julianday(ts)) * 86400.0 AS age_s "
            "FROM learn_events WHERE agent_id = ?1 ORDER BY id DESC LIMIT 1",
            {agent});

    g_db.query("INSERT INTO learn_events (agent_id, action) VALUES (?1, ?2)", {agent, action});

    if (!prev.empty() && prev[0].count("action")) {
        double age = 1e9;
        try {
            age = std::stod(prev[0].at("age_s"));
        } catch (...) {
        }
        const std::string& ante = prev[0].at("action");
        if (age <= g_config.learning_window_seconds && !ante.empty()) {
            g_db.query(
                    "INSERT INTO learn_pairs (antecedent, consequent, n) VALUES (?1, ?2, 1) "
                    "ON CONFLICT(antecedent, consequent) DO UPDATE SET n = n + 1",
                    {ante, action});
        }
    }
}

void note_track_fields(const std::string& agent, const nlohmann::json& body) {
    if (!enabled() || !body.is_object())
        return;
    for (const char* f : kTrackFields) {
        if (body.contains(f))
            note(agent, std::string("track.set:") + f);
    }
}

nlohmann::json suggestions(const std::string& agent, const std::string& after, int limit) {
    nlohmann::json out = nlohmann::json::array();
    if (!enabled())
        return out;

    std::string ante = after;
    if (ante.empty()) {
        auto last = g_db.query(
                "SELECT action FROM learn_events WHERE agent_id = ?1 ORDER BY id DESC LIMIT 1",
                {agent});
        if (!last.empty() && last[0].count("action"))
            ante = last[0].at("action");
    }
    if (ante.empty())
        return out;

    int64_t total = g_db.scalar_int(
            "SELECT COALESCE(SUM(n), 0) FROM learn_pairs WHERE antecedent = ?1", {ante});
    if (total <= 0)
        return out;

    auto rows = g_db.query(
            "SELECT consequent, n FROM learn_pairs WHERE antecedent = ?1 ORDER BY n DESC LIMIT ?2",
            {ante, std::to_string(limit > 0 ? limit : 5)});
    for (auto& r : rows) {
        int64_t n = 0;
        try {
            n = std::stoll(r.at("n"));
        } catch (...) {
        }
        double conf = static_cast<double>(n) / static_cast<double>(total);
        if (n < g_config.learning_min_support || conf < g_config.learning_min_confidence)
            continue;
        out.push_back({{"after", ante},
                       {"suggest", r.at("consequent")},
                       {"support", n},
                       {"confidence", conf},
                       {"method", "learned"}});
    }
    return out;
}

nlohmann::json stats() {
    return {{"enabled", enabled()},
            {"events", g_db.scalar_int("SELECT COUNT(*) FROM learn_events")},
            {"patterns", g_db.scalar_int("SELECT COUNT(*) FROM learn_pairs")},
            {"agents", g_db.scalar_int("SELECT COUNT(DISTINCT agent_id) FROM learn_events")},
            {"window_seconds", g_config.learning_window_seconds},
            {"min_support", g_config.learning_min_support},
            {"min_confidence", g_config.learning_min_confidence}};
}

// GET /suggestions?after=&agent=&limit=
void handle_suggestions(const httplib::Request& req, httplib::Response& res) {
    if (!enabled()) {
        Handlers::json_ok(res,
                          {{"enabled", false},
                           {"suggestions", nlohmann::json::array()},
                           {"note",
                            "learning is opt-in and off by default — set learning.enabled=true in "
                            "config.json to mine local edit history. Nothing is recorded or sent "
                            "while disabled."}});
        return;
    }
    std::string agent = req.has_param("agent") ? req.get_param_value("agent")
                                               : Handlers::agent_id(req);
    std::string after = req.has_param("after") ? req.get_param_value("after") : "";
    int limit = 5;
    if (req.has_param("limit")) {
        try {
            limit = std::stoi(req.get_param_value("limit"));
        } catch (...) {
        }
    }
    nlohmann::json sugg = suggestions(agent, after, limit);
    Handlers::json_ok(res,
                      {{"enabled", true},
                       {"agent", agent},
                       {"after", after},
                       {"suggestions", sugg},
                       {"note",
                        "learned from this machine's local edit history only; tagged "
                        "method:learned with a confidence. Advisory, not automatic."}});
}

// GET /learn/stats
void handle_learn_stats(const httplib::Request& req, httplib::Response& res) {
    (void)req;
    Handlers::json_ok(res, stats());
}

}  // namespace ReaClaw::Learning

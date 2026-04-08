#include "handlers/history.h"

#include "app.h"
#include "handlers/common.h"

#include <httplib.h>

#include <string>

#include <json.hpp>

namespace ReaClaw::Handlers {

// GET /history[?limit=N&offset=N&agent_id=...]
void handle_history(const httplib::Request& req, httplib::Response& res) {
    int limit = 50;
    int offset = 0;
    if (req.has_param("limit"))
        try {
            limit = std::stoi(req.get_param_value("limit"));
        } catch (...) {
        }
    if (req.has_param("offset"))
        try {
            offset = std::stoi(req.get_param_value("offset"));
        } catch (...) {
        }
    if (limit < 1)
        limit = 1;
    if (limit > 500)
        limit = 500;
    if (offset < 0)
        offset = 0;

    std::string filter_agent = req.has_param("agent_id") ? req.get_param_value("agent_id") : "";

    Rows rows;
    int64_t total = 0;

    if (filter_agent.empty()) {
        total = g_db.scalar_int("SELECT COUNT(*) FROM execution_history");
        rows = g_db.query(
                "SELECT id, type, target_id, agent_id, status, error, executed_at "
                "FROM execution_history ORDER BY executed_at DESC LIMIT ?1 OFFSET ?2",
                {std::to_string(limit), std::to_string(offset)});
    } else {
        total = g_db.scalar_int("SELECT COUNT(*) FROM execution_history WHERE agent_id = ?1",
                                {filter_agent});
        rows = g_db.query(
                "SELECT id, type, target_id, agent_id, status, error, executed_at "
                "FROM execution_history WHERE agent_id = ?1 "
                "ORDER BY executed_at DESC LIMIT ?2 OFFSET ?3",
                {filter_agent, std::to_string(limit), std::to_string(offset)});
    }

    nlohmann::json executions = nlohmann::json::array();
    for (auto& r : rows) {
        int row_id = 0;
        try {
            row_id = std::stoi(r.at("id"));
        } catch (...) {
        }
        nlohmann::json entry = {{"id", row_id},
                                {"type", r.count("type") ? r.at("type") : ""},
                                {"target_id", r.count("target_id") ? r.at("target_id") : ""},
                                {"agent_id", r.count("agent_id") ? r.at("agent_id") : nullptr},
                                {"status", r.count("status") ? r.at("status") : ""},
                                {"executed_at", r.count("executed_at") ? r.at("executed_at") : ""}};
        if (r.count("error") && !r.at("error").empty()) {
            entry["error"] = r.at("error");
        }
        executions.push_back(entry);
    }

    json_ok(res,
            {{"total", total}, {"offset", offset}, {"limit", limit}, {"executions", executions}});
}

}  // namespace ReaClaw::Handlers

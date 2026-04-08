#include "handlers/catalog.h"

#include "app.h"
#include "handlers/common.h"

#include <httplib.h>

#include <string>

#include <json.hpp>

namespace ReaClaw::Handlers {

namespace {

nlohmann::json action_row_to_json(const Row& row) {
    int id = 0;
    try {
        id = std::stoi(row.at("id"));
    } catch (...) {
    }
    return {{"id", id},
            {"name", row.count("name") ? row.at("name") : ""},
            {"category", row.count("category") ? row.at("category") : ""},
            {"section", row.count("section") ? row.at("section") : "main"}};
}

}  // namespace

// GET /catalog[?limit=N&offset=N]
void handle_catalog_list(const httplib::Request& req, httplib::Response& res) {
    int limit = 100;
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
    if (limit > 1000)
        limit = 1000;
    if (offset < 0)
        offset = 0;

    int64_t total = g_db.scalar_int("SELECT COUNT(*) FROM actions");

    auto rows = g_db.query(
            "SELECT id, name, category, section FROM actions "
            "ORDER BY id LIMIT ?1 OFFSET ?2",
            {std::to_string(limit), std::to_string(offset)});

    nlohmann::json actions = nlohmann::json::array();
    for (auto& r : rows)
        actions.push_back(action_row_to_json(r));

    json_ok(res, {{"total", total}, {"offset", offset}, {"limit", limit}, {"actions", actions}});
}

// GET /catalog/search?q=...&limit=N&category=...
void handle_catalog_search(const httplib::Request& req, httplib::Response& res) {
    if (!req.has_param("q") || req.get_param_value("q").empty()) {
        json_error(res, 400, "Missing required query param: q", "BAD_REQUEST");
        return;
    }

    std::string q = req.get_param_value("q");
    std::string category = req.has_param("category") ? req.get_param_value("category") : "";
    int limit = 20;
    if (req.has_param("limit"))
        try {
            limit = std::stoi(req.get_param_value("limit"));
        } catch (...) {
        }
    if (limit < 1)
        limit = 1;
    if (limit > 200)
        limit = 200;

    // FTS5 via actions_fts; join back to actions for full row data
    Rows rows;
    if (category.empty()) {
        rows = g_db.query(
                "SELECT a.id, a.name, a.category, a.section "
                "FROM actions_fts f JOIN actions a ON f.rowid = a.id "
                "WHERE actions_fts MATCH ?1 ORDER BY rank LIMIT ?2",
                {q, std::to_string(limit)});
    } else {
        rows = g_db.query(
                "SELECT a.id, a.name, a.category, a.section "
                "FROM actions_fts f JOIN actions a ON f.rowid = a.id "
                "WHERE actions_fts MATCH ?1 AND a.category = ?2 "
                "ORDER BY rank LIMIT ?3",
                {q, category, std::to_string(limit)});
    }

    nlohmann::json actions = nlohmann::json::array();
    for (auto& r : rows)
        actions.push_back(action_row_to_json(r));

    json_ok(res, {{"query", q}, {"total", (int)actions.size()}, {"actions", actions}});
}

// GET /catalog/:id
void handle_catalog_by_id(const httplib::Request& req, httplib::Response& res) {
    const std::string& id_str = req.path_params.at("id");
    int action_id = 0;
    try {
        action_id = std::stoi(id_str);
    } catch (...) {
        json_error(res, 400, "Action ID must be a numeric integer", "BAD_REQUEST");
        return;
    }

    auto rows = g_db.query_i("SELECT id, name, category, section FROM actions WHERE id = ?1",
                             {action_id});

    if (rows.empty()) {
        json_error(res, 404, "Action not found", "NOT_FOUND");
        return;
    }

    json_ok(res, action_row_to_json(rows[0]));
}

// GET /catalog/categories
void handle_catalog_categories(const httplib::Request& req, httplib::Response& res) {
    (void)req;
    auto rows = g_db.query(
            "SELECT category, COUNT(*) AS cnt FROM actions "
            "GROUP BY category ORDER BY cnt DESC");

    nlohmann::json cats = nlohmann::json::array();
    for (auto& r : rows) {
        int cnt = 0;
        try {
            cnt = std::stoi(r.at("cnt"));
        } catch (...) {
        }
        cats.push_back({{"name", r.at("category")}, {"count", cnt}});
    }

    json_ok(res, {{"categories", cats}});
}

}  // namespace ReaClaw::Handlers

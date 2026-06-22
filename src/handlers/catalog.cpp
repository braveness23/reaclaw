#include "handlers/catalog.h"

#include "app.h"
#include "handlers/common.h"

#include <httplib.h>

#include <cctype>
#include <string>

#include <json.hpp>

namespace ReaClaw::Handlers {

namespace {

// Known modal native actions whose names lack the usual ellipsis tell but which
// still pop a blocking dialog (so they'd hang a headless agent). Mirrors the
// ReaClaw Skill's "DON'T" list; extend as exceptions surface.
bool is_known_modal_id(int id) {
    switch (id) {
        case 40696:  // Track: Rename last touched track
            return true;
        default:
            return false;
    }
}

// Does this action open a modal dialog (and so hang a headless agent)? REAPER
// convention is that dialog-opening actions end with an ellipsis; "prompt"/
// "dialog" in the name are further tells, plus a small curated ID set for the
// exceptions the name heuristic misses. Lets agents filter modal actions out.
bool action_is_interactive(const std::string& name, int id) {
    if (is_known_modal_id(id))
        return true;
    if (name.find("...") != std::string::npos)
        return true;
    if (name.find("\xE2\x80\xA6") != std::string::npos)  // UTF-8 ellipsis "…"
        return true;
    std::string lower;
    lower.reserve(name.size());
    for (char c : name)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return lower.find("prompt") != std::string::npos || lower.find("dialog") != std::string::npos;
}

nlohmann::json action_row_to_json(const Row& row, const char* section = "main") {
    int id = 0;
    try {
        id = std::stoi(row.at("id"));
    } catch (...) {
    }
    std::string name = row.count("name") ? row.at("name") : "";
    return {{"id", id},
            {"name", name},
            {"category", row.count("category") ? row.at("category") : ""},
            {"section", row.count("section") ? row.at("section") : section},
            {"interactive", action_is_interactive(name, id)}};
}

// Resolve the ?section= query param to the backing table + the section label
// reported in responses. Defaults to the main section.
struct SectionTables {
    const char* table;
    const char* fts;
    const char* label;
};
SectionTables section_tables(const httplib::Request& req) {
    if (req.has_param("section") && req.get_param_value("section") == "midi_editor")
        return {"actions_midi", "actions_midi_fts", "midi_editor"};
    return {"actions", "actions_fts", "main"};
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

    SectionTables st = section_tables(req);

    int64_t total = g_db.scalar_int(std::string("SELECT COUNT(*) FROM ") + st.table);

    auto rows = g_db.query(std::string("SELECT id, name, category FROM ") + st.table +
                                   " ORDER BY id LIMIT ?1 OFFSET ?2",
                           {std::to_string(limit), std::to_string(offset)});

    nlohmann::json actions = nlohmann::json::array();
    for (auto& r : rows)
        actions.push_back(action_row_to_json(r, st.label));

    json_ok(res,
            {{"total", total},
             {"offset", offset},
             {"limit", limit},
             {"section", st.label},
             {"actions", actions}});
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

    SectionTables st = section_tables(req);

    // FTS5 via the section's *_fts table; join back to its actions table.
    Rows rows;
    if (category.empty()) {
        rows = g_db.query(std::string("SELECT a.id, a.name, a.category FROM ") + st.fts +
                                  " f JOIN " + st.table + " a ON f.rowid = a.id WHERE " + st.fts +
                                  " MATCH ?1 ORDER BY rank LIMIT ?2",
                          {q, std::to_string(limit)});
    } else {
        rows = g_db.query(std::string("SELECT a.id, a.name, a.category FROM ") + st.fts +
                                  " f JOIN " + st.table + " a ON f.rowid = a.id WHERE " + st.fts +
                                  " MATCH ?1 AND a.category = ?2 ORDER BY rank LIMIT ?3",
                          {q, category, std::to_string(limit)});
    }

    nlohmann::json actions = nlohmann::json::array();
    for (auto& r : rows)
        actions.push_back(action_row_to_json(r, st.label));

    json_ok(res,
            {{"query", q},
             {"section", st.label},
             {"total", (int)actions.size()},
             {"actions", actions}});
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

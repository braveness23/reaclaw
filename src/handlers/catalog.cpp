#include "handlers/catalog.h"

#include "app.h"
#include "handlers/common.h"

#include <httplib.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include <json.hpp>

namespace ReaClaw::Handlers {

namespace {

// Curated synonym groups: any token in a group expands (OR) to all members, so
// natural-language phrasing finds REAPER's vocabulary (e.g. "folder depth" ->
// "indent"). Used only as a fallback when the literal query misses, so precise
// queries keep their precision. Extend as gaps surface.
const std::vector<std::vector<std::string>>& synonym_groups() {
    static const std::vector<std::vector<std::string>> g = {
            {"folder", "indent", "depth", "nest", "unindent"},
            {"arm", "recarm", "record", "rec"},
            {"color", "colour", "tint"},
            {"mute", "silence"},
            {"send", "route", "routing", "bus", "receive"},
            {"fx", "plugin", "effect", "vst", "instrument"},
            {"tempo", "bpm"},
            {"marker", "region"},
            {"volume", "gain", "level", "fader"},
            {"pan", "balance"},
            {"automation", "envelope"},
            {"midi", "note"},
            {"render", "bounce", "export"},
            {"duplicate", "copy"},
            {"delete", "remove"},
            {"rename", "name", "label", "title"},
            {"normalize", "normalise"},
            {"fade", "crossfade"},
            {"quantize", "quantise", "snap"},
            {"zoom", "scroll"},
    };
    return g;
}

// Split into lowercase alphanumeric tokens (also sanitizes for safe FTS5 syntax).
std::vector<std::string> tokenize_lower(const std::string& q) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : q) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            cur.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else if (!cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

std::vector<std::string> expand_token(const std::string& t) {
    for (const auto& grp : synonym_groups())
        if (std::find(grp.begin(), grp.end(), t) != grp.end())
            return grp;
    return {t};
}

// AND of per-token OR-groups: `set (folder OR indent OR depth)` — keeps every
// concept the user typed, broadening each to its synonyms.
std::string build_fts_and(const std::vector<std::string>& toks) {
    std::string out;
    for (const auto& t : toks) {
        auto ex = expand_token(t);
        std::string grp;
        if (ex.size() == 1) {
            grp = ex[0];
        } else {
            grp = "(";
            for (size_t i = 0; i < ex.size(); ++i)
                grp += (i ? " OR " : "") + ex[i];
            grp += ")";
        }
        out += (out.empty() ? "" : " ") + grp;
    }
    return out;
}

// OR over every expanded term — widest recall fallback.
std::string build_fts_or(const std::vector<std::string>& toks) {
    std::vector<std::string> all;
    for (const auto& t : toks)
        for (const auto& e : expand_token(t))
            all.push_back(e);
    std::string out;
    for (size_t i = 0; i < all.size(); ++i)
        out += (i ? " OR " : "") + all[i];
    return out;
}

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

    // Run one FTS5 MATCH against the section's *_fts table, joined to its actions
    // table. `fts` is the raw MATCH expression (caller builds it safely).
    auto run = [&](const std::string& fts) -> Rows {
        if (category.empty()) {
            return g_db.query(std::string("SELECT a.id, a.name, a.category FROM ") + st.fts +
                                      " f JOIN " + st.table + " a ON f.rowid = a.id WHERE " +
                                      st.fts + " MATCH ?1 ORDER BY rank LIMIT ?2",
                              {fts, std::to_string(limit)});
        }
        return g_db.query(std::string("SELECT a.id, a.name, a.category FROM ") + st.fts +
                                  " f JOIN " + st.table + " a ON f.rowid = a.id WHERE " + st.fts +
                                  " MATCH ?1 AND a.category = ?2 ORDER BY rank LIMIT ?3",
                          {fts, category, std::to_string(limit)});
    };

    // Strict first (preserves precision for queries that already work), then
    // widen via synonyms only on a miss, then OR-of-all as a last resort.
    std::string used = q;
    bool expanded = false;
    Rows rows = run(q);
    if (rows.empty()) {
        auto toks = tokenize_lower(q);
        if (!toks.empty()) {
            std::string fts_and = build_fts_and(toks);
            if (fts_and != q) {
                rows = run(fts_and);
                used = fts_and;
                expanded = true;
            }
            if (rows.empty()) {
                std::string fts_or = build_fts_or(toks);
                if (fts_or != fts_and) {
                    rows = run(fts_or);
                    used = fts_or;
                    expanded = true;
                }
            }
        }
    }

    nlohmann::json actions = nlohmann::json::array();
    for (auto& r : rows)
        actions.push_back(action_row_to_json(r, st.label));

    json_ok(res,
            {{"query", q},
             {"matched", used},
             {"expanded", expanded},
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

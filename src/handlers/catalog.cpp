#include "handlers/catalog.h"

#include "app.h"
#include "handlers/common.h"

#include <httplib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <utility>
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

std::string to_lower(const std::string& s) {
    std::string lower;
    lower.reserve(s.size());
    for (char c : s)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return lower;
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
    std::string lower = to_lower(name);
    return lower.find("prompt") != std::string::npos || lower.find("dialog") != std::string::npos;
}

// Issue #10 — does this action need an existing track/item selection to do
// anything meaningful? REAPER's own naming convention is the tell: actions
// that operate on selection consistently say "selected" in the name (e.g.
// "Track: Mute selected tracks"). A name-based heuristic, not a guarantee.
bool action_requires_selection(const std::string& name) {
    return to_lower(name).find("selected") != std::string::npos;
}

// Issue #10 — does this action change project state (vs. a UI/session-only
// toggle)? Coarse, deliberately conservative heuristic: REAPER's own "View:"
// and "Options:" category/name prefixes are display/preference toggles that
// don't touch the .rpp data; everything else defaults to true (mutating) —
// the safer assumption for an agent deciding whether an edit needs care.
bool action_mutates_state(const std::string& category, const std::string& name) {
    if (category == "View" || category == "Options")
        return false;
    if (name.rfind("View:", 0) == 0 || name.rfind("Options:", 0) == 0)
        return false;
    return true;
}

nlohmann::json action_row_to_json(const Row& row, const char* section = "main") {
    int id = 0;
    try {
        id = std::stoi(row.at("id"));
    } catch (...) {
    }
    std::string name = row.count("name") ? row.at("name") : "";
    std::string category = row.count("category") ? row.at("category") : "";
    nlohmann::json j = {{"id", id},
                        {"name", name},
                        {"category", category},
                        {"section", row.count("section") ? row.at("section") : section},
                        {"interactive", action_is_interactive(name, id)},
                        {"mutates_state", action_mutates_state(category, name)},
                        {"requires_selection", action_requires_selection(name)}};
    if (row.count("score")) {
        try {
            j["score"] = std::stod(row.at("score"));
        } catch (...) {
        }
    }
    return j;
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

// ---------------------------------------------------------------------------
// Issue #10 — server-side semantic catalog search. Opt-in (config.semantic_
// search_enabled, off by default) and request-gated (?semantic=true). See
// ReaClaw_TECH_DECISIONS.md §25 for why this is a narrow, deliberate carve-out
// of §11's no-LLM-client stance — an embedding model, not a generative one,
// and the same call this project's MCP client already makes, just server-side
// so plain REST/MCP-less callers get it too.
// ---------------------------------------------------------------------------

// Loopback-only safety rail: refuse to call anywhere but localhost, so a
// misconfigured ollama_url can't turn an explicit opt-in into real network
// egress. Deliberately simple string check, not DNS resolution — an allowlist
// of the handful of ways "localhost" gets spelled.
bool is_loopback_url(const std::string& url) {
    auto scheme_end = url.find("://");
    size_t start = (scheme_end == std::string::npos) ? 0 : scheme_end + 3;
    size_t end = url.find_first_of(":/", start);
    std::string host = url.substr(start,
                                  end == std::string::npos ? std::string::npos : end - start);
    return host == "127.0.0.1" || host == "localhost" || host == "::1" || host == "[::1]";
}

// POST {ollama_url}/api/embed {model, input:[...]} -> L2-normalized vectors
// (so a plain dot product is cosine similarity). Empty return on any failure
// (not loopback, unreachable, non-200, malformed body) — callers fall back to
// keyword search rather than erroring the whole request.
std::vector<std::vector<float>> ollama_embed(const std::string& ollama_url,
                                             const std::string& model,
                                             const std::vector<std::string>& texts) {
    if (!is_loopback_url(ollama_url) || texts.empty())
        return {};
    httplib::Client cli(ollama_url);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(30, 0);
    nlohmann::json body = {{"model", model}, {"input", texts}};
    auto res = cli.Post("/api/embed", body.dump(), "application/json");
    if (!res || res->status != 200)
        return {};
    nlohmann::json parsed = nlohmann::json::parse(res->body, nullptr, false);
    if (!parsed.is_object() || !parsed.contains("embeddings") || !parsed["embeddings"].is_array())
        return {};

    std::vector<std::vector<float>> out;
    out.reserve(parsed["embeddings"].size());
    for (auto& emb : parsed["embeddings"]) {
        if (!emb.is_array())
            return {};
        std::vector<float> v;
        v.reserve(emb.size());
        double norm_sq = 0.0;
        for (auto& x : emb) {
            float f = x.get<float>();
            v.push_back(f);
            norm_sq += double(f) * double(f);
        }
        double norm = std::sqrt(norm_sq);
        if (norm > 0.0)
            for (auto& f : v)
                f = static_cast<float>(f / norm);
        out.push_back(std::move(v));
    }
    return out;
}

std::string vector_to_text(const std::vector<float>& v) {
    std::string s;
    s.reserve(v.size() * 12);
    for (size_t i = 0; i < v.size(); i++) {
        if (i)
            s += ',';
        s += std::to_string(v[i]);
    }
    return s;
}

std::vector<float> text_to_vector(const std::string& s) {
    std::vector<float> v;
    size_t start = 0;
    while (start <= s.size()) {
        size_t comma = s.find(',', start);
        std::string tok = s.substr(start,
                                   comma == std::string::npos ? std::string::npos : comma - start);
        if (!tok.empty()) {
            try {
                v.push_back(std::stof(tok));
            } catch (...) {
                v.push_back(0.0f);
            }
        }
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return v;
}

// Ensure the embedding cache for this section covers the current catalog at
// the given model, (re)building it (embedding every action name, in batches)
// if the catalog size or model changed since it was last built. Returns false
// if Ollama is unreachable partway through — caller falls back to keyword.
bool ensure_embeddings_cached(const SectionTables& st,
                              const std::string& ollama_url,
                              const std::string& model) {
    std::string sig_key = std::string("action_embeddings_sig_") + st.table;
    int64_t catalog_count = g_db.scalar_int(std::string("SELECT COUNT(*) FROM ") + st.table);
    std::string sig = std::to_string(catalog_count) + "-" + model;
    std::string stored_sig = g_db.scalar_text("SELECT value FROM meta WHERE key = ?1", {sig_key});
    int64_t cached_count = g_db.scalar_int(
            "SELECT COUNT(*) FROM action_embeddings WHERE section = ?1 AND model = ?2",
            {st.label, model});
    if (stored_sig == sig && cached_count == catalog_count)
        return true;  // already built and matches the current catalog

    auto rows = g_db.query(std::string("SELECT id, name FROM ") + st.table + " ORDER BY id");
    if (rows.empty())
        return true;  // nothing to embed

    g_db.query("DELETE FROM action_embeddings WHERE section = ?1 AND model = ?2",
               {st.label, model});

    constexpr size_t kBatch = 64;
    for (size_t i = 0; i < rows.size(); i += kBatch) {
        std::vector<std::string> names;
        std::vector<std::string> ids;
        for (size_t j = i; j < std::min(i + kBatch, rows.size()); j++) {
            names.push_back(rows[j].at("name"));
            ids.push_back(rows[j].at("id"));
        }
        auto embs = ollama_embed(ollama_url, model, names);
        if (embs.size() != names.size())
            return false;  // embedding failed partway; caller falls back
        for (size_t j = 0; j < embs.size(); j++) {
            g_db.query(
                    "INSERT OR REPLACE INTO action_embeddings(id, section, model, vector) "
                    "VALUES(?1, ?2, ?3, ?4)",
                    {ids[j], st.label, model, vector_to_text(embs[j])});
        }
    }
    g_db.query("INSERT OR REPLACE INTO meta(key, value) VALUES(?1, ?2)", {sig_key, sig});
    return true;
}

// Rank the cached catalog embeddings against the query, top-N, joined back to
// name/category. Empty result signals "couldn't do semantic search" (Ollama
// down, cache build failed) — the caller falls back to keyword search, not
// an error, since semantic search degrading gracefully is the whole point.
Rows semantic_catalog_search(const SectionTables& st,
                             const std::string& query,
                             int limit,
                             const std::string& category,
                             const std::string& ollama_url,
                             const std::string& model) {
    if (!ensure_embeddings_cached(st, ollama_url, model))
        return {};

    auto qvecs = ollama_embed(ollama_url, model, {query});
    if (qvecs.empty())
        return {};
    const auto& qv = qvecs[0];

    auto rows = g_db.query(
            "SELECT id, vector FROM action_embeddings WHERE section = ?1 AND model = ?2",
            {st.label, model});
    std::vector<std::pair<double, int64_t>> scored;
    scored.reserve(rows.size());
    for (auto& r : rows) {
        auto v = text_to_vector(r.at("vector"));
        double dot = 0.0;
        size_t n = std::min(v.size(), qv.size());
        for (size_t i = 0; i < n; i++)
            dot += double(v[i]) * double(qv[i]);
        int64_t id = 0;
        try {
            id = std::stoll(r.at("id"));
        } catch (...) {
        }
        scored.push_back({dot, id});
    }
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });

    Rows out;
    for (auto& [score, id] : scored) {
        if (static_cast<int>(out.size()) >= limit)
            break;
        Rows arows = category.empty()
                             ? g_db.query_i(std::string("SELECT id, name, category FROM ") +
                                                    st.table + " WHERE id = ?1",
                                            {id})
                             : g_db.query(std::string("SELECT id, name, category FROM ") +
                                                  st.table + " WHERE id = ?1 AND category = ?2",
                                          {std::to_string(id), category});
        if (arows.empty())
            continue;
        Row row = arows[0];
        row["score"] = std::to_string(score);
        out.push_back(std::move(row));
    }
    return out;
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

    // Issue #10 — semantic search: opt-in twice over (config.semantic_search.
    // enabled AND this request's ?semantic=true), and gracefully degrades to
    // the keyword path below on any failure (Ollama down, cache build failed,
    // not loopback) rather than erroring the request.
    bool want_semantic = req.has_param("semantic") && req.get_param_value("semantic") == "true";
    if (want_semantic && g_config.semantic_search_enabled) {
        Rows srows = semantic_catalog_search(st,
                                             q,
                                             limit,
                                             category,
                                             g_config.semantic_search_ollama_url,
                                             g_config.semantic_search_model);
        if (!srows.empty()) {
            nlohmann::json sactions = nlohmann::json::array();
            for (auto& r : srows)
                sactions.push_back(action_row_to_json(r, st.label));
            json_ok(res,
                    {{"query", q},
                     {"mode", "semantic"},
                     {"section", st.label},
                     {"total", (int)sactions.size()},
                     {"actions", sactions}});
            return;
        }
        // Fall through to keyword search on any semantic-path failure.
    }

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
             {"mode", "keyword"},
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

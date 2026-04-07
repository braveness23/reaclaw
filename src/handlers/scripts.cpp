#include "handlers/scripts.h"
#include "handlers/common.h"
#include "reaper/scripts.h"
#include "app.h"

#include <httplib.h>
#include <json.hpp>

#include <string>
#include <vector>

namespace ReaClaw::Handlers {

// ---------------------------------------------------------------------------
// POST /scripts/register
// ---------------------------------------------------------------------------
void handle_scripts_register(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try { body = nlohmann::json::parse(req.body); } catch (...) {
        json_error(res, 400, "Invalid JSON body", "BAD_REQUEST");
        return;
    }

    if (!body.contains("name") || !body["name"].is_string() ||
        !body.contains("script") || !body["script"].is_string()) {
        json_error(res, 400, "Missing required fields: name, script", "BAD_REQUEST");
        return;
    }

    std::string name   = body["name"].get<std::string>();
    std::string script = body["script"].get<std::string>();

    if (name.empty()) {
        json_error(res, 400, "Field 'name' must not be empty", "BAD_REQUEST");
        return;
    }

    // Script body size limit
    int max_bytes = g_config.max_script_size_kb * 1024;
    if (static_cast<int>(script.size()) > max_bytes) {
        json_error(res, 400,
            "Script body exceeds max_script_size_kb (" +
            std::to_string(g_config.max_script_size_kb) + " KB)",
            "BAD_REQUEST");
        return;
    }

    // Parse optional tags array
    std::vector<std::string> tags;
    if (body.contains("tags") && body["tags"].is_array()) {
        for (auto& t : body["tags"]) {
            if (t.is_string()) tags.push_back(t.get<std::string>());
        }
    }

    auto result = Scripts::register_script(name, script, tags);

    // Syntax error from the Lua validator
    if (!result.registered && result.syntax_error_line > 0) {
        json_ok(res, {
            {"registered", false},
            {"syntax_error", {
                {"line",    result.syntax_error_line},
                {"message", result.syntax_error_message}
            }}
        });
        return;
    }

    // Syntax error without a line number (luac returned an error but no line parsed)
    if (!result.registered && !result.syntax_error_message.empty()) {
        json_ok(res, {
            {"registered", false},
            {"syntax_error", {
                {"line",    0},
                {"message", result.syntax_error_message}
            }}
        });
        return;
    }

    // Internal error (file I/O, REAPER rejection, timeout, ...)
    if (!result.registered) {
        json_error(res, 500,
            result.internal_error.empty() ? "Script registration failed"
                                          : result.internal_error,
            "INTERNAL_ERROR");
        return;
    }

    json_ok(res, {
        {"action_id",   result.action_id},
        {"registered",  true},
        {"script_path", result.script_path}
    });
}

// ---------------------------------------------------------------------------
// GET /scripts/cache[?tags=<tag>]
// ---------------------------------------------------------------------------
void handle_scripts_cache(const httplib::Request& req, httplib::Response& res) {
    std::string tag_filter = req.has_param("tags")
        ? req.get_param_value("tags") : "";

    Rows rows;
    if (tag_filter.empty()) {
        rows = g_db.query(
            "SELECT id, name, tags, execution_count, created_at, last_executed "
            "FROM scripts ORDER BY created_at DESC", {});
    } else {
        // Simple substring match against the JSON array column
        std::string like_pattern = "%\"" + tag_filter + "\"%";
        rows = g_db.query(
            "SELECT id, name, tags, execution_count, created_at, last_executed "
            "FROM scripts WHERE tags LIKE ?1 ORDER BY created_at DESC",
            {like_pattern});
    }

    nlohmann::json scripts_arr = nlohmann::json::array();
    for (auto& r : rows) {
        nlohmann::json entry = {
            {"action_id",       r.at("id")},
            {"name",            r.at("name")},
            {"execution_count", 0},
            {"created_at",      r.at("created_at")}
        };

        try { entry["tags"] = nlohmann::json::parse(r.at("tags")); }
        catch (...) { entry["tags"] = nlohmann::json::array(); }

        try { entry["execution_count"] = std::stoi(r.at("execution_count")); }
        catch (...) {}

        const std::string& last = r.at("last_executed");
        if (!last.empty()) entry["last_executed"] = last;

        scripts_arr.push_back(std::move(entry));
    }

    json_ok(res, {{"scripts", scripts_arr}});
}

// ---------------------------------------------------------------------------
// GET /scripts/{id}
// ---------------------------------------------------------------------------
void handle_scripts_get(const httplib::Request& req, httplib::Response& res) {
    auto it = req.path_params.find("id");
    if (it == req.path_params.end()) {
        json_error(res, 400, "Missing script id", "BAD_REQUEST");
        return;
    }
    const std::string& id = it->second;

    auto rows = g_db.query(
        "SELECT id, name, body, script_path, tags, "
        "       execution_count, created_at, last_executed "
        "FROM scripts WHERE id = ?1", {id});

    if (rows.empty()) {
        json_error(res, 404, "Script not found: " + id, "NOT_FOUND");
        return;
    }

    auto& r = rows[0];
    nlohmann::json entry = {
        {"action_id",       r.at("id")},
        {"name",            r.at("name")},
        {"script",          r.at("body")},
        {"script_path",     r.at("script_path")},
        {"execution_count", 0},
        {"created_at",      r.at("created_at")}
    };

    try { entry["tags"] = nlohmann::json::parse(r.at("tags")); }
    catch (...) { entry["tags"] = nlohmann::json::array(); }

    try { entry["execution_count"] = std::stoi(r.at("execution_count")); }
    catch (...) {}

    const std::string& last = r.at("last_executed");
    if (!last.empty()) entry["last_executed"] = last;

    json_ok(res, entry);
}

// ---------------------------------------------------------------------------
// DELETE /scripts/{id}
// ---------------------------------------------------------------------------
void handle_scripts_delete(const httplib::Request& req, httplib::Response& res) {
    auto it = req.path_params.find("id");
    if (it == req.path_params.end()) {
        json_error(res, 400, "Missing script id", "BAD_REQUEST");
        return;
    }
    const std::string& id = it->second;

    if (!Scripts::unregister_script(id)) {
        json_error(res, 404, "Script not found: " + id, "NOT_FOUND");
        return;
    }

    json_ok(res, {{"deleted", true}, {"action_id", id}});
}

}  // namespace ReaClaw::Handlers

// Script registration (AddRemoveReaScript, syntax validation) — Phase 1.
// Implementation follows ReaClaw_IMPLEMENTATION_CHECKLIST.md §Phase 1.

#include "reaper/scripts.h"
#include "reaper/executor.h"
#include "app.h"
#include "util/logging.h"

// REAPER SDK — extern declarations (REAPERAPI_IMPLEMENT only in reaper/api.cpp)
#include <reaper_plugin_functions.h>

#include <openssl/sha.h>
#include <json.hpp>

#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>   // WIFEXITED / WEXITSTATUS
#endif

namespace fs = std::filesystem;

namespace ReaClaw::Scripts {

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Replace non-alphanumeric characters with '_', collapse runs, lowercase,
// truncate to 40 chars. Result is safe for use in filenames and REAPER IDs.
std::string sanitize_name(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (unsigned char c : name) {
        if (std::isalnum(c)) {
            out += static_cast<char>(std::tolower(c));
        } else {
            if (!out.empty() && out.back() != '_') out += '_';
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.size() > 40) out.resize(40);
    if (out.empty()) out = "script";
    return out;
}

// SHA-256 of body → first 8 lowercase hex characters.
std::string sha256_prefix8(const std::string& body) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(body.data()), body.size(), hash);
    char hex[9];
    snprintf(hex, sizeof(hex), "%02x%02x%02x%02x",
             hash[0], hash[1], hash[2], hash[3]);
    return std::string(hex, 8);
}

// Build the canonical REAPER script action ID: _{sanitized_name}_{hash8}
std::string make_action_id(const std::string& name, const std::string& body) {
    return "_" + sanitize_name(name) + "_" + sha256_prefix8(body);
}

// ---------------------------------------------------------------------------
// Lua syntax validation
// ---------------------------------------------------------------------------

struct SyntaxResult {
    bool        ok      = true;
    int         line    = 0;
    std::string message;
};

// Parse "luac: <path>:<digits>: <message>" to extract line + message.
// Falls back to returning the whole output as message with line=0.
SyntaxResult parse_luac_output(const std::string& output) {
    SyntaxResult res{false, 0, ""};

    // Scan for ":<digits>:" pattern — the first one is the line number.
    for (size_t i = 0; i + 2 < output.size(); i++) {
        if (output[i] == ':' && std::isdigit(static_cast<unsigned char>(output[i + 1]))) {
            size_t j = i + 1;
            while (j < output.size() && std::isdigit(static_cast<unsigned char>(output[j]))) j++;
            if (j < output.size() && output[j] == ':') {
                res.line = std::stoi(output.substr(i + 1, j - i - 1));
                size_t msg_start = j + 1;
                // Skip one leading space if present
                if (msg_start < output.size() && output[msg_start] == ' ') msg_start++;
                res.message = output.substr(msg_start);
                // Trim trailing whitespace / newlines
                while (!res.message.empty() &&
                       (res.message.back() == '\n' || res.message.back() == '\r' ||
                        res.message.back() == ' '))
                    res.message.pop_back();
                return res;
            }
        }
    }

    // Fallback: use the whole output as the message
    res.message = output;
    while (!res.message.empty() &&
           (res.message.back() == '\n' || res.message.back() == '\r'))
        res.message.pop_back();
    return res;
}

// Check Lua syntax by shelling out to `luac -p <tempfile>`.
// If luac is not installed, returns {ok=true} (trust the agent).
SyntaxResult check_lua_syntax(const std::string& body) {
    // Unique temp file name (uses time + body-hash prefix to avoid collisions)
    fs::path tmp_dir = fs::temp_directory_path();
    std::string tmp_name = "reaclaw_luacheck_" +
        sha256_prefix8(body + std::to_string(
            std::chrono::system_clock::now().time_since_epoch().count())) + ".lua";
    fs::path tmp_path = tmp_dir / tmp_name;

    // Write body to temp file
    {
        std::ofstream f(tmp_path);
        if (!f) return {true, 0, ""};  // Can't write temp — skip check
        f << body;
    }

#ifdef _WIN32
    std::string cmd = "luac -p \"" + tmp_path.string() + "\" 2>&1";
    FILE* fp = _popen(cmd.c_str(), "r");
#else
    // Use single-quoted path; unlikely to contain single quotes given our temp dir
    std::string cmd = "luac -p '" + tmp_path.string() + "' 2>&1";
    FILE* fp = popen(cmd.c_str(), "r");
#endif

    if (!fp) {
        // popen failed — luac unavailable; skip validation
        std::error_code ec;
        fs::remove(tmp_path, ec);
        return {true, 0, ""};
    }

    std::string output;
    char buf[512];
    while (fgets(buf, sizeof(buf), fp)) output += buf;

#ifdef _WIN32
    int pclose_rc = _pclose(fp);
    bool luac_ok  = (pclose_rc == 0);
#else
    int pclose_rc = pclose(fp);
    bool luac_ok  = WIFEXITED(pclose_rc) && (WEXITSTATUS(pclose_rc) == 0);
#endif

    std::error_code ec;
    fs::remove(tmp_path, ec);

    if (luac_ok) return {true, 0, ""};  // luac says OK

    // Detect "luac not found" rather than a real syntax error
    if (output.find("not found") != std::string::npos ||
        output.find("not recognized") != std::string::npos ||
        output.find("cannot find") != std::string::npos ||
        output.find("No such file") != std::string::npos) {
        Log::warn("luac not on PATH — skipping Lua syntax validation");
        return {true, 0, ""};
    }

    return parse_luac_output(output);
}

// Write body to path, creating parent directories if needed.
bool write_file(const fs::path& path, const std::string& body) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream f(path);
    if (!f) return false;
    f << body;
    return f.good();
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

RegisterResult register_script(const std::string& name,
                                const std::string& body,
                                const std::vector<std::string>& tags) {
    RegisterResult result;

    // --- Idempotency: return existing registration if name already known ---
    {
        auto existing = g_db.query(
            "SELECT id, script_path FROM scripts WHERE name = ?1", {name});
        if (!existing.empty()) {
            result.registered  = true;
            result.action_id   = existing[0].at("id");
            result.script_path = existing[0].at("script_path");
            Log::info("Scripts: idempotent — returning existing '" + name + "' → " +
                      result.action_id);
            return result;
        }
    }

    // --- Lua syntax validation ---
    if (g_config.validate_syntax) {
        auto syn = check_lua_syntax(body);
        if (!syn.ok) {
            result.syntax_error_line    = syn.line;
            result.syntax_error_message = syn.message;
            Log::warn("Scripts: syntax error in '" + name + "' line " +
                      std::to_string(syn.line) + ": " + syn.message);
            return result;
        }
    }

    // --- Build unique action ID and target file path ---
    std::string action_id   = make_action_id(name, body);
    std::string script_path = g_config.scripts_dir + action_id + ".lua";

    // --- Path traversal guard: verify path stays inside scripts_dir ---
    {
        fs::path scripts_root = fs::path(g_config.scripts_dir).lexically_normal();
        fs::path resolved     = fs::path(script_path).lexically_normal();
        fs::path rel          = resolved.lexically_relative(scripts_root);
        // lexically_relative returns an empty path or one starting with ".." if outside
        if (rel.empty() || rel.begin()->string() == "..") {
            result.internal_error = "Script path escapes scripts directory (path traversal rejected)";
            Log::error("Scripts: " + result.internal_error + " — " + script_path);
            return result;
        }
    }

    // --- Write script to disk ---
    if (!write_file(script_path, body)) {
        result.internal_error = "Failed to write script file: " + script_path;
        Log::error("Scripts: " + result.internal_error);
        return result;
    }

    // --- Register with REAPER on the main thread ---
    auto reg = Executor::post([script_path]() -> nlohmann::json {
        if (!AddRemoveReaScript)
            return {{"_error", "AddRemoveReaScript API function not available"}};
        int cmd = AddRemoveReaScript(true, 0, script_path.c_str(), true);
        if (cmd == 0)
            return {{"_error", "AddRemoveReaScript returned 0 (REAPER rejected script)"}};
        return {{"ok", true}, {"cmd", cmd}};
    });

    if (reg.contains("_timeout") || reg.contains("_error")) {
        // Clean up the file we just wrote
        std::error_code ec;
        fs::remove(script_path, ec);

        result.internal_error = reg.contains("_error")
            ? reg["_error"].get<std::string>()
            : "main thread timeout during AddRemoveReaScript";
        Log::error("Scripts: REAPER registration failed — " + result.internal_error);
        return result;
    }

    // --- Persist to DB ---
    nlohmann::json tags_json(tags);
    g_db.query(
        "INSERT OR REPLACE INTO scripts(id, name, body, script_path, tags) "
        "VALUES(?1, ?2, ?3, ?4, ?5)",
        {action_id, name, body, script_path, tags_json.dump()});

    Log::info("Scripts: registered '" + name + "' → " + action_id);

    result.registered  = true;
    result.action_id   = action_id;
    result.script_path = script_path;
    return result;
}

bool unregister_script(const std::string& action_id) {
    auto rows = g_db.query(
        "SELECT script_path FROM scripts WHERE id = ?1", {action_id});
    if (rows.empty()) return false;

    std::string script_path = rows[0].at("script_path");

    // Unregister from REAPER on the main thread (best-effort; ignore result)
    Executor::post([script_path]() -> nlohmann::json {
        if (AddRemoveReaScript)
            AddRemoveReaScript(false, 0, script_path.c_str(), true);
        return {{"ok", true}};
    });

    // Delete file from disk
    std::error_code ec;
    fs::remove(script_path, ec);
    if (ec) Log::warn("Scripts: could not delete " + script_path + ": " + ec.message());

    // Remove from DB
    g_db.query("DELETE FROM scripts WHERE id = ?1", {action_id});

    Log::info("Scripts: unregistered " + action_id);
    return true;
}

}  // namespace ReaClaw::Scripts

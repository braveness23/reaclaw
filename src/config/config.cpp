#include "config/config.h"
#include "util/logging.h"

#include <json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace ReaClaw {

namespace {

json default_config() {
    return {
        {"server", {
            {"host", "0.0.0.0"},
            {"port", 9091},
            {"thread_pool_size", 4}
        }},
        {"tls", {
            {"enabled", true},
            {"generate_if_missing", true},
            {"cert_file", ""},
            {"key_file", ""}
        }},
        {"auth", {
            {"type", "api_key"},
            {"key", "sk_change_me"}
        }},
        {"database", {
            {"path", ""}
        }},
        {"script_security", {
            {"validate_syntax", true},
            {"log_all_executions", true},
            {"max_script_size_kb", 512}
        }},
        {"logging", {
            {"level", "info"},
            {"file", ""}
        }}
    };
}

template<typename T>
T jval(const json& j, const std::string& key, T def) {
    try {
        if (j.contains(key) && !j[key].is_null()) return j[key].get<T>();
    } catch (...) {}
    return def;
}

}  // namespace

bool Config::load(Config& cfg, const std::string& resource_path) {
    cfg.resource_dir = resource_path + "/reaclaw/";
    cfg.certs_dir    = cfg.resource_dir + "certs/";
    cfg.scripts_dir  = cfg.resource_dir + "scripts/";

    std::error_code ec;
    fs::create_directories(cfg.resource_dir, ec);
    fs::create_directories(cfg.certs_dir,    ec);
    fs::create_directories(cfg.scripts_dir,  ec);

    const std::string config_path = cfg.resource_dir + "config.json";

    json j = default_config();

    if (!fs::exists(config_path)) {
        Log::info("Config not found; writing defaults to: " + config_path);
        std::ofstream f(config_path);
        if (!f) {
            Log::error("Cannot create config file: " + config_path);
            return false;
        }
        f << j.dump(4);
    } else {
        std::ifstream f(config_path);
        if (!f) {
            Log::error("Cannot read config file: " + config_path);
            return false;
        }
        try {
            json parsed;
            f >> parsed;
            j.merge_patch(parsed);
        } catch (const std::exception& e) {
            Log::error(std::string("Config parse error: ") + e.what());
            return false;
        }
    }

    auto& srv            = j["server"];
    cfg.host             = jval<std::string>(srv, "host", "0.0.0.0");
    cfg.port             = jval<int>(srv, "port", 9091);
    cfg.thread_pool_size = jval<int>(srv, "thread_pool_size", 4);

    auto& tls                   = j["tls"];
    cfg.tls_enabled             = jval<bool>(tls, "enabled", true);
    cfg.tls_generate_if_missing = jval<bool>(tls, "generate_if_missing", true);
    cfg.tls_cert_file           = jval<std::string>(tls, "cert_file", "");
    cfg.tls_key_file            = jval<std::string>(tls, "key_file", "");

    auto& auth    = j["auth"];
    cfg.auth_type = jval<std::string>(auth, "type", "api_key");
    cfg.auth_key  = jval<std::string>(auth, "key", "sk_change_me");

    cfg.db_path = jval<std::string>(j["database"], "path", "");
    if (cfg.db_path.empty()) cfg.db_path = cfg.resource_dir + "reaclawdb.sqlite";

    auto& ss               = j["script_security"];
    cfg.validate_syntax    = jval<bool>(ss, "validate_syntax", true);
    cfg.log_all_executions = jval<bool>(ss, "log_all_executions", true);
    cfg.max_script_size_kb = jval<int>(ss, "max_script_size_kb", 512);

    auto& log    = j["logging"];
    cfg.log_level = jval<std::string>(log, "level", "info");
    cfg.log_file  = jval<std::string>(log, "file", "");

    // Resolve TLS cert/key paths (user-supplied or derived from certs_dir)
    if (!cfg.tls_cert_file.empty()) {
        cfg.resolved_cert_path = cfg.tls_cert_file;
        cfg.resolved_key_path  = cfg.tls_key_file;
    } else {
        cfg.resolved_cert_path = cfg.certs_dir + "reaclaw.crt";
        cfg.resolved_key_path  = cfg.certs_dir + "reaclaw.key";
    }

    return true;
}

bool Config::save() const {
    json j = {
        {"server", {
            {"host", host}, {"port", port}, {"thread_pool_size", thread_pool_size}
        }},
        {"tls", {
            {"enabled", tls_enabled}, {"generate_if_missing", tls_generate_if_missing},
            {"cert_file", tls_cert_file}, {"key_file", tls_key_file}
        }},
        {"auth", {{"type", auth_type}, {"key", auth_key}}},
        {"database", {{"path", db_path}}},
        {"script_security", {
            {"validate_syntax", validate_syntax},
            {"log_all_executions", log_all_executions},
            {"max_script_size_kb", max_script_size_kb}
        }},
        {"logging", {{"level", log_level}, {"file", log_file}}}
    };

    std::ofstream f(resource_dir + "config.json");
    if (!f) return false;
    f << j.dump(4);
    return true;
}

}  // namespace ReaClaw

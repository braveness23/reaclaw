#pragma once
#include <string>

namespace ReaClaw {

struct Config {
    // server
    std::string host             = "0.0.0.0";
    int         port             = 9091;
    int         thread_pool_size = 4;

    // tls
    bool        tls_enabled             = true;
    bool        tls_generate_if_missing = true;
    std::string tls_cert_file;
    std::string tls_key_file;

    // auth
    std::string auth_type = "api_key";  // "none" or "api_key"
    std::string auth_key  = "sk_change_me";

    // database
    std::string db_path;  // defaults to {resource_dir}/reaclawdb.sqlite

    // script_security
    bool validate_syntax    = true;
    bool log_all_executions = true;
    int  max_script_size_kb = 512;

    // logging
    std::string log_level  = "info";  // debug, info, warn, error
    std::string log_file;             // empty = REAPER console only
    std::string log_format = "text";  // "text" or "json"

    // Derived paths (filled in by load())
    std::string resource_dir;   // {GetResourcePath()}/reaclaw/
    std::string certs_dir;      // {resource_dir}/certs/
    std::string scripts_dir;    // {resource_dir}/scripts/

    // Resolved TLS paths (filled in by load(); may be derived from resource_dir)
    std::string resolved_cert_path;
    std::string resolved_key_path;

    // Load config from {resource_path}/reaclaw/config.json.
    // Writes defaults when the file is missing.
    // Returns false only on fatal error (cannot create directory or parse JSON).
    static bool load(Config& out, const std::string& resource_path);

    // Persist current values back to config.json.
    bool save() const;
};

}  // namespace ReaClaw

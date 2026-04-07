#include "auth/auth.h"
#include "config/config.h"

#include <httplib.h>

namespace ReaClaw::Auth {

bool check(const Config& cfg, const httplib::Request& req) {
    if (cfg.auth_type != "api_key") return true;  // auth.type = "none"

    auto it = req.headers.find("Authorization");
    if (it == req.headers.end()) return false;

    const std::string& hdr    = it->second;
    const std::string  prefix = "Bearer ";
    if (hdr.size() <= prefix.size()) return false;
    if (hdr.substr(0, prefix.size()) != prefix) return false;

    return hdr.substr(prefix.size()) == cfg.auth_key;
}

void reject(httplib::Response& res) {
    res.status = 401;
    res.set_content(
        R"({"error":"Unauthorized","code":"UNAUTHORIZED","context":{}})",
        "application/json");
}

}  // namespace ReaClaw::Auth

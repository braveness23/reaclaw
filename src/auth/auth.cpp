#include "auth/auth.h"

#include "config/config.h"
#include "util/logging.h"

#include <httplib.h>

namespace ReaClaw::Auth {

bool check(const Config& cfg, const httplib::Request& req) {
    if (cfg.auth_type != "api_key")
        return true;  // auth.type = "none"

    auto it = req.headers.find("Authorization");
    if (it == req.headers.end()) {
        Log::warn("Auth: missing Authorization header from " + req.remote_addr);
        return false;
    }

    const std::string& hdr = it->second;
    const std::string prefix = "Bearer ";
    if (hdr.size() <= prefix.size() || hdr.substr(0, prefix.size()) != prefix) {
        Log::warn("Auth: malformed Authorization header from " + req.remote_addr);
        return false;
    }

    if (hdr.substr(prefix.size()) != cfg.auth_key) {
        Log::warn("Auth: invalid API key from " + req.remote_addr);
        return false;
    }

    return true;
}

void reject(httplib::Response& res) {
    res.status = 401;
    res.set_content(R"({"error":"Unauthorized","code":"UNAUTHORIZED","context":{}})",
                    "application/json");
}

}  // namespace ReaClaw::Auth

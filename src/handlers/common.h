#pragma once
#include <httplib.h>

#include <chrono>
#include <ctime>
#include <string>

#include <json.hpp>

namespace ReaClaw::Handlers {

inline void json_ok(httplib::Response& res, const nlohmann::json& body) {
    res.status = 200;
    res.set_content(body.dump(), "application/json");
}

inline void json_error(httplib::Response& res,
                       int status,
                       const std::string& msg,
                       const std::string& code,
                       nlohmann::json ctx = nlohmann::json::object()) {
    res.status = status;
    nlohmann::json j{{"error", msg}, {"code", code}, {"context", ctx}};
    res.set_content(j.dump(), "application/json");
}

inline void not_implemented(httplib::Response& res) {
    json_error(res, 501, "Not implemented in this phase", "NOT_IMPLEMENTED");
}

inline std::string now_iso() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf {};
#ifdef _WIN32
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return buf;
}

// Convert REAPER linear volume (0.0+) to dB.
inline double vol_to_db(double vol) {
    if (vol <= 0.0)
        return -150.0;
    return 20.0 * std::log10(vol);
}

// Convert dB to REAPER linear volume.
inline double db_to_vol(double db) {
    return std::pow(10.0, db / 20.0);
}

// Format a REAPER GUID struct as "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}".
// The GUID type is defined in reaper_plugin.h.
template <typename GUID_T>
std::string guid_to_string(const GUID_T* g) {
    if (!g)
        return "{}";
    char buf[48];
    snprintf(buf,
             sizeof(buf),
             "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
             static_cast<unsigned>(g->Data1),
             static_cast<unsigned>(g->Data2),
             static_cast<unsigned>(g->Data3),
             g->Data4[0],
             g->Data4[1],
             g->Data4[2],
             g->Data4[3],
             g->Data4[4],
             g->Data4[5],
             g->Data4[6],
             g->Data4[7]);
    return buf;
}

// Extract optional X-Agent-Id header value.
inline std::string agent_id(const httplib::Request& req) {
    auto it = req.headers.find("X-Agent-Id");
    return it != req.headers.end() ? it->second : "";
}

}  // namespace ReaClaw::Handlers

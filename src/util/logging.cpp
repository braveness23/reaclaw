#include "util/logging.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>

namespace ReaClaw::Log {

namespace {

LogLevel               g_level        = LogLevel::info;
bool                   g_json_format  = false;
void (*g_show_console)(const char*)   = nullptr;
std::ofstream          g_file;
std::mutex             g_mutex;

const char* level_str(LogLevel l) {
    switch (l) {
        case LogLevel::debug: return "DEBUG";
        case LogLevel::info:  return "INFO ";
        case LogLevel::warn:  return "WARN ";
        case LogLevel::error: return "ERROR";
    }
    return "     ";
}

const char* level_str_short(LogLevel l) {
    switch (l) {
        case LogLevel::debug: return "debug";
        case LogLevel::info:  return "info";
        case LogLevel::warn:  return "warn";
        case LogLevel::error: return "error";
    }
    return "info";
}

std::string now_iso() {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
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

// Minimal JSON string escaper — avoids pulling in nlohmann/json.
std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

void emit(LogLevel level, const std::string& msg) {
    if (level < g_level) return;

    std::string line;
    if (g_json_format) {
        // {"level":"info","ts":"2026-04-07T12:00:00Z","msg":"..."}
        line  = "{\"level\":\"";
        line += level_str_short(level);
        line += "\",\"ts\":\"";
        line += now_iso();
        line += "\",\"msg\":\"";
        line += json_escape(msg);
        line += "\"}\n";
    } else {
        // "ReaClaw [LEVEL] [timestamp] message\n"
        line  = "ReaClaw [";
        line += level_str(level);
        line += "] [";
        line += now_iso();
        line += "] ";
        line += msg;
        line += "\n";
    }

    std::lock_guard<std::mutex> lk(g_mutex);

    if (g_show_console) {
        g_show_console(line.c_str());
    }
    if (g_file.is_open()) {
        g_file << line;
        g_file.flush();
    }
    if (!g_show_console && !g_file.is_open()) {
        fputs(line.c_str(), stderr);
    }
}

}  // namespace

void init(LogLevel level, const std::string& file_path,
          void (*show_console_fn)(const char*),
          const std::string& format) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_level        = level;
    g_json_format  = (format == "json");
    g_show_console = show_console_fn;
    // Close any existing file handle before (re-)opening.
    if (g_file.is_open()) g_file.close();
    if (!file_path.empty()) {
        g_file.open(file_path, std::ios::app);
    }
}

void debug(const std::string& msg) { emit(LogLevel::debug, msg); }
void info(const std::string& msg)  { emit(LogLevel::info,  msg); }
void warn(const std::string& msg)  { emit(LogLevel::warn,  msg); }
void error(const std::string& msg) { emit(LogLevel::error, msg); }

}  // namespace ReaClaw::Log

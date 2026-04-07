#include "util/logging.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>

namespace ReaClaw::Log {

namespace {

LogLevel               g_level        = LogLevel::info;
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

void emit(LogLevel level, const std::string& msg) {
    if (level < g_level) return;

    // "ReaClaw [LEVEL] [timestamp] message\n"
    std::string line = "ReaClaw [";
    line += level_str(level);
    line += "] [";
    line += now_iso();
    line += "] ";
    line += msg;
    line += "\n";

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
          void (*show_console_fn)(const char*)) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_level        = level;
    g_show_console = show_console_fn;
    if (!file_path.empty() && !g_file.is_open()) {
        g_file.open(file_path, std::ios::app);
    }
}

void debug(const std::string& msg) { emit(LogLevel::debug, msg); }
void info(const std::string& msg)  { emit(LogLevel::info,  msg); }
void warn(const std::string& msg)  { emit(LogLevel::warn,  msg); }
void error(const std::string& msg) { emit(LogLevel::error, msg); }

}  // namespace ReaClaw::Log

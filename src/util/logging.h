#pragma once
#include <string>

namespace ReaClaw {

enum class LogLevel { debug = 0, info = 1, warn = 2, error = 3 };

namespace Log {

// Call once after REAPERAPI_LoadAPI has run.
// show_console_fn: pointer to REAPER's ShowConsoleMsg (may be null before init).
// level: minimum level to emit.
// file_path: if non-empty, also append to this file.
// format: "text" (default) or "json" for structured log output.
void init(LogLevel level,
          const std::string& file_path,
          void (*show_console_fn)(const char*),
          const std::string& format = "text");

void debug(const std::string& msg);
void info(const std::string& msg);
void warn(const std::string& msg);
void error(const std::string& msg);

}  // namespace Log
}  // namespace ReaClaw

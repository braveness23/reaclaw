#include "handlers/restart.h"

#include "handlers/common.h"
#include "reaper/executor.h"
#include "util/logging.h"

#include <httplib.h>

#include <string>
#include <vector>

#include <json.hpp>

#ifdef __linux__

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include <fcntl.h>
#include <reaper_plugin_functions.h>
#include <unistd.h>

namespace ReaClaw::Handlers {

namespace {

// Read a /proc file's raw bytes (may contain embedded NULs — cmdline/environ
// are NUL-separated, not text — so this copies via rdbuf() rather than
// stream extraction, which would stop at whitespace).
std::string read_all_bytes(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Split a NUL-separated buffer (as found in /proc/self/cmdline and
// /proc/self/environ) into strings, dropping the trailing empty entry.
std::vector<std::string> split_nul(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\0') {
            if (i > start)
                out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    if (start < s.size())
        out.push_back(s.substr(start));
    return out;
}

// Build a null-terminated char* argv/envp array. The backing `strs` vector
// must outlive the returned array (and the execve() call using it).
std::vector<char*> to_cstr_array(const std::vector<std::string>& strs) {
    std::vector<char*> out;
    out.reserve(strs.size() + 1);
    for (const auto& s : strs)
        out.push_back(const_cast<char*>(s.c_str()));
    out.push_back(nullptr);
    return out;
}

// Poll until `pid` no longer exists (kill(pid, 0) fails with ESRCH), or
// `timeout_ms` elapses. Returns true if the process exited within the window.
bool wait_for_exit(pid_t pid, int timeout_ms, int poll_ms) {
    int waited = 0;
    while (waited < timeout_ms) {
        if (kill(pid, 0) != 0 && errno == ESRCH)
            return true;
        usleep(static_cast<useconds_t>(poll_ms) * 1000);
        waited += poll_ms;
    }
    return false;
}

// Child-process body: detach, kill the original REAPER process (graceful
// then forceful), and relaunch it with the exact original argv/environment.
// Never returns on success (execve replaces the process image); on failure,
// falls back to a plain file write since ReaClaw's own logger/config live in
// memory that's about to be irrelevant to this now-orphaned process.
[[noreturn]] void restart_child(pid_t target_pid,
                                std::vector<std::string> argv_strs,
                                std::vector<std::string> envp_strs) {
    setsid();
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
    }

    usleep(300 * 1000);  // safety margin so the HTTP response has time to flush

    kill(target_pid, SIGTERM);
    if (!wait_for_exit(target_pid, 8000, 150)) {
        kill(target_pid, SIGKILL);
        wait_for_exit(target_pid, 2000, 100);
    }

    auto argv = to_cstr_array(argv_strs);
    auto envp = to_cstr_array(envp_strs);
    execve(argv[0], argv.data(), envp.data());

    // Only reached if execve failed.
    FILE* f = fopen("/tmp/reaclaw_restart_failed.log", "a");
    if (f) {
        fprintf(f, "execve(%s) failed: errno=%d (%s)\n", argv[0], errno, strerror(errno));
        fclose(f);
    }
    _exit(1);
}

}  // namespace

void handle_reaper_restart(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try {
        body = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
    } catch (...) {
        json_error(res, 400, "Invalid JSON body", "BAD_REQUEST");
        return;
    }
    bool want_save = body.value("save_project", true);

    // Best-effort save with a short timeout — the whole point of this
    // endpoint is recovering from a wedged main thread, so this must not
    // block the recovery path itself. Mirrors handle_project_save's guard:
    // only save in place if the project already has a filename (a never-
    // saved project would otherwise risk a save-as dialog — exactly the
    // kind of unrecoverable modal this endpoint exists to escape).
    bool saved = false;
    std::string save_note;
    if (want_save) {
        auto result = Executor::post(
                []() -> nlohmann::json {
                    // EnumProjects(-1, ...), not GetSetProjectInfo_String(...,
                    // "PROJECT_FILENAME", ...) — see project.cpp's
                    // project_filename() for why.
                    std::vector<char> fn(4096, 0);
                    if (EnumProjects)
                        EnumProjects(-1, fn.data(), static_cast<int>(fn.size()));
                    if (fn[0] == '\0')
                        return {{"skipped", "project has never been saved"}};
                    // Main_SaveProjectEx needs an actual filename — nullptr
                    // silently no-ops rather than meaning "current file".
                    if (Main_SaveProjectEx)
                        Main_SaveProjectEx(nullptr, fn.data(), 0);
                    return {{"ok", true}};
                },
                5);
        if (result.contains("_timeout")) {
            save_note = "save timed out (main thread unresponsive) — proceeding without it";
        } else if (result.contains("skipped")) {
            save_note = result["skipped"].get<std::string>();
        } else if (result.value("ok", false)) {
            saved = true;
        } else {
            save_note = "save did not complete";
        }
    } else {
        save_note = "save_project was false";
    }

    pid_t target_pid = getpid();
    std::vector<std::string> argv_strs = split_nul(read_all_bytes("/proc/self/cmdline"));
    std::vector<std::string> envp_strs = split_nul(read_all_bytes("/proc/self/environ"));
    if (argv_strs.empty()) {
        json_error(res,
                   500,
                   "Could not read /proc/self/cmdline — cannot determine restart command",
                   "INTERNAL_ERROR");
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        json_error(res, 500, "fork() failed", "INTERNAL_ERROR");
        return;
    }
    if (pid == 0) {
        restart_child(target_pid, argv_strs, envp_strs);
        // unreachable — restart_child is [[noreturn]]
    }

    Log::warn("POST /reaper/restart — relaunching pid " + std::to_string(target_pid) +
              " (saved=" + (saved ? "true" : "false") + ")");
    nlohmann::json restart_command(argv_strs);
    nlohmann::json resp = {{"restarting", true},
                           {"saved", saved},
                           {"pid", target_pid},
                           {"restart_command", restart_command}};
    if (!save_note.empty())
        resp["save_note"] = save_note;
    json_ok(res, resp);
}

}  // namespace ReaClaw::Handlers

#else  // Linux-only — the mechanism is /proc/self/{cmdline,environ} based.

namespace ReaClaw::Handlers {

void handle_reaper_restart(const httplib::Request&, httplib::Response& res) {
    json_error(res,
               501,
               "POST /reaper/restart is implemented for Linux only (relies on "
               "/proc/self/cmdline and /proc/self/environ)",
               "NOT_IMPLEMENTED");
}

}  // namespace ReaClaw::Handlers

#endif

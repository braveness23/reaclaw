#pragma once

// Persistent child-process wrapper for long-lived capture pipelines (video/
// audio streaming). Unlike the block-until-exit `run_capture()` helper in
// util/x11_capture.h, callers here read from a live child over minutes and
// must be able to tear it down deterministically (client disconnect,
// extension shutdown) without leaking a process. POSIX-only; spawn() always
// fails on Windows (no fork/execvp — matches screenshot.cpp's Linux/X11-only
// scope). No OS headers here so this stays includable from a translation
// unit built on any platform.

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace ReaClaw::Util {

class Subprocess {
   public:
    // Spawns argv[0] via fork/execvp (no shell — argv values can't inject).
    // stdout is captured for read_some(); stderr goes to /dev/null. Returns
    // nullptr on fork/pipe failure (or unconditionally on Windows).
    static std::unique_ptr<Subprocess> spawn(const std::vector<std::string>& argv);

    ~Subprocess();
    Subprocess(const Subprocess&) = delete;
    Subprocess& operator=(const Subprocess&) = delete;

    // Blocking read of up to len bytes from the child's stdout. Returns 0 on
    // EOF (child exited / closed stdout), <0 on error.
    long read_some(char* buf, size_t len);

    // Liveness check (reaps the child once it has exited; safe to call
    // repeatedly).
    bool alive();

   private:
    Subprocess(long pid, int stdout_fd);
    long pid_;
    int stdout_fd_;
    bool reaped_ = false;
};

}  // namespace ReaClaw::Util

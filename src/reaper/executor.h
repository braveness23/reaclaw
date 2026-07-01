#pragma once
#include <functional>

#include <json.hpp>

namespace ReaClaw::Executor {

// Post a callable to REAPER's main thread and block until it completes.
// Returns {"_timeout": true} if the main thread doesn't drain within timeout_seconds.
// Returns {"_error": "..."} if the callable throws.
nlohmann::json post(std::function<nlohmann::json()> fn, int timeout_seconds = 15);

// Called from the REAPER main-thread timer (~30fps). Drains the command queue.
void tick();

// Returns the number of commands currently waiting in the queue.
size_t queue_depth();

// Returns true if the queue has been non-empty for >10s without a tick() drain —
// indicating the main thread is stuck (e.g. blocked by ShowConsoleMsg).
bool is_stuck();

// Issue #64 — drain the pending command backlog without waiting for the main
// thread. Resolves every queued (not-yet-executing) command's promise with
// {"_flushed": true} so its blocked HTTP caller returns immediately instead of
// waiting out its timeout. Does not affect a command already mid-execute() on
// the main thread (if the main thread itself is wedged, only a REAPER restart
// recovers that one). Returns the number of commands flushed.
size_t flush();

// Issue #31 — attribution for the event feed. True while main-thread code is
// running on ReaClaw's behalf, false otherwise. A control-surface callback
// firing synchronously inside that call stack (REAPER dispatches state-change
// notifications inline, before the triggering SDK call returns) reads this to
// tag its event source "reaclaw" vs. "external". Main-thread only, so a plain
// depth counter (not atomic) is enough — set/read on the same thread.
bool is_reaclaw_editing();

// RAII guard marking a block of main-thread code as reaclaw-originated.
// tick() wraps every command's execute() with one; execute.cpp's async path
// (which fires via SWELL SetTimer, not the command queue — see execute.cpp)
// wraps its own Main_OnCommand call with a second. Nest-safe (depth-counted),
// so nothing needs to know whether it's already inside one.
class EditingGuard {
   public:
    EditingGuard();
    ~EditingGuard();
    EditingGuard(const EditingGuard&) = delete;
    EditingGuard& operator=(const EditingGuard&) = delete;
};

}  // namespace ReaClaw::Executor

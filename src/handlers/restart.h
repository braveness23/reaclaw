#pragma once

#include <httplib.h>

namespace ReaClaw::Handlers {

// Issue #77 — let ReaClaw restart the REAPER process it's embedded in, for
// self-recovery when the main thread is wedged and POST /queue/flush's
// pending-backlog drain (issue #64) isn't enough (only a full REAPER restart
// unsticks a call that's actually stuck mid-execute).
//
//   POST /reaper/restart
//   Body: { "save_project": true }   // default true; best-effort, short timeout
//
// Deliberately does NOT go through Executor::post — that would block forever
// if the main thread is the thing that's stuck. Captures REAPER's own
// current argv/environment from /proc/self/{cmdline,environ} (this process
// *is* REAPER — ReaClaw is a shared library loaded inside it) and replays
// them byte-for-byte on relaunch, so DISPLAY/XAUTHORITY/etc. are exactly
// what's already working, not reconstructed from a different shell context.
// Linux-only (the mechanism is /proc-based); 501 on other platforms.
void handle_reaper_restart(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

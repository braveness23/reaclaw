#pragma once
#include <functional>

#include <json.hpp>

namespace ReaClaw::Executor {

// Post a callable to REAPER's main thread and block until it completes.
// Returns {"_timeout": true} if the main thread doesn't drain within timeout_seconds.
// Returns {"_error": "..."} if the callable throws.
nlohmann::json post(std::function<nlohmann::json()> fn, int timeout_seconds = 5);

// Called from the REAPER main-thread timer (~30fps). Drains the command queue.
void tick();

// Returns the number of commands currently waiting in the queue.
size_t queue_depth();

}  // namespace ReaClaw::Executor

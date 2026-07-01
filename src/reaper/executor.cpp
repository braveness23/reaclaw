#include "reaper/executor.h"

#include "util/logging.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <future>
#include <mutex>
#include <queue>

namespace ReaClaw::Executor {

namespace {

struct Command {
    std::function<nlohmann::json()> execute;
    std::promise<nlohmann::json> result;
};

std::queue<Command> s_queue;
std::mutex s_mutex;

// Milliseconds since epoch when tick() last ran. Used to detect a stuck main thread.
std::atomic<int64_t> s_last_tick_ms{0};

// Milliseconds since epoch when the queue first became non-empty in the current
// backlog (reset to 0 when the queue empties).
std::atomic<int64_t> s_queue_nonempty_since_ms{0};

// Issue #31 attribution — main-thread only, plain int (not atomic).
int s_reaclaw_editing_depth = 0;

}  // namespace

bool is_reaclaw_editing() {
    return s_reaclaw_editing_depth > 0;
}

EditingGuard::EditingGuard() {
    s_reaclaw_editing_depth++;
}

EditingGuard::~EditingGuard() {
    s_reaclaw_editing_depth--;
}

nlohmann::json post(std::function<nlohmann::json()> fn, int timeout_seconds) {
    Command cmd;
    std::future<nlohmann::json> fut = cmd.result.get_future();
    cmd.execute = std::move(fn);

    {
        std::lock_guard<std::mutex> lk(s_mutex);
        s_queue.push(std::move(cmd));
        // Record when the queue first became non-empty (don't overwrite if already set).
        if (s_queue_nonempty_since_ms.load() == 0) {
            s_queue_nonempty_since_ms.store(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count());
        }
    }

    auto status = fut.wait_for(std::chrono::seconds(timeout_seconds));
    if (status == std::future_status::timeout) {
        Log::warn("Command queue timeout after " + std::to_string(timeout_seconds) + "s");
        return nlohmann::json{{"_timeout", true}};
    }
    try {
        return fut.get();
    } catch (const std::exception& e) {
        Log::error(std::string("Command execution threw: ") + e.what());
        return nlohmann::json{{"_error", e.what()}};
    } catch (...) {
        Log::error("Command execution threw unknown exception");
        return nlohmann::json{{"_error", "unknown"}};
    }
}

size_t queue_depth() {
    std::lock_guard<std::mutex> lk(s_mutex);
    return s_queue.size();
}

void tick() {
    s_last_tick_ms.store(std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count());

    // Drain up to 10 commands per tick (~30fps → ~300 commands/sec sustained).
    for (int i = 0; i < 10; i++) {
        Command cmd;
        {
            std::lock_guard<std::mutex> lk(s_mutex);
            if (s_queue.empty()) {
                s_queue_nonempty_since_ms.store(0);  // queue empty — reset stuck timer
                break;
            }
            cmd = std::move(s_queue.front());
            s_queue.pop();
        }
        try {
            EditingGuard guard;
            cmd.result.set_value(cmd.execute());
        } catch (...) {
            cmd.result.set_exception(std::current_exception());
        }
    }
}

size_t flush() {
    std::queue<Command> drained;
    {
        std::lock_guard<std::mutex> lk(s_mutex);
        std::swap(drained, s_queue);
        s_queue_nonempty_since_ms.store(0);
    }
    size_t n = drained.size();
    while (!drained.empty()) {
        drained.front().result.set_value(nlohmann::json{{"_flushed", true}});
        drained.pop();
    }
    if (n > 0)
        Log::warn("Executor: flushed " + std::to_string(n) + " pending command(s)");
    return n;
}

bool is_stuck() {
    if (queue_depth() == 0)
        return false;
    int64_t since = s_queue_nonempty_since_ms.load();
    if (since == 0)
        return false;
    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count();
    // Degraded if queue has been non-empty for >10s (main thread not draining).
    return (now_ms - since) > 10000;
}

}  // namespace ReaClaw::Executor

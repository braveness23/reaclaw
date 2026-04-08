#include "reaper/executor.h"

#include "util/logging.h"

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

}  // namespace

nlohmann::json post(std::function<nlohmann::json()> fn, int timeout_seconds) {
    Command cmd;
    std::future<nlohmann::json> fut = cmd.result.get_future();
    cmd.execute = std::move(fn);

    {
        std::lock_guard<std::mutex> lk(s_mutex);
        s_queue.push(std::move(cmd));
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
    // Drain up to 10 commands per tick (~30fps → ~300 commands/sec sustained).
    for (int i = 0; i < 10; i++) {
        Command cmd;
        {
            std::lock_guard<std::mutex> lk(s_mutex);
            if (s_queue.empty())
                break;
            cmd = std::move(s_queue.front());
            s_queue.pop();
        }
        try {
            cmd.result.set_value(cmd.execute());
        } catch (...) {
            cmd.result.set_exception(std::current_exception());
        }
    }
}

}  // namespace ReaClaw::Executor

#include "streaming/registry.h"

#include <algorithm>
#include <atomic>

namespace ReaClaw::Streaming {

namespace {
std::atomic<uint64_t> g_next_id{1};
}

Registry& instance() {
    static Registry r;
    return r;
}

std::string Registry::register_stream(const std::string& kind, const std::string& client) {
    std::string id = kind + "-" + std::to_string(g_next_id++);
    std::lock_guard<std::mutex> lock(mu_);
    entries_.push_back(
            Entry{StreamInfo{id, kind, client, std::chrono::system_clock::now()}, false});
    return id;
}

void Registry::unregister(const std::string& id) {
    std::lock_guard<std::mutex> lock(mu_);
    entries_.erase(std::remove_if(entries_.begin(),
                                  entries_.end(),
                                  [&](const Entry& e) {
                                      return e.info.id == id;
                                  }),
                   entries_.end());
}

bool Registry::request_stop(const std::string& id) {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& e : entries_) {
        if (e.info.id == id) {
            e.stop = true;
            return true;
        }
    }
    return false;
}

bool Registry::stop_requested(const std::string& id) {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& e : entries_) {
        if (e.info.id == id)
            return e.stop;
    }
    return true;
}

std::vector<StreamInfo> Registry::list() {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<StreamInfo> out;
    out.reserve(entries_.size());
    for (auto& e : entries_)
        out.push_back(e.info);
    return out;
}

void Registry::shutdown_all() {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& e : entries_)
        e.stop = true;
}

}  // namespace ReaClaw::Streaming

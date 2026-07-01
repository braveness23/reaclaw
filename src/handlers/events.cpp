#include "handlers/events.h"

#include "handlers/common.h"
#include "reaper/csurf.h"

#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>

#include <json.hpp>

namespace ReaClaw::Handlers {

namespace {

nlohmann::json event_to_json(const Csurf::Event& e) {
    nlohmann::json j = {{"seq", e.seq},
                        {"ts", e.ts},
                        {"kind", e.kind},
                        {"value", e.value},
                        {"source", e.source}};
    if (e.track_index >= 0)
        j["track_index"] = e.track_index;
    if (!e.track_guid.empty())
        j["track_guid"] = e.track_guid;
    return j;
}

int64_t query_since(const httplib::Request& req) {
    if (!req.has_param("since"))
        return 0;
    try {
        return std::stoll(req.get_param_value("since"));
    } catch (...) {
        return 0;
    }
}

int query_limit(const httplib::Request& req, int dflt, int lo, int hi) {
    if (!req.has_param("limit"))
        return dflt;
    try {
        int v = std::stoi(req.get_param_value("limit"));
        return std::max(lo, std::min(hi, v));
    } catch (...) {
        return dflt;
    }
}

}  // namespace

// GET /events?since=<cursor>&limit=
void handle_events_list(const httplib::Request& req, httplib::Response& res) {
    int64_t since = query_since(req);
    int limit = query_limit(req, 100, 1, 500);

    auto evs = Csurf::events_since(since, limit);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : evs)
        arr.push_back(event_to_json(e));

    json_ok(res, {{"cursor", Csurf::head_cursor()}, {"events", arr}});
}

// GET /events/stream?since= — Server-Sent Events. Each event is one
// `data: {...}\n\n` frame. Polls the shared event ring every 300ms (there's
// no push-wakeup from the main thread to an HTTP worker — the control
// surface only ever writes to the ring, it never signals anyone) and streams
// anything new. Bounded to kMaxStreamDuration so one connection can't hold an
// httplib worker thread forever; the client should reconnect with `since` set
// to the last seq it saw. Single-user tool (TECH_DECISIONS §13) — no
// per-connection limit beyond httplib's own thread pool size.
void handle_events_stream(const httplib::Request& req, httplib::Response& res) {
    int64_t since = query_since(req);
    res.set_header("Cache-Control", "no-cache");
    res.set_chunked_content_provider(
            "text/event-stream", [since](size_t, httplib::DataSink& sink) mutable -> bool {
                constexpr auto kMaxStreamDuration = std::chrono::minutes(10);
                constexpr auto kPollInterval = std::chrono::milliseconds(300);
                auto start = std::chrono::steady_clock::now();
                while (std::chrono::steady_clock::now() - start < kMaxStreamDuration) {
                    if (!sink.is_writable())
                        return false;
                    for (const auto& e : Csurf::events_since(since, 100)) {
                        std::string line = "data: " + event_to_json(e).dump() + "\n\n";
                        if (!sink.write(line.c_str(), line.size()))
                            return false;
                        since = e.seq;
                    }
                    std::this_thread::sleep_for(kPollInterval);
                }
                sink.done();
                return false;
            });
}

}  // namespace ReaClaw::Handlers

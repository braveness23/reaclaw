#include "handlers/stream_admin.h"

#include "handlers/common.h"
#include "streaming/registry.h"

#include <chrono>

#include <json.hpp>

namespace ReaClaw::Handlers {

void handle_stream_status(const httplib::Request&, httplib::Response& res) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& s : Streaming::instance().list()) {
        auto started_unix = std::chrono::duration_cast<std::chrono::seconds>(
                                    s.started_at.time_since_epoch())
                                    .count();
        arr.push_back({{"id", s.id},
                       {"kind", s.kind},
                       {"client", s.client},
                       {"started_at_unix", started_unix}});
    }
    json_ok(res, {{"streams", arr}});
}

void handle_stream_stop(const httplib::Request& req, httplib::Response& res) {
    std::string id = req.path_params.at("id");
    if (!Streaming::instance().request_stop(id)) {
        json_error(res, 404, "No active stream with id '" + id + "'", "NOT_FOUND");
        return;
    }
    json_ok(res, {{"id", id}, {"stopping", true}});
}

}  // namespace ReaClaw::Handlers

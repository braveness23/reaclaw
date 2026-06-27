#include "handlers/chunk.h"

#include "app.h"
#include "handlers/common.h"
#include "handlers/state.h"
#include "reaper/executor.h"
#include "util/logging.h"

#include <httplib.h>

#include <functional>
#include <string>
#include <vector>

#include <reaper_plugin_functions.h>

#include <json.hpp>

namespace ReaClaw::Handlers {

namespace {

// Cap on chunk size we'll grow the read buffer to. Track chunks with many items
// and FX can be large; 64 MB is comfortably beyond any real session object.
constexpr size_t k_chunk_read_cap = 64u * 1024u * 1024u;

// Call a Get*StateChunk getter with a growing buffer until it succeeds.
// getter(buf, size) must return true on success (chunk fit the buffer).
bool read_chunk_growing(const std::function<bool(char*, int)>& getter, std::string& out) {
    size_t sz = 256u * 1024u;
    while (sz <= k_chunk_read_cap) {
        std::vector<char> buf(sz, 0);
        if (getter(buf.data(), static_cast<int>(sz))) {
            out.assign(buf.data());  // NUL-terminated by REAPER
            return true;
        }
        sz *= 2;
    }
    return false;
}

// Resolve the requested target's chunk text on the main thread. Returns one of:
//   {chunk: "..."}            success
//   {_bad_request: "msg"}     invalid params
//   {_not_found: true}        index out of range
//   {_error: "msg"}           getter failed / API unavailable
nlohmann::json read_target(const std::string& target, int index, int env_index) {
    if (target == "track") {
        if (!GetTrack || !GetTrackStateChunk)
            return {{"_error", "track chunk API unavailable"}};
        if (index < 0 || index >= CountTracks(nullptr))
            return {{"_not_found", true}};
        MediaTrack* t = GetTrack(nullptr, index);
        std::string chunk;
        if (!t || !read_chunk_growing(
                          [&](char* b, int n) {
                              return GetTrackStateChunk(t, b, n, false);
                          },
                          chunk))
            return {{"_error", "failed to read track chunk"}};
        return {{"chunk", chunk}};
    }
    if (target == "item") {
        if (!GetMediaItem || !GetItemStateChunk)
            return {{"_error", "item chunk API unavailable"}};
        if (index < 0 || index >= CountMediaItems(nullptr))
            return {{"_not_found", true}};
        MediaItem* it = GetMediaItem(nullptr, index);
        std::string chunk;
        if (!it || !read_chunk_growing(
                           [&](char* b, int n) {
                               return GetItemStateChunk(it, b, n, false);
                           },
                           chunk))
            return {{"_error", "failed to read item chunk"}};
        return {{"chunk", chunk}};
    }
    if (target == "envelope") {
        if (!GetTrack || !GetTrackEnvelope || !GetEnvelopeStateChunk)
            return {{"_error", "envelope chunk API unavailable"}};
        if (index < 0 || index >= CountTracks(nullptr))
            return {{"_not_found", true}};
        MediaTrack* t = GetTrack(nullptr, index);
        if (!t)
            return {{"_not_found", true}};
        if (env_index < 0 || env_index >= CountTrackEnvelopes(t))
            return {{"_bad_request", "envelope index out of range"}};
        TrackEnvelope* env = GetTrackEnvelope(t, env_index);
        std::string chunk;
        if (!env || !read_chunk_growing(
                            [&](char* b, int n) {
                                return GetEnvelopeStateChunk(env, b, n, false);
                            },
                            chunk))
            return {{"_error", "failed to read envelope chunk"}};
        return {{"chunk", chunk}};
    }
    return {{"_bad_request", "target must be one of: track, item, envelope"}};
}

// Apply a chunk to the requested target on the main thread (inside an undo block).
nlohmann::json
write_target(const std::string& target, int index, int env_index, const std::string& chunk) {
    if (target == "track") {
        if (!GetTrack || !SetTrackStateChunk)
            return {{"_error", "track chunk API unavailable"}};
        if (index < 0 || index >= CountTracks(nullptr))
            return {{"_not_found", true}};
        MediaTrack* t = GetTrack(nullptr, index);
        if (!t || !SetTrackStateChunk(t, chunk.c_str(), false))
            return {{"_error", "SetTrackStateChunk failed (malformed chunk?)"}};
        return {{"applied", true}};
    }
    if (target == "item") {
        if (!GetMediaItem || !SetItemStateChunk)
            return {{"_error", "item chunk API unavailable"}};
        if (index < 0 || index >= CountMediaItems(nullptr))
            return {{"_not_found", true}};
        MediaItem* it = GetMediaItem(nullptr, index);
        if (!it || !SetItemStateChunk(it, chunk.c_str(), false))
            return {{"_error", "SetItemStateChunk failed (malformed chunk?)"}};
        return {{"applied", true}};
    }
    if (target == "envelope") {
        if (!GetTrack || !GetTrackEnvelope || !SetEnvelopeStateChunk)
            return {{"_error", "envelope chunk API unavailable"}};
        if (index < 0 || index >= CountTracks(nullptr))
            return {{"_not_found", true}};
        MediaTrack* t = GetTrack(nullptr, index);
        if (!t)
            return {{"_not_found", true}};
        if (env_index < 0 || env_index >= CountTrackEnvelopes(t))
            return {{"_bad_request", "envelope index out of range"}};
        TrackEnvelope* env = GetTrackEnvelope(t, env_index);
        if (!env || !SetEnvelopeStateChunk(env, chunk.c_str(), false))
            return {{"_error", "SetEnvelopeStateChunk failed (malformed chunk?)"}};
        return {{"applied", true}};
    }
    return {{"_bad_request", "target must be one of: track, item, envelope"}};
}

// Translate an internal result map into the right HTTP error, or return false
// when the result is a success the caller should serialize.
bool relay_failure(httplib::Response& res, const nlohmann::json& r) {
    if (r.contains("_timeout")) {
        json_error(res, 408, "Main thread timeout", "TIMEOUT");
        return true;
    }
    if (r.contains("_error")) {
        json_error(res, 500, r["_error"].get<std::string>(), "INTERNAL_ERROR");
        return true;
    }
    if (r.contains("_not_found")) {
        json_error(res, 404, "Object index out of range", "NOT_FOUND");
        return true;
    }
    if (r.contains("_bad_request")) {
        json_error(res, 400, r["_bad_request"].get<std::string>(), "BAD_REQUEST");
        return true;
    }
    return false;
}

}  // namespace

// GET /state/chunk?target=track|item|envelope&index=N[&envelope=M]
void handle_chunk_get(const httplib::Request& req, httplib::Response& res) {
    std::string target = req.has_param("target") ? req.get_param_value("target") : "";
    if (target.empty()) {
        json_error(res, 400, "Missing required query param: target", "BAD_REQUEST");
        return;
    }
    int index = 0;
    int env_index = 0;
    try {
        index = std::stoi(req.get_param_value("index"));
    } catch (...) {
        json_error(res, 400, "Missing or invalid query param: index", "BAD_REQUEST");
        return;
    }
    if (req.has_param("envelope")) {
        try {
            env_index = std::stoi(req.get_param_value("envelope"));
        } catch (...) {
            json_error(res, 400, "Invalid query param: envelope", "BAD_REQUEST");
            return;
        }
    }

    // Chunk serialization touches REAPER state — run on the main thread. Allow a
    // longer window than the default since large chunks take time to build.
    auto result = Executor::post(
            [target, index, env_index]() -> nlohmann::json {
                return read_target(target, index, env_index);
            },
            15);

    if (relay_failure(res, result))
        return;

    json_ok(res,
            {{"target", target},
             {"index", index},
             {"envelope", env_index},
             {"chunk", result["chunk"]}});
}

// POST /state/chunk  { target, index, [envelope], chunk }
void handle_chunk_post(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (...) {
        json_error(res, 400, "Invalid JSON body", "BAD_REQUEST");
        return;
    }
    if (!body.contains("target") || !body["target"].is_string()) {
        json_error(res, 400, "Missing required field: target", "BAD_REQUEST");
        return;
    }
    if (!body.contains("index") || !body["index"].is_number_integer()) {
        json_error(res, 400, "Missing required field: index", "BAD_REQUEST");
        return;
    }
    if (!body.contains("chunk") || !body["chunk"].is_string() ||
        body["chunk"].get<std::string>().empty()) {
        json_error(res, 400, "Missing required field: chunk (non-empty RPP text)", "BAD_REQUEST");
        return;
    }
    std::string target = body["target"].get<std::string>();
    int index = body["index"].get<int>();
    int env_index = body.value("envelope", 0);
    std::string chunk = body["chunk"].get<std::string>();

    auto result = Executor::post(
            [target, index, env_index, chunk]() -> nlohmann::json {
                Undo_BeginBlock2(nullptr);
                nlohmann::json r = write_target(target, index, env_index, chunk);
                const bool changed = r.value("applied", false);
                Undo_EndBlock2(nullptr, "ReaClaw: set state chunk", changed ? -1 : 0);
                return r;
            },
            15);

    if (relay_failure(res, result))
        return;

    invalidate_state_cache();
    Log::info("Chunk applied to " + target + " " + std::to_string(index));
    json_ok(res,
            {{"target", target}, {"index", index}, {"envelope", env_index}, {"applied", true}});
}

}  // namespace ReaClaw::Handlers

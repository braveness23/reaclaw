#pragma once

// Tracks active video/audio streams (handlers/stream_video.cpp,
// handlers/stream_audio.cpp) so GET /stream/status and POST /stream/{id}/stop
// have something to act on, and so extension shutdown can ask every in-flight
// ffmpeg capture to stop before REAPER unloads the plugin.
//
// Each stream's owning httplib worker thread is the only thread that ever
// touches its util/subprocess.h Subprocess — this registry only ever sets a
// "stop requested" flag that the owning thread polls once per read cycle
// (same shape as the wall-clock bound in handlers/events.cpp), so there is no
// cross-thread process-teardown race.

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace ReaClaw::Streaming {

struct StreamInfo {
    std::string id;
    std::string kind;    // "video" | "audio"
    std::string client;  // req.remote_addr
    std::chrono::system_clock::time_point started_at;
};

class Registry {
   public:
    // Registers a running stream, returning its id. The owning handler must
    // call unregister() once its loop exits, for any reason (disconnect,
    // wall-clock bound, stop requested).
    std::string register_stream(const std::string& kind, const std::string& client);
    void unregister(const std::string& id);

    // Flags one stream to stop. Returns false if `id` isn't active.
    bool request_stop(const std::string& id);
    // Polled by the owning handler's read loop; unknown ids read as "stop"
    // so a stale/already-unregistered loop exits promptly rather than spin.
    bool stop_requested(const std::string& id);

    std::vector<StreamInfo> list();

    // Flags every active stream to stop. Called from ReaClaw::shutdown()
    // before Server::stop() so no ffmpeg child outlives the extension unload.
    void shutdown_all();

   private:
    struct Entry {
        StreamInfo info;
        bool stop = false;
    };
    std::mutex mu_;
    std::vector<Entry> entries_;
};

// Process-wide registry — one per REAPER instance, mirrors the module-level
// singletons in app.h (g_config, g_db).
Registry& instance();

}  // namespace ReaClaw::Streaming

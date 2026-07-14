#include "handlers/stream_audio.h"

#include "app.h"
#include "handlers/common.h"
#include "streaming/registry.h"
#include "util/logging.h"
#include "util/subprocess.h"
#include "util/x11_capture.h"  // have_binary/run_capture (generic process probes, not X11-specific)

#include <httplib.h>

#include <chrono>
#include <string>
#include <vector>

#include <json.hpp>

#ifndef _WIN32

namespace ReaClaw::Handlers {

namespace {

int query_int(const httplib::Request& req, const char* key, int dflt) {
    auto it = req.params.find(key);
    if (it == req.params.end())
        return dflt;
    try {
        return std::stoi(it->second);
    } catch (...) {
        return dflt;
    }
}

}  // namespace

void handle_stream_audio(const httplib::Request& req, httplib::Response& res) {
    if (!Util::have_binary(g_config.streaming_ffmpeg_path.c_str())) {
        json_error(
                res, 501, "Audio streaming requires ffmpeg (pulse input) on PATH", "TOOL_MISSING");
        return;
    }
    if (g_config.streaming_audio_monitor_source.empty()) {
        json_error(res,
                   501,
                   "streaming.audio_monitor_source is not configured — set it in config.json "
                   "to a PulseAudio monitor name (see GET /stream/audio/devices), after "
                   "routing REAPER's output to a null sink",
                   "NOT_CONFIGURED");
        return;
    }

    int bitrate = query_int(req, "bitrate", g_config.streaming_audio_bitrate_kbps);
    if (bitrate < 32)
        bitrate = 32;
    if (bitrate > 320)
        bitrate = 320;

    std::vector<std::string> argv = {g_config.streaming_ffmpeg_path,
                                     "-loglevel",
                                     "error",
                                     "-f",
                                     "pulse",
                                     "-i",
                                     g_config.streaming_audio_monitor_source,
                                     "-f",
                                     "mp3",
                                     "-b:a",
                                     std::to_string(bitrate) + "k",
                                     "pipe:1"};

    // shared_ptr, not unique_ptr: httplib::ContentProviderWithoutLength is a
    // std::function, which requires its target to be copy-constructible even
    // though only one owner ever calls it.
    std::shared_ptr<Util::Subprocess> proc = Util::Subprocess::spawn(argv);
    if (!proc) {
        json_error(res, 503, "Failed to start ffmpeg audio capture", "CAPTURE_FAILED");
        return;
    }

    std::string stream_id = Streaming::instance().register_stream("audio", req.remote_addr);
    Log::info("Audio stream started: " + stream_id + " (" + req.remote_addr + ")");

    int max_minutes = g_config.streaming_max_duration_minutes;
    res.set_header("Cache-Control", "no-cache");
    res.set_chunked_content_provider(
            "audio/mpeg",
            [proc, stream_id, max_minutes](size_t, httplib::DataSink& sink) mutable -> bool {
                auto max_duration = std::chrono::minutes(max_minutes);
                auto start = std::chrono::steady_clock::now();
                char buf[65536];
                while (std::chrono::steady_clock::now() - start < max_duration) {
                    if (!sink.is_writable())
                        return false;
                    if (Streaming::instance().stop_requested(stream_id))
                        break;
                    if (!proc->alive())
                        break;
                    long n = proc->read_some(buf, sizeof(buf));
                    if (n <= 0)
                        break;
                    if (!sink.write(buf, static_cast<size_t>(n)))
                        return false;
                }
                sink.done();
                Streaming::instance().unregister(stream_id);
                Log::info("Audio stream ended: " + stream_id);
                return false;
            });
}

void handle_stream_audio_devices(const httplib::Request&, httplib::Response& res) {
    if (!Util::have_binary("pactl")) {
        json_error(
                res, 501, "Device discovery requires pactl (PulseAudio) on PATH", "TOOL_MISSING");
        return;
    }
    std::string out;
    if (!Util::run_capture({"pactl", "list", "short", "sources"}, &out)) {
        json_error(res, 503, "pactl failed to list sources", "CAPTURE_FAILED");
        return;
    }
    nlohmann::json sources = nlohmann::json::array();
    size_t pos = 0;
    while (pos < out.size()) {
        size_t nl = out.find('\n', pos);
        std::string line = out.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? out.size() : nl + 1;
        if (line.empty())
            continue;
        // pactl short format: "<index>\t<name>\t<driver>\t<format>\t<state>"
        size_t tab1 = line.find('\t');
        size_t tab2 = tab1 == std::string::npos ? std::string::npos : line.find('\t', tab1 + 1);
        if (tab1 == std::string::npos)
            continue;
        std::string name = line.substr(
                tab1 + 1, tab2 == std::string::npos ? std::string::npos : tab2 - tab1 - 1);
        sources.push_back(
                {{"name", name},
                 {"is_monitor", name.size() > 8 && name.substr(name.size() - 8) == ".monitor"}});
    }
    json_ok(res, {{"sources", sources}});
}

}  // namespace ReaClaw::Handlers

#else  // _WIN32

namespace ReaClaw::Handlers {

void handle_stream_audio(const httplib::Request&, httplib::Response& res) {
    json_error(res,
               501,
               "Live audio streaming is implemented for Linux/PulseAudio only",
               "NOT_IMPLEMENTED");
}

void handle_stream_audio_devices(const httplib::Request&, httplib::Response& res) {
    json_error(res,
               501,
               "Live audio streaming is implemented for Linux/PulseAudio only",
               "NOT_IMPLEMENTED");
}

}  // namespace ReaClaw::Handlers

#endif

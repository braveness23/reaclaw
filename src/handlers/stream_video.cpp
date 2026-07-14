#include "handlers/stream_video.h"

#include "app.h"
#include "handlers/common.h"
#include "streaming/registry.h"
#include "util/logging.h"
#include "util/subprocess.h"
#include "util/x11_capture.h"

#include <httplib.h>

#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

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

void handle_stream_video(const httplib::Request& req, httplib::Response& res) {
    const char* disp_env = getenv("DISPLAY");
    if (!disp_env || !*disp_env) {
        json_error(res,
                   501,
                   "No GUI display available (DISPLAY unset) — video streaming needs a "
                   "running X server; this looks like a headless host",
                   "NO_DISPLAY");
        return;
    }
    std::string display = disp_env;

    if (!Util::have_binary(g_config.streaming_ffmpeg_path.c_str())) {
        json_error(res, 501, "Video streaming requires ffmpeg (x11grab) on PATH", "TOOL_MISSING");
        return;
    }

    // Resolve capture rectangle — same precedence/framing as GET /screenshot:
    // region > window > target.
    bool have_rect = false;
    int x = 0, y = 0, w = 0, h = 0;

    auto rit = req.params.find("region");
    auto wit = req.params.find("window");
    std::string target = req.params.count("target") ? req.params.find("target")->second : "screen";

    if (rit != req.params.end()) {
        if (!Util::parse_region(rit->second, x, y, w, h)) {
            json_error(res, 400, "region must be 'x,y,w,h' with positive w,h", "BAD_REQUEST");
            return;
        }
        have_rect = true;
    } else if (wit != req.params.end() || target != "screen") {
        std::string name;
        if (wit != req.params.end()) {
            name = wit->second;
        } else {
            const char* sub = Util::target_window_substr(target);
            if (!sub) {
                json_error(res,
                           400,
                           "Unknown target '" + target +
                                   "'. Use screen, arrange/reaper, mixer, fxchain, midi, "
                                   "routing, master, transport, explorer — or window=<title> "
                                   "/ region=x,y,w,h",
                           "BAD_REQUEST");
                return;
            }
            name = sub;
        }
        if (!Util::have_binary("xdotool")) {
            json_error(res,
                       501,
                       "Framing a window requires xdotool on PATH; omit window/target to "
                       "stream the whole screen",
                       "TOOL_MISSING");
            return;
        }
        if (!Util::window_geometry(name, x, y, w, h)) {
            json_error(res,
                       404,
                       "No visible window matching '" + name +
                               "' — is that surface open in REAPER?",
                       "NOT_FOUND");
            return;
        }
        have_rect = true;
    }

    // Clamp to the screen so x11grab doesn't refuse an over-the-edge region.
    if (have_rect) {
        int sw = 0, sh = 0;
        if (Util::display_geometry(sw, sh)) {
            if (x < 0)
                x = 0;
            if (y < 0)
                y = 0;
            if (x > sw - 1)
                x = sw - 1;
            if (y > sh - 1)
                y = sh - 1;
            if (x + w > sw)
                w = sw - x;
            if (y + h > sh)
                h = sh - y;
        }
    }

    int fps = query_int(req, "fps", g_config.streaming_video_fps);
    if (fps < 1)
        fps = 1;
    if (fps > 30)
        fps = 30;
    int quality = query_int(req, "quality", g_config.streaming_video_quality);
    int scale_w = query_int(req, "width", 0);

    std::vector<std::string> argv = {g_config.streaming_ffmpeg_path,
                                     "-loglevel",
                                     "error",
                                     "-f",
                                     "x11grab",
                                     "-r",
                                     std::to_string(fps)};
    if (have_rect) {
        argv.push_back("-video_size");
        argv.push_back(std::to_string(w) + "x" + std::to_string(h));
        argv.push_back("-i");
        argv.push_back(display + "+" + std::to_string(x) + "," + std::to_string(y));
    } else {
        argv.push_back("-i");
        argv.push_back(display);  // full screen (ffmpeg auto-detects size)
    }
    if (scale_w > 0) {
        argv.push_back("-vf");
        argv.push_back("scale=" + std::to_string(scale_w) + ":-1");
    }
    argv.push_back("-q:v");
    argv.push_back(std::to_string(quality));
    argv.push_back("-f");
    argv.push_back("mpjpeg");
    argv.push_back("-boundary_tag");
    argv.push_back("reaclawframe");
    argv.push_back("pipe:1");

    // shared_ptr, not unique_ptr: httplib::ContentProviderWithoutLength is a
    // std::function, which requires its target to be copy-constructible even
    // though only one owner ever calls it.
    std::shared_ptr<Util::Subprocess> proc = Util::Subprocess::spawn(argv);
    if (!proc) {
        json_error(res, 503, "Failed to start ffmpeg capture", "CAPTURE_FAILED");
        return;
    }

    std::string stream_id = Streaming::instance().register_stream("video", req.remote_addr);
    Log::info("Video stream started: " + stream_id + " (" + req.remote_addr + ")");

    int max_minutes = g_config.streaming_max_duration_minutes;
    res.set_header("Cache-Control", "no-cache");
    res.set_chunked_content_provider(
            "multipart/x-mixed-replace; boundary=reaclawframe",
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
                Log::info("Video stream ended: " + stream_id);
                return false;
            });
}

}  // namespace ReaClaw::Handlers

#else  // _WIN32

namespace ReaClaw::Handlers {

void handle_stream_video(const httplib::Request&, httplib::Response& res) {
    json_error(
            res, 501, "Live video streaming is implemented for Linux/X11 only", "NOT_IMPLEMENTED");
}

}  // namespace ReaClaw::Handlers

#endif

#pragma once

#include <httplib.h>

namespace ReaClaw::Handlers {

// GET /stream/video?target=|window=|region=&fps=&quality=&width=&token=<key>
//
// Continuous MJPEG-over-HTTP stream (multipart/x-mixed-replace) of REAPER's
// screen — open the URL directly in a browser <img> tag or a phone browser
// tab, no extra software required. Formalizes the on-demand capture in
// handlers/screenshot.h into a live feed: same target/window/region framing
// and error shapes, but backed by a persistent
// `ffmpeg -f x11grab ... -f mpjpeg` subprocess (util/subprocess.h) instead of
// a single frame. One ffmpeg process per HTTP connection — opening the URL
// starts the stream, disconnecting stops it (see ReaClaw_TECH_DECISIONS.md
// for why this codebase doesn't build a shared-capture/fan-out broadcaster).
// `?token=` auth (Auth::check_stream) exists because a browser/media-player
// tag can't set a custom Authorization header. Linux/X11 only.
void handle_stream_video(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

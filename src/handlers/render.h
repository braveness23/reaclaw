#pragma once

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib

namespace ReaClaw::Handlers {

// Epic #32 / issue #33 — first-class offline render to audio file.
//
// POST /render  {output, format, bit_depth, srate, channels, bounds,
//                start, end, normalize?}
// GET  /render/jobs (stub — async model is issue #35)
void handle_render(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

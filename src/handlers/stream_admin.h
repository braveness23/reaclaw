#pragma once

#include <httplib.h>

namespace ReaClaw::Handlers {

// Admin/observability surface for the streaming registry (streaming/
// registry.h) — a safety valve for orphaned/crashed-client cleanup, not the
// primary stream control path (opening/closing the GET /stream/video or
// GET /stream/audio URL is that).
//
//   GET  /stream/status     -> list of active video/audio streams
//   POST /stream/{id}/stop  -> flag one stream to stop; the owning handler's
//                              read loop polls this flag and exits within one
//                              read cycle.
void handle_stream_status(const httplib::Request& req, httplib::Response& res);
void handle_stream_stop(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

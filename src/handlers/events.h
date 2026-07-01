#pragma once

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib

namespace ReaClaw::Handlers {

// Issue #31 — the push event feed built on reaper/csurf.h's control-surface
// hook. Complements GET /state/changes (cheap poll token) and
// GET /snapshot/diff (fallback for clients that don't want an event feed).
//
//   GET /events?since=<cursor>&limit=   -> {cursor, events:[...]}
//   GET /events/stream                  -> Server-Sent Events, one event per line
void handle_events_list(const httplib::Request& req, httplib::Response& res);
void handle_events_stream(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

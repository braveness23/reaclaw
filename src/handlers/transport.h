#pragma once

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib

namespace ReaClaw::Handlers {

// Issue #49 — Transport verbs: play/stop/pause/record/cursor/loop.

void handle_transport_action(const httplib::Request& req,
                             httplib::Response& res);  // POST /transport
void handle_transport_cursor(const httplib::Request& req,
                             httplib::Response& res);  // POST /transport/cursor
void handle_transport_loop(const httplib::Request& req,
                           httplib::Response& res);  // POST /transport/loop

// Issue #71 — agent-friendly aliases for the common transport verbs, so a
// guessed POST /transport/play doesn't 404 with no recovery hint.
void handle_transport_play(const httplib::Request& req, httplib::Response& res);
void handle_transport_stop(const httplib::Request& req, httplib::Response& res);
void handle_transport_pause(const httplib::Request& req, httplib::Response& res);
void handle_transport_record(const httplib::Request& req, httplib::Response& res);

// Issue #67 — live transport position, bypassing the 1s state cache. Cheap
// (single main-thread read), safe to poll during playback.
void handle_transport_get(const httplib::Request& req, httplib::Response& res);  // GET /transport

}  // namespace ReaClaw::Handlers

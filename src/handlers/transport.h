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

}  // namespace ReaClaw::Handlers

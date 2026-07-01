#pragma once

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib

namespace ReaClaw::Handlers {

void handle_execute_action(const httplib::Request& req, httplib::Response& res);
void handle_execute_sequence(const httplib::Request& req, httplib::Response& res);

// Issue #69 — one-shot script execution: register + run + (by default)
// deregister in a single call, for short throw-away scripts.
void handle_execute_script(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

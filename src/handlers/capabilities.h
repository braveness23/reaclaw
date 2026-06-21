#pragma once

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib

namespace ReaClaw::Handlers {

// GET /capabilities — manifest of what the API supports directly vs. via scripts.
void handle_capabilities(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

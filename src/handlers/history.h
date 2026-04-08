#pragma once

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib

namespace ReaClaw::Handlers {

void handle_history(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

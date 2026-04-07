#pragma once

namespace httplib { struct Request; struct Response; }

namespace ReaClaw::Handlers {

void handle_execute_action(const httplib::Request& req, httplib::Response& res);
void handle_execute_sequence(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

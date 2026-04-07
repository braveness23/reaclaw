#pragma once

namespace httplib { struct Request; struct Response; }

namespace ReaClaw::Handlers {

void handle_state(const httplib::Request& req, httplib::Response& res);
void handle_state_tracks(const httplib::Request& req, httplib::Response& res);
void handle_state_set_track(const httplib::Request& req, httplib::Response& res);
void handle_state_items(const httplib::Request& req, httplib::Response& res);
void handle_state_selection(const httplib::Request& req, httplib::Response& res);
void handle_state_automation(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

#pragma once

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib

namespace ReaClaw::Handlers {

void handle_state(const httplib::Request& req, httplib::Response& res);
void handle_state_tracks(const httplib::Request& req, httplib::Response& res);
void handle_state_set_track(const httplib::Request& req, httplib::Response& res);
void handle_state_items(const httplib::Request& req, httplib::Response& res);
void handle_state_selection(const httplib::Request& req, httplib::Response& res);
void handle_state_automation(const httplib::Request& req, httplib::Response& res);

// Phase 4 Stage 2 — high-level structured verbs.
void handle_state_tracks_post(const httplib::Request& req, httplib::Response& res);
void handle_state_delete_track(const httplib::Request& req, httplib::Response& res);
void handle_state_add_fx(const httplib::Request& req, httplib::Response& res);
void handle_state_get_fx(const httplib::Request& req, httplib::Response& res);
void handle_state_set_fx(const httplib::Request& req, httplib::Response& res);
void handle_state_delete_fx(const httplib::Request& req, httplib::Response& res);
void handle_state_add_send(const httplib::Request& req, httplib::Response& res);
void handle_state_delete_send(const httplib::Request& req, httplib::Response& res);
void handle_state_set_selection(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

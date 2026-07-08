#pragma once

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib

namespace ReaClaw::Handlers {

// Track-FX verbs (Phase 4 Stage 2 + Epics #16/#17).
void handle_state_add_fx(const httplib::Request& req, httplib::Response& res);
void handle_state_get_fx(const httplib::Request& req, httplib::Response& res);
void handle_state_set_fx(const httplib::Request& req, httplib::Response& res);
void handle_state_delete_fx(const httplib::Request& req, httplib::Response& res);
void handle_fx_copy(const httplib::Request& req, httplib::Response& res);
void handle_fx_get_preset(const httplib::Request& req, httplib::Response& res);
void handle_fx_set_preset(const httplib::Request& req, httplib::Response& res);
void handle_fx_get_pins(const httplib::Request& req, httplib::Response& res);
void handle_fx_set_pins(const httplib::Request& req, httplib::Response& res);

// Take-FX verbs (issue #50) — mirror of the track-FX surface for item takes.
void handle_take_add_fx(const httplib::Request& req, httplib::Response& res);
void handle_take_get_fx(const httplib::Request& req, httplib::Response& res);
void handle_take_set_fx(const httplib::Request& req, httplib::Response& res);
void handle_take_delete_fx(const httplib::Request& req, httplib::Response& res);
void handle_take_copy_fx(const httplib::Request& req, httplib::Response& res);
void handle_take_get_fx_preset(const httplib::Request& req, httplib::Response& res);
void handle_take_set_fx_preset(const httplib::Request& req, httplib::Response& res);
void handle_take_get_pins(const httplib::Request& req, httplib::Response& res);
void handle_take_set_pins(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

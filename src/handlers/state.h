#pragma once

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib

namespace ReaClaw::Handlers {

void handle_state(const httplib::Request& req, httplib::Response& res);
void handle_state_tracks(const httplib::Request& req, httplib::Response& res);
void handle_state_set_track(const httplib::Request& req, httplib::Response& res);
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
void handle_state_set_send(const httplib::Request& req, httplib::Response& res);
void handle_state_delete_send(const httplib::Request& req, httplib::Response& res);
void handle_state_set_selection(const httplib::Request& req, httplib::Response& res);

// Epic #16 — Tier-A control verbs.
void handle_fx_get_preset(const httplib::Request& req, httplib::Response& res);
void handle_fx_set_preset(const httplib::Request& req, httplib::Response& res);
void handle_automation_write(const httplib::Request& req, httplib::Response& res);

// Epic #17 — Tier-B content manipulation.
void handle_fx_copy(const httplib::Request& req, httplib::Response& res);

// Issue #29 — Track icons.
void handle_state_track_icons(const httplib::Request& req, httplib::Response& res);

// Drop the 1s state read-cache (tracks/state/items) so the next read reflects a
// just-applied write. Exported for cross-handler use (e.g. the chunk backstop).
void invalidate_state_cache();

}  // namespace ReaClaw::Handlers

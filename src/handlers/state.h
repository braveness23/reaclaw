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

// Phase 4 Stage 2 — high-level structured verbs. (FX verbs live in fx.h.)
void handle_state_tracks_post(const httplib::Request& req, httplib::Response& res);
void handle_state_delete_track(const httplib::Request& req, httplib::Response& res);
void handle_state_add_send(const httplib::Request& req, httplib::Response& res);
void handle_state_set_send(const httplib::Request& req, httplib::Response& res);
void handle_state_delete_send(const httplib::Request& req, httplib::Response& res);
void handle_state_set_selection(const httplib::Request& req, httplib::Response& res);

// Epic #16 — Tier-A control verbs.
void handle_automation_write(const httplib::Request& req, httplib::Response& res);

// Issue #29 — Track icons.
void handle_state_track_icons(const httplib::Request& req, httplib::Response& res);

// Issue #31 — Change token: cheap monotonic poll to detect external edits.
void handle_state_changes(const httplib::Request& req, httplib::Response& res);

// Drop the 1s state read-cache (tracks/state/items) so the next read reflects a
// just-applied write. Exported for cross-handler use (e.g. the chunk backstop).
void invalidate_state_cache();

}  // namespace ReaClaw::Handlers

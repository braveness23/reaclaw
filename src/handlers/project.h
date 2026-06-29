#pragma once

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib

namespace ReaClaw::Handlers {

// Epic #16 — project-level Tier-A verbs.

// Undo / redo (mutations are wrapped in undo blocks; these drive the stack).
void handle_undo_state(const httplib::Request& req, httplib::Response& res);  // GET  /undo
void handle_undo(const httplib::Request& req, httplib::Response& res);        // POST /undo
void handle_redo(const httplib::Request& req, httplib::Response& res);        // POST /redo

// Project extras: dirty flag, length, notes scratchpad.
void handle_project_get(const httplib::Request& req, httplib::Response& res);  // GET  /project
void handle_project_notes(const httplib::Request& req,
                          httplib::Response& res);  // POST /project/notes

// Markers & regions.
void handle_markers_get(const httplib::Request& req, httplib::Response& res);  // GET /state/markers
void handle_markers_post(const httplib::Request& req,
                         httplib::Response& res);  // POST   /state/markers
void handle_markers_delete(const httplib::Request& req,
                           httplib::Response& res);  // DELETE /state/markers/{id}

// Tempo / time-signature map.
void handle_tempo_get(const httplib::Request& req, httplib::Response& res);   // GET  /state/tempo
void handle_tempo_post(const httplib::Request& req, httplib::Response& res);  // POST /state/tempo

// Beat <-> time conversion utilities.
void handle_time_convert(const httplib::Request& req, httplib::Response& res);  // GET /time

// Epic #17 — per-project ext state (persistent agent scratchpad in the .rpp).
void handle_extstate_get(const httplib::Request& req,
                         httplib::Response& res);  // GET    /project/extstate
void handle_extstate_post(const httplib::Request& req,
                          httplib::Response& res);  // POST   /project/extstate
void handle_extstate_delete(const httplib::Request& req,
                            httplib::Response& res);  // DELETE /project/extstate

// Issue #34 — project lifecycle: new / open / save / reset.
void handle_project_new(const httplib::Request& req,
                        httplib::Response& res);    // POST /project/new
void handle_project_open(const httplib::Request& req,
                         httplib::Response& res);   // POST /project/open
void handle_project_save(const httplib::Request& req,
                         httplib::Response& res);   // POST /project/save
void handle_project_reset(const httplib::Request& req,
                          httplib::Response& res);  // POST /project/reset

}  // namespace ReaClaw::Handlers

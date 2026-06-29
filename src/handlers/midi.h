#pragma once

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib

namespace ReaClaw::Handlers {

// Issue #51 — MIDI verbs: structured read/write for MIDI notes and CC events.
// Items are addressed by project-wide index (same indexing as GET /state/items).
// The active take of the item must be a MIDI take; non-MIDI items return 400.
void handle_midi_get(const httplib::Request& req, httplib::Response& res);
void handle_midi_post(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

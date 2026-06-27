#pragma once

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib

namespace ReaClaw::Handlers {

// GET /state/chunk  — read the full RPP state chunk of a track/item/envelope.
// POST /state/chunk — write a full RPP state chunk back.
//
// The universal reachability backstop (issue #48): any object property REAPER
// serializes into its .rpp chunk is readable and writable here even when no
// dedicated structured verb exists. Together with the action-runner and the Lua
// escape hatch, this makes the automation surface provably 100% reachable.
void handle_chunk_get(const httplib::Request& req, httplib::Response& res);
void handle_chunk_post(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

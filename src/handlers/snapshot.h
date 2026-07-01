#pragma once

#include <json.hpp>

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib

namespace ReaClaw::Handlers {

// Epic #20 prep — shared snapshot / state-diff layer.
//
// Capture a canonical, diffable snapshot of project state, store it, and diff
// two snapshots (or a snapshot vs. live). This is the cross-cutting layer the
// roadmap calls for: it backs the #19 A/B visual diff and the #20 correction
// mining ("what did the agent change, and was it corrected").

// Build the canonical state snapshot. Main-thread only (reads the REAPER SDK).
nlohmann::json capture_state();

void handle_snapshot_create(const httplib::Request& req, httplib::Response& res);  // POST /snapshot
void handle_snapshot_list(const httplib::Request& req, httplib::Response& res);    // GET /snapshot
void handle_snapshot_get(const httplib::Request& req,
                         httplib::Response& res);  // GET /snapshot/{id}
void handle_snapshot_delete(const httplib::Request& req,
                            httplib::Response& res);  // DELETE /snapshot/{id}
void handle_snapshot_diff(const httplib::Request& req,
                          httplib::Response& res);  // GET /snapshot/diff?from=&to=

// Issue #53 — A/B visual diff. Both snapshots need an `audio` target
// (POST /snapshot's optional audio:{item|file}). Reuses build_file_visualization
// (visualize.h) for both sides and jsondiff for the digest delta.
//   GET /snapshot/diff/visualize?from=&to=&type=spectrum|waveform|loudness&width=&height=&image=
void handle_snapshot_diff_visualize(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

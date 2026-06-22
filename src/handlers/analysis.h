#pragma once

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib

namespace ReaClaw::Handlers {

// Epic #18 — audio perception ("the agent hears itself").
//
// Built-in, always-available analysis: loudness/peak/true-peak/RMS (exact, via
// REAPER's offline CalculateNormalization), a rough 3-band spectral digest
// (estimated DSP over decoded samples), and clipping derived from true-peak.
// Every result is tagged with a `method` and `confidence` so the agent knows how
// much to trust each number.
void handle_analysis_item(const httplib::Request& req, httplib::Response& res);  // GET /analysis/item/{index}
void handle_analysis_file(const httplib::Request& req, httplib::Response& res);  // GET /analysis/file?path=

// Live per-track peak metering (introspection of REAPER's own meters; only
// meaningful while audio is running).
void handle_meters(const httplib::Request& req, httplib::Response& res);  // GET /state/meters

}  // namespace ReaClaw::Handlers

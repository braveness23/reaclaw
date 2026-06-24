#pragma once

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib

namespace ReaClaw::Handlers {

// Epic #19 / Q7 — musical-attribute probes (key / tempo / pitch).
//
// A "probe" is the measure-counterpart of an action: it reads the material and
// returns data instead of changing the project. Each result is tagged with its
// truth source — exact `introspection` (the project already knows it, no render)
// vs. `estimated_dsp` (decoded + analysed, carries confidence). Advanced
// detectors may use an optional external tool; when it is absent the probe
// degrades gracefully to the built-in estimate or reports unavailability rather
// than failing.
void handle_probe_item(const httplib::Request& req,
                       httplib::Response& res);  // GET /analysis/item/{index}/probe
void handle_probe_file(const httplib::Request& req,
                       httplib::Response& res);  // GET /analysis/file/probe?path=

}  // namespace ReaClaw::Handlers

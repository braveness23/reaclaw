#pragma once

#include <httplib.h>

#include <string>

#include <json.hpp>

namespace ReaClaw::Handlers {

// Audio visualization (Epic #19 / Q4). Renders a picture of an audio source —
// spectrum (EQ-style curve), waveform, or loudness contour — and returns it as
// a base64 PNG *alongside* a machine-readable digest, so the agent reads numbers
// rather than OCR-ing pixels. Built on the same offline decode + FFT used by the
// analysis handlers; every result is tagged with `method` + `confidence`.
//
//   GET /analysis/item/{index}/visualize?type=spectrum|waveform|loudness
//                                        &start=&end=&width=&height=&image=png|none
//   GET /analysis/file/visualize?path=ABS&type=...&...
void handle_visualize_item(const httplib::Request& req, httplib::Response& res);
void handle_visualize_file(const httplib::Request& req, httplib::Response& res);

// Shared core behind handle_visualize_file — exposed so other handlers (the
// snapshot A/B diff, issue #53) can build the exact same visualize-shaped
// result for a file without going through an HTTP request. Main-thread only
// (PCM_Source_CreateFromFile + REAPER SDK decode) — call from inside
// Executor::post. Returns the same shape as GET /analysis/file/visualize, or
// an error-sentinel object (`_not_found` / `_bad_request`) on failure.
nlohmann::json build_file_visualization(const std::string& path,
                                        const std::string& type_str,
                                        double start,
                                        double end,
                                        int width,
                                        int height,
                                        bool image);

}  // namespace ReaClaw::Handlers

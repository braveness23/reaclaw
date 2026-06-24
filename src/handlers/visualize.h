#pragma once

#include <httplib.h>

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

}  // namespace ReaClaw::Handlers

#pragma once

#include <httplib.h>

namespace ReaClaw::Handlers {

// On-demand screenshot (Epic #19 / Q5). The guiding principle is structure-first:
// the agent should reach for /state reads, and screenshot only for GUI-only state
// a structured read can't express (a plugin's custom GUI, a meter display). This
// moves the proven `ffmpeg -f x11grab` recipe server-side so the agent doesn't
// have to do the capture dance, and adds named-surface framing + downscale.
//
//   GET /screenshot?target=screen|reaper        (default screen)
//                  &window=<title-substring>     (frame any window by X11 title)
//                  &region=x,y,w,h               (explicit crop; overrides target)
//                  &width=<px>                    (downscale to width, 0 = native)
//
// Linux/SWELL only for now: shells out to ffmpeg (capture) and xdotool (window
// geometry). Returns 501 with a clear message when those aren't available, in
// keeping with the "advanced features are optional, degrade gracefully" decision.
void handle_screenshot(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

#pragma once

// X11/window-framing helpers shared by handlers/screenshot.cpp (one-shot
// capture) and handlers/stream_video.cpp (continuous capture). Linux only —
// all functions are no-ops (return false/nullptr) on Windows.

#include <string>
#include <vector>

namespace ReaClaw::Util {

// Run argv[0] with no shell (so caller-supplied strings can't inject),
// capturing stdout into `out` (may be null to discard). Returns true on exit
// status 0. Blocking — only for short-lived probes (`xdotool`, `which`), not
// for the persistent capture pipelines in util/subprocess.h.
bool run_capture(const std::vector<std::string>& argv, std::string* out);

bool have_binary(const char* name);

// Parse "x,y,w,h" into four non-negative ints. Returns false if malformed.
bool parse_region(const std::string& s, int& x, int& y, int& w, int& h);

// Geometry of the largest X11 window whose title matches `name_substr`, via
// xdotool. "Largest" skips the tiny SWELL/helper windows that share the title
// and picks the real surface. Returns false if none found.
bool window_geometry(const std::string& name_substr, int& x, int& y, int& w, int& h);

// Full display size (via xdotool), used to clamp capture rectangles to the
// screen — a maximized window's geometry can overrun the display edge, and
// x11grab refuses any region that falls outside the screen.
bool display_geometry(int& w, int& h);

// Map a friendly named surface to the X11 window-title substring REAPER gives
// that surface. Lets a caller ask for `target=mixer` instead of guessing
// window titles. Returns nullptr for an unknown name.
const char* target_window_substr(const std::string& t);

}  // namespace ReaClaw::Util

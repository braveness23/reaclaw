#include "handlers/screenshot.h"

#include "handlers/common.h"
#include "util/image.h"
#include "util/logging.h"

#include <httplib.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <json.hpp>

#ifndef _WIN32

#include <array>
#include <fstream>

#include <fcntl.h>
#include <unistd.h>

#include <sys/wait.h>

namespace ReaClaw::Handlers {

namespace {

// Run argv[0] with no shell (so query-string values can't inject), capturing
// stdout into `out`. Returns true on exit status 0. `out` may be null.
bool run(const std::vector<std::string>& argv, std::string* out) {
    int pipefd[2];
    if (pipe(pipefd) != 0)
        return false;
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }
    if (pid == 0) {
        // Child: stdout → pipe, stderr → /dev/null.
        dup2(pipefd[1], STDOUT_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0)
            dup2(devnull, STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        std::vector<char*> cargv;
        for (const auto& a : argv)
            cargv.push_back(const_cast<char*>(a.c_str()));
        cargv.push_back(nullptr);
        execvp(cargv[0], cargv.data());
        _exit(127);  // execvp failed (binary not found)
    }
    // Parent.
    close(pipefd[1]);
    if (out) {
        std::array<char, 4096> buf;
        ssize_t n;
        while ((n = read(pipefd[0], buf.data(), buf.size())) > 0)
            out->append(buf.data(), static_cast<size_t>(n));
    } else {
        char tmp[4096];
        while (read(pipefd[0], tmp, sizeof(tmp)) > 0) {
        }
    }
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool have_binary(const char* name) {
    return run({"which", name}, nullptr);
}

// Parse "x,y,w,h" into four non-negative ints. Returns false if malformed.
bool parse_region(const std::string& s, int& x, int& y, int& w, int& h) {
    return sscanf(s.c_str(), "%d,%d,%d,%d", &x, &y, &w, &h) == 4 && w > 0 && h > 0 && x >= 0 &&
           y >= 0;
}

// Width/height of a PNG from its IHDR (bytes 16..23, big-endian).
bool png_dims(const std::vector<uint8_t>& png, int& w, int& h) {
    if (png.size() < 24)
        return false;
    w = (png[16] << 24) | (png[17] << 16) | (png[18] << 8) | png[19];
    h = (png[20] << 24) | (png[21] << 16) | (png[22] << 8) | png[23];
    return w > 0 && h > 0;
}

// Geometry of the largest X11 window whose title matches `name_substr`, via
// xdotool. "Largest" skips the tiny SWELL/helper windows that share the title and
// picks the real surface. Returns false if none found.
bool window_geometry(const std::string& name_substr, int& x, int& y, int& w, int& h) {
    std::string ids;
    if (!run({"xdotool", "search", "--name", name_substr}, &ids) || ids.empty())
        return false;

    auto field = [](const std::string& geo, const char* key) -> int {
        auto p = geo.find(key);
        if (p == std::string::npos)
            return -1;
        return std::atoi(geo.c_str() + p + std::char_traits<char>::length(key));
    };

    long best_area = 0;
    bool found = false;
    size_t pos = 0;
    while (pos < ids.size()) {
        size_t nl = ids.find('\n', pos);
        std::string id = ids.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? ids.size() : nl + 1;
        if (id.empty())
            continue;
        std::string geo;
        if (!run({"xdotool", "getwindowgeometry", "--shell", id}, &geo))
            continue;
        int gx = field(geo, "\nX="), gy = field(geo, "\nY=");
        int gw = field(geo, "WIDTH="), gh = field(geo, "HEIGHT=");
        if (gw <= 0 || gh <= 0)
            continue;
        long area = static_cast<long>(gw) * gh;
        if (area > best_area) {
            best_area = area;
            x = gx < 0 ? 0 : gx;
            y = gy < 0 ? 0 : gy;
            w = gw;
            h = gh;
            found = true;
        }
    }
    return found;
}

// Full display size (via xdotool), used to clamp capture rectangles to the
// screen — a maximized window's geometry can overrun the display edge, and
// x11grab refuses any region that falls outside the screen.
bool display_geometry(int& w, int& h) {
    std::string out;
    if (!run({"xdotool", "getdisplaygeometry"}, &out))
        return false;
    return sscanf(out.c_str(), "%d %d", &w, &h) == 2 && w > 0 && h > 0;
}

int query_int(const httplib::Request& req, const char* key, int dflt) {
    auto it = req.params.find(key);
    if (it == req.params.end())
        return dflt;
    try {
        return std::stoi(it->second);
    } catch (...) {
        return dflt;
    }
}

// Map a friendly named surface to the X11 window-title substring REAPER gives
// that surface. Lets an agent ask for `target=mixer` instead of guessing window
// titles. Returns nullptr for an unknown name (so the caller can 400 with the
// valid list). `screen` is handled earlier (no window framing).
const char* target_window_substr(const std::string& t) {
    if (t == "reaper" || t == "arrange" || t == "main")
        return "REAPER";
    if (t == "mixer")
        return "Mixer";
    if (t == "fx" || t == "fxchain" || t == "plugin")
        return "FX:";
    if (t == "midi")
        return "MIDI";
    if (t == "routing")
        return "Routing";
    if (t == "master")
        return "Master";
    if (t == "transport")
        return "Transport";
    if (t == "explorer" || t == "mediaexplorer")
        return "Media Explorer";
    return nullptr;
}

}  // namespace

void handle_screenshot(const httplib::Request& req, httplib::Response& res) {
    const char* disp_env = getenv("DISPLAY");
    if (!disp_env || !*disp_env) {
        json_error(res,
                   501,
                   "No GUI display available (DISPLAY unset) — screenshots need a "
                   "running X server; this looks like a headless host",
                   "NO_DISPLAY");
        return;
    }
    std::string display = disp_env;

    if (!have_binary("ffmpeg")) {
        json_error(res,
                   501,
                   "Screenshot requires ffmpeg (x11grab) on PATH — install it or use "
                   "structured /state reads instead",
                   "TOOL_MISSING");
        return;
    }

    // Resolve capture rectangle. Precedence: region > window > target.
    bool have_rect = false;
    int x = 0, y = 0, w = 0, h = 0;
    std::string framed = "screen";

    auto rit = req.params.find("region");
    auto wit = req.params.find("window");
    std::string target = req.params.count("target") ? req.params.find("target")->second : "screen";

    if (rit != req.params.end()) {
        if (!parse_region(rit->second, x, y, w, h)) {
            json_error(res, 400, "region must be 'x,y,w,h' with positive w,h", "BAD_REQUEST");
            return;
        }
        have_rect = true;
        framed = "region";
    } else if (wit != req.params.end() || target != "screen") {
        // Resolve a window-title substring: explicit `window=` wins, else map the
        // named surface target (mixer / fxchain / midi / routing / master / …).
        std::string name;
        if (wit != req.params.end()) {
            name = wit->second;
            framed = "window:" + name;
        } else {
            const char* sub = target_window_substr(target);
            if (!sub) {
                json_error(res,
                           400,
                           "Unknown target '" + target +
                                   "'. Use screen, arrange/reaper, mixer, fxchain, midi, "
                                   "routing, master, transport, explorer — or window=<title> "
                                   "/ region=x,y,w,h",
                           "BAD_REQUEST");
                return;
            }
            name = sub;
            framed = target;
        }
        if (!have_binary("xdotool")) {
            json_error(res,
                       501,
                       "Framing a window requires xdotool on PATH; omit window/target to "
                       "grab the whole screen",
                       "TOOL_MISSING");
            return;
        }
        if (!window_geometry(name, x, y, w, h)) {
            json_error(res,
                       404,
                       "No visible window matching '" + name +
                               "' — is that surface open in REAPER?",
                       "NOT_FOUND");
            return;
        }
        have_rect = true;
    }

    // Clamp the capture rectangle to the screen so x11grab doesn't refuse an
    // over-the-edge region (e.g. a maximized window's full geometry).
    if (have_rect) {
        int sw = 0, sh = 0;
        if (display_geometry(sw, sh)) {
            if (x < 0)
                x = 0;
            if (y < 0)
                y = 0;
            if (x > sw - 1)
                x = sw - 1;
            if (y > sh - 1)
                y = sh - 1;
            if (x + w > sw)
                w = sw - x;
            if (y + h > sh)
                h = sh - y;
        }
    }

    int scale_w = query_int(req, "width", 0);

    // Build the ffmpeg argv. Single-frame x11grab to a temp PNG.
    std::string tmp = "/tmp/reaclaw_shot_" + std::to_string(getpid()) + "_" + now_iso().substr(11) +
                      ".png";
    for (char& c : tmp)
        if (c == ':')
            c = '-';

    std::vector<std::string> argv = {"ffmpeg", "-loglevel", "error", "-f", "x11grab"};
    if (have_rect) {
        argv.push_back("-video_size");
        argv.push_back(std::to_string(w) + "x" + std::to_string(h));
        argv.push_back("-i");
        argv.push_back(display + "+" + std::to_string(x) + "," + std::to_string(y));
    } else {
        argv.push_back("-i");
        argv.push_back(display);  // full screen (ffmpeg auto-detects size)
    }
    argv.push_back("-frames:v");
    argv.push_back("1");
    if (scale_w > 0) {
        argv.push_back("-vf");
        argv.push_back("scale=" + std::to_string(scale_w) + ":-1");
    }
    argv.push_back("-y");
    argv.push_back(tmp);

    bool ok = run(argv, nullptr);
    std::vector<uint8_t> png;
    if (ok) {
        std::ifstream f(tmp, std::ios::binary);
        png.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }
    std::remove(tmp.c_str());

    if (!ok || png.empty()) {
        json_error(res,
                   503,
                   "Screen capture failed — ffmpeg could not grab the display (is the X "
                   "session reachable?)",
                   "CAPTURE_FAILED");
        return;
    }

    int iw = 0, ih = 0;
    png_dims(png, iw, ih);
    nlohmann::json out = {
            {"framed", framed},
            {"display", display},
            {"image",
             {{"format", "png"}, {"width", iw}, {"height", ih}, {"base64", Image::base64(png)}}},
            {"note",
             "structure-first: prefer /state reads; use a screenshot only for "
             "GUI-only state structured data can't express"}};
    if (have_rect)
        out["region"] = {{"x", x}, {"y", y}, {"w", w}, {"h", h}};
    json_ok(res, out);
}

}  // namespace ReaClaw::Handlers

#else  // _WIN32 — capture is implemented via the x11grab path on Linux only.

namespace ReaClaw::Handlers {

void handle_screenshot(const httplib::Request&, httplib::Response& res) {
    json_error(res,
               501,
               "Built-in screenshot is implemented for Linux/X11 only; on this platform "
               "capture the screen out-of-band",
               "NOT_IMPLEMENTED");
}

}  // namespace ReaClaw::Handlers

#endif

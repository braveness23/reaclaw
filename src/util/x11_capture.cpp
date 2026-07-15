#include "util/x11_capture.h"

#include <cstdio>
#include <cstdlib>

#ifndef _WIN32

#include <array>

#include <fcntl.h>
#include <unistd.h>

#include <sys/wait.h>

namespace ReaClaw::Util {

bool run_capture(const std::vector<std::string>& argv, std::string* out) {
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
        // Child: stdout -> pipe, stderr -> /dev/null.
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
    return run_capture({"which", name}, nullptr);
}

bool parse_region(const std::string& s, int& x, int& y, int& w, int& h) {
    return sscanf(s.c_str(), "%d,%d,%d,%d", &x, &y, &w, &h) == 4 && w > 0 && h > 0 && x >= 0 &&
           y >= 0;
}

bool window_geometry(const std::string& name_substr, int& x, int& y, int& w, int& h) {
    std::string ids;
    if (!run_capture({"xdotool", "search", "--name", name_substr}, &ids) || ids.empty())
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
        if (!run_capture({"xdotool", "getwindowgeometry", "--shell", id}, &geo))
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

bool display_geometry(int& w, int& h) {
    std::string out;
    if (!run_capture({"xdotool", "getdisplaygeometry"}, &out))
        return false;
    return sscanf(out.c_str(), "%d %d", &w, &h) == 2 && w > 0 && h > 0;
}

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

}  // namespace ReaClaw::Util

#else  // _WIN32 — X11 capture is Linux-only.

namespace ReaClaw::Util {

bool run_capture(const std::vector<std::string>&, std::string*) {
    return false;
}
bool have_binary(const char*) {
    return false;
}
bool parse_region(const std::string&, int&, int&, int&, int&) {
    return false;
}
bool window_geometry(const std::string&, int&, int&, int&, int&) {
    return false;
}
bool display_geometry(int&, int&) {
    return false;
}
const char* target_window_substr(const std::string&) {
    return nullptr;
}

}  // namespace ReaClaw::Util

#endif

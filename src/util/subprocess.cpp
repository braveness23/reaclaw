#include "util/subprocess.h"

#ifndef _WIN32

#include <csignal>

#include <fcntl.h>
#include <unistd.h>

#include <sys/wait.h>

namespace ReaClaw::Util {

std::unique_ptr<Subprocess> Subprocess::spawn(const std::vector<std::string>& argv) {
    int pipefd[2];
    if (pipe(pipefd) != 0)
        return nullptr;
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return nullptr;
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
    return std::unique_ptr<Subprocess>(new Subprocess(static_cast<long>(pid), pipefd[0]));
}

Subprocess::Subprocess(long pid, int stdout_fd) : pid_(pid), stdout_fd_(stdout_fd) {
}

Subprocess::~Subprocess() {
    if (stdout_fd_ >= 0)
        close(stdout_fd_);
    if (reaped_)
        return;
    pid_t pid = static_cast<pid_t>(pid_);
    // Ask nicely, give it ~200ms, then insist — a stuck ffmpeg must never
    // outlive the HTTP connection (or the extension) that started it.
    kill(pid, SIGTERM);
    for (int i = 0; i < 20; i++) {
        int status = 0;
        if (waitpid(pid, &status, WNOHANG) == pid) {
            reaped_ = true;
            return;
        }
        usleep(10000);
    }
    kill(pid, SIGKILL);
    int status = 0;
    waitpid(pid, &status, 0);
    reaped_ = true;
}

long Subprocess::read_some(char* buf, size_t len) {
    return static_cast<long>(read(stdout_fd_, buf, len));
}

bool Subprocess::alive() {
    if (reaped_)
        return false;
    int status = 0;
    pid_t r = waitpid(static_cast<pid_t>(pid_), &status, WNOHANG);
    if (r == 0)
        return true;
    reaped_ = true;
    return false;
}

}  // namespace ReaClaw::Util

#else  // _WIN32 — streaming capture is Linux/X11(+Pulse)-only for now.

namespace ReaClaw::Util {

std::unique_ptr<Subprocess> Subprocess::spawn(const std::vector<std::string>&) {
    return nullptr;
}

Subprocess::Subprocess(long pid, int stdout_fd) : pid_(pid), stdout_fd_(stdout_fd) {
}
Subprocess::~Subprocess() {
}
long Subprocess::read_some(char*, size_t) {
    return -1;
}
bool Subprocess::alive() {
    return false;
}

}  // namespace ReaClaw::Util

#endif

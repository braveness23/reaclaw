#include "server/server.h"
#include "server/router.h"
#include "util/tls.h"
#include "util/logging.h"
#include "config/config.h"

#include <httplib.h>  // CPPHTTPLIB_OPENSSL_SUPPORT is set via CMake compile definitions

#include <memory>
#include <string>
#include <thread>

namespace ReaClaw::Server {

namespace {

std::unique_ptr<httplib::SSLServer> g_server;
std::thread                         g_thread;

}  // namespace

bool start(const Config& cfg) {
    // Resolve or generate TLS cert/key
    const std::string& cert = cfg.resolved_cert_path;
    const std::string& key  = cfg.resolved_key_path;

    if (!TLS::files_exist(cert, key)) {
        if (!cfg.tls_generate_if_missing) {
            Log::error("TLS cert/key not found and generate_if_missing is false. "
                       "Set cert_file/key_file in config or enable generate_if_missing.");
            return false;
        }
        if (!TLS::generate_self_signed(cert, key)) {
            Log::error("Failed to generate self-signed TLS certificate");
            return false;
        }
    }

    g_server = std::make_unique<httplib::SSLServer>(cert.c_str(), key.c_str());
    if (!g_server->is_valid()) {
        Log::error("Failed to create SSLServer — check cert/key files");
        g_server.reset();
        return false;
    }

    g_server->new_task_queue = [&cfg]() -> httplib::TaskQueue* {
        return new httplib::ThreadPool(cfg.thread_pool_size);
    };

    // Register all API routes
    Router::register_routes(*g_server, cfg);

    // Start listening on a background thread
    const std::string host = cfg.host;
    const int         port = cfg.port;

    g_thread = std::thread([host, port]() {
        Log::info("HTTPS server listening on " + host + ":" + std::to_string(port));
        g_server->listen(host.c_str(), port);
        Log::info("HTTPS server stopped");
    });

    return true;
}

void stop() {
    if (g_server) {
        g_server->stop();
    }
    if (g_thread.joinable()) {
        g_thread.join();
    }
    g_server.reset();
}

}  // namespace ReaClaw::Server

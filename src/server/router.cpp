#include "server/router.h"

#include "app.h"
#include "auth/auth.h"
#include "config/config.h"
#include "handlers/catalog.h"
#include "handlers/common.h"
#include "handlers/execute.h"
#include "handlers/history.h"
#include "handlers/scripts.h"
#include "handlers/state.h"
#include "reaper/executor.h"
#include "server/server.h"

#include <httplib.h>  // CPPHTTPLIB_OPENSSL_SUPPORT is set via CMake compile definitions

#include <json.hpp>

// REAPER SDK — extern declarations for GetAppVersion (defined in api.cpp)
#include <chrono>
#include <string>

#include <reaper_plugin_functions.h>

namespace ReaClaw::Router {

namespace {

// Wrap a handler with auth checking.
using H = std::function<void(const httplib::Request&, httplib::Response&)>;

H auth_wrap(const Config& cfg, H handler) {
    return [cfg, handler](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");
        res.set_header("Strict-Transport-Security", "max-age=31536000");
        if (!Auth::check(cfg, req)) {
            Auth::reject(res);
            return;
        }
        handler(req, res);
    };
}

}  // namespace

void register_routes(httplib::SSLServer& svr, const Config& cfg) {
    // --- Health ---
    svr.Get("/health", auth_wrap(cfg, [](const httplib::Request&, httplib::Response& res) {
                auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                                      std::chrono::steady_clock::now() - g_start_time)
                                      .count();
                int catalog_size = static_cast<int>(
                        g_db.scalar_int("SELECT COUNT(*) FROM actions"));

                const char* rv = GetAppVersion ? GetAppVersion() : "unknown";

                Handlers::json_ok(res,
                                  {{"status", "ok"},
                                   {"version", REACLAW_VERSION},
                                   {"reaper_version", rv ? rv : ""},
                                   {"catalog_size", catalog_size},
                                   {"uptime_seconds", uptime},
                                   {"queue_depth", static_cast<int>(Executor::queue_depth())},
                                   {"db_ok", g_db.is_open()},
                                   {"server_running", Server::is_running()}});
            }));

    // --- Catalog ---
    svr.Get("/catalog/search", auth_wrap(cfg, Handlers::handle_catalog_search));
    svr.Get("/catalog/categories", auth_wrap(cfg, Handlers::handle_catalog_categories));
    svr.Get(R"(/catalog/(\d+))",
            auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                // httplib regex capture → inject as path_params for handler compatibility
                const_cast<httplib::Request&>(req).path_params["id"] = req.matches[1];
                Handlers::handle_catalog_by_id(req, res);
            }));
    svr.Get("/catalog", auth_wrap(cfg, Handlers::handle_catalog_list));

    // --- State ---
    svr.Get("/state/tracks", auth_wrap(cfg, Handlers::handle_state_tracks));
    svr.Get("/state/items", auth_wrap(cfg, Handlers::handle_state_items));
    svr.Get("/state/selection", auth_wrap(cfg, Handlers::handle_state_selection));
    svr.Get("/state/automation", auth_wrap(cfg, Handlers::handle_state_automation));
    svr.Get("/state", auth_wrap(cfg, Handlers::handle_state));

    svr.Post(R"(/state/tracks/(\d+))",
             auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                 const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                 Handlers::handle_state_set_track(req, res);
             }));

    // --- Execute ---
    svr.Post("/execute/action", auth_wrap(cfg, Handlers::handle_execute_action));
    svr.Post("/execute/sequence", auth_wrap(cfg, Handlers::handle_execute_sequence));

    // --- Scripts (Phase 1) ---
    svr.Post("/scripts/register", auth_wrap(cfg, Handlers::handle_scripts_register));
    svr.Get("/scripts/cache", auth_wrap(cfg, Handlers::handle_scripts_cache));
    svr.Get(R"(/scripts/([^/]+))",
            auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                const_cast<httplib::Request&>(req).path_params["id"] = req.matches[1];
                Handlers::handle_scripts_get(req, res);
            }));
    svr.Delete(R"(/scripts/([^/]+))",
               auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                   const_cast<httplib::Request&>(req).path_params["id"] = req.matches[1];
                   Handlers::handle_scripts_delete(req, res);
               }));

    // --- History ---
    svr.Get("/history", auth_wrap(cfg, Handlers::handle_history));

    // --- Catch-all: 404 ---
    svr.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.status == 404) {
            res.set_content(R"({"error":"Not found","code":"NOT_FOUND","context":{}})",
                            "application/json");
        }
    });
}

}  // namespace ReaClaw::Router

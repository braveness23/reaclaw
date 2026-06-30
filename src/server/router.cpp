#include "server/router.h"

#include "app.h"
#include "auth/auth.h"
#include "config/config.h"
#include "handlers/analysis.h"
#include "handlers/capabilities.h"
#include "handlers/catalog.h"
#include "handlers/chunk.h"
#include "handlers/common.h"
#include "handlers/execute.h"
#include "handlers/history.h"
#include "handlers/items.h"
#include "handlers/learning.h"
#include "handlers/midi.h"
#include "handlers/probe.h"
#include "handlers/project.h"
#include "handlers/render.h"
#include "handlers/screenshot.h"
#include "handlers/scripts.h"
#include "handlers/snapshot.h"
#include "handlers/state.h"
#include "handlers/transport.h"
#include "handlers/visualize.h"
#include "reaper/executor.h"
#include "server/server.h"
#include "util/logging.h"

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
        Log::debug(req.method + " " + req.path);
        handler(req, res);
        if (res.status >= 500)
            Log::error(req.method + " " + req.path + " → " + std::to_string(res.status));
        else if (res.status >= 400)
            Log::warn(req.method + " " + req.path + " → " + std::to_string(res.status));
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
    // Track icon discovery — registered before the parameterised track routes so
    // the literal path "track-icons" is never mistaken for a track index.
    svr.Get("/state/track-icons", auth_wrap(cfg, Handlers::handle_state_track_icons));
    svr.Get("/state/tracks", auth_wrap(cfg, Handlers::handle_state_tracks));
    svr.Get("/state/selection", auth_wrap(cfg, Handlers::handle_state_selection));
    svr.Get("/state/automation", auth_wrap(cfg, Handlers::handle_state_automation));
    // Universal state-chunk backstop (issue #48): read/write any track/item/
    // envelope's full RPP chunk. Registered before "/state" so the literal path
    // is matched ahead of the catch-all snapshot read.
    svr.Get("/state/chunk", auth_wrap(cfg, Handlers::handle_chunk_get));
    svr.Post("/state/chunk", auth_wrap(cfg, Handlers::handle_chunk_post));
    svr.Get("/state/changes", auth_wrap(cfg, Handlers::handle_state_changes));
    svr.Get("/state", auth_wrap(cfg, Handlers::handle_state));

    // Create + batch update tracks.
    svr.Post("/state/tracks", auth_wrap(cfg, Handlers::handle_state_tracks_post));

    // FX sub-resources (registered before the bare track index route; httplib
    // does full-path regex matching, so /fx and /sends paths never fall through
    // to the single-track handler).
    svr.Post(R"(/state/tracks/(\d+)/fx)",
             auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                 const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                 Handlers::handle_state_add_fx(req, res);
             }));
    // FX preset sub-resource (registered before the bare fx/{slot} routes).
    svr.Get(R"(/state/tracks/(\d+)/fx/(\d+)/preset)",
            auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                const_cast<httplib::Request&>(req).path_params["slot"] = req.matches[2];
                Handlers::handle_fx_get_preset(req, res);
            }));
    svr.Post(R"(/state/tracks/(\d+)/fx/(\d+)/preset)",
             auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                 const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                 const_cast<httplib::Request&>(req).path_params["slot"] = req.matches[2];
                 Handlers::handle_fx_set_preset(req, res);
             }));
    // FX copy/move to another track (extra path segment; registered before the
    // bare fx/{slot} routes for clarity).
    svr.Post(R"(/state/tracks/(\d+)/fx/(\d+)/copy)",
             auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                 const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                 const_cast<httplib::Request&>(req).path_params["slot"] = req.matches[2];
                 Handlers::handle_fx_copy(req, res);
             }));
    svr.Get(R"(/state/tracks/(\d+)/fx/(\d+))",
            auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                const_cast<httplib::Request&>(req).path_params["slot"] = req.matches[2];
                Handlers::handle_state_get_fx(req, res);
            }));
    svr.Post(R"(/state/tracks/(\d+)/fx/(\d+))",
             auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                 const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                 const_cast<httplib::Request&>(req).path_params["slot"] = req.matches[2];
                 Handlers::handle_state_set_fx(req, res);
             }));
    svr.Delete(R"(/state/tracks/(\d+)/fx/(\d+))",
               auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                   const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                   const_cast<httplib::Request&>(req).path_params["slot"] = req.matches[2];
                   Handlers::handle_state_delete_fx(req, res);
               }));

    // Send sub-resources.
    svr.Post(R"(/state/tracks/(\d+)/sends)",
             auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                 const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                 Handlers::handle_state_add_send(req, res);
             }));
    svr.Post(R"(/state/tracks/(\d+)/sends/(\d+))",
             auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                 const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                 const_cast<httplib::Request&>(req).path_params["send"] = req.matches[2];
                 Handlers::handle_state_set_send(req, res);
             }));
    svr.Delete(R"(/state/tracks/(\d+)/sends/(\d+))",
               auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                   const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                   const_cast<httplib::Request&>(req).path_params["send"] = req.matches[2];
                   Handlers::handle_state_delete_send(req, res);
               }));

    // Envelope automation write.
    svr.Post(R"(/state/tracks/(\d+)/automation)",
             auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                 const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                 Handlers::handle_automation_write(req, res);
             }));

    // Single-track update + delete.
    svr.Post(R"(/state/tracks/(\d+))",
             auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                 const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                 Handlers::handle_state_set_track(req, res);
             }));
    svr.Delete(R"(/state/tracks/(\d+))",
               auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                   const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                   Handlers::handle_state_delete_track(req, res);
               }));

    // Selection write.
    svr.Post("/state/selection", auth_wrap(cfg, Handlers::handle_state_set_selection));

    // --- Media items (Epic #17 Tier-B content manipulation) ---
    // Split sub-resource registered before the bare item-index routes.
    svr.Post(R"(/state/items/(\d+)/split)",
             auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                 const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                 Handlers::handle_item_split(req, res);
             }));
    svr.Get(R"(/state/items/(\d+))",
            auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                Handlers::handle_item_get(req, res);
            }));
    svr.Post(R"(/state/items/(\d+))",
             auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                 const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                 Handlers::handle_item_update(req, res);
             }));
    svr.Delete(R"(/state/items/(\d+))",
               auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                   const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                   Handlers::handle_item_delete(req, res);
               }));
    svr.Get("/state/items", auth_wrap(cfg, Handlers::handle_items_get));
    svr.Post("/state/items", auth_wrap(cfg, Handlers::handle_items_post));

    // --- Take-FX verbs (issue #50) — registered before bare item-index routes ---
    // Preset sub-resource first (3 captures + /preset suffix — most specific).
    svr.Get(R"(/state/items/(\d+)/takes/(\d+)/fx/(\d+)/preset)",
            auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                const_cast<httplib::Request&>(req).path_params["take"] = req.matches[2];
                const_cast<httplib::Request&>(req).path_params["slot"] = req.matches[3];
                Handlers::handle_take_get_fx_preset(req, res);
            }));
    svr.Post(R"(/state/items/(\d+)/takes/(\d+)/fx/(\d+)/preset)",
             auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                 const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                 const_cast<httplib::Request&>(req).path_params["take"] = req.matches[2];
                 const_cast<httplib::Request&>(req).path_params["slot"] = req.matches[3];
                 Handlers::handle_take_set_fx_preset(req, res);
             }));
    // Copy sub-resource (3 captures + /copy suffix).
    svr.Post(R"(/state/items/(\d+)/takes/(\d+)/fx/(\d+)/copy)",
             auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                 const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                 const_cast<httplib::Request&>(req).path_params["take"] = req.matches[2];
                 const_cast<httplib::Request&>(req).path_params["slot"] = req.matches[3];
                 Handlers::handle_take_copy_fx(req, res);
             }));
    // Bare slot routes (3 captures).
    svr.Get(R"(/state/items/(\d+)/takes/(\d+)/fx/(\d+))",
            auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                const_cast<httplib::Request&>(req).path_params["take"] = req.matches[2];
                const_cast<httplib::Request&>(req).path_params["slot"] = req.matches[3];
                Handlers::handle_take_get_fx(req, res);
            }));
    svr.Post(R"(/state/items/(\d+)/takes/(\d+)/fx/(\d+))",
             auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                 const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                 const_cast<httplib::Request&>(req).path_params["take"] = req.matches[2];
                 const_cast<httplib::Request&>(req).path_params["slot"] = req.matches[3];
                 Handlers::handle_take_set_fx(req, res);
             }));
    svr.Delete(R"(/state/items/(\d+)/takes/(\d+)/fx/(\d+))",
               auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                   const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                   const_cast<httplib::Request&>(req).path_params["take"] = req.matches[2];
                   const_cast<httplib::Request&>(req).path_params["slot"] = req.matches[3];
                   Handlers::handle_take_delete_fx(req, res);
               }));
    // Add FX to take (2 captures, no slot).
    svr.Post(R"(/state/items/(\d+)/takes/(\d+)/fx)",
             auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                 const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                 const_cast<httplib::Request&>(req).path_params["take"] = req.matches[2];
                 Handlers::handle_take_add_fx(req, res);
             }));

    // --- MIDI verbs (issue #51) ---
    // Registered before the bare items index routes to avoid shadowing.
    svr.Get(R"(/state/items/(\d+)/midi)",
            auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                Handlers::handle_midi_get(req, res);
            }));
    svr.Post(R"(/state/items/(\d+)/midi)",
             auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                 const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                 Handlers::handle_midi_post(req, res);
             }));

    // --- Audio perception (Epic #18) ---
    svr.Get("/state/meters", auth_wrap(cfg, Handlers::handle_meters));
    svr.Get(R"(/analysis/item/(\d+))",
            auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                Handlers::handle_analysis_item(req, res);
            }));
    svr.Get("/analysis/file", auth_wrap(cfg, Handlers::handle_analysis_file));

    // --- Audio visualization (Epic #19 / Q4) — image + machine-readable digest ---
    svr.Get(R"(/analysis/item/(\d+)/visualize)",
            auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                Handlers::handle_visualize_item(req, res);
            }));
    svr.Get("/analysis/file/visualize", auth_wrap(cfg, Handlers::handle_visualize_file));

    // --- Musical-attribute probes (Epic #19 / Q7) — key / pitch / tempo ---
    svr.Get(R"(/analysis/item/(\d+)/probe)",
            auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                const_cast<httplib::Request&>(req).path_params["index"] = req.matches[1];
                Handlers::handle_probe_item(req, res);
            }));
    svr.Get("/analysis/file/probe", auth_wrap(cfg, Handlers::handle_probe_file));

    // --- On-demand screenshot (Epic #19 / Q5) — fallback for GUI-only state ---
    svr.Get("/screenshot", auth_wrap(cfg, Handlers::handle_screenshot));

    // --- Snapshot / state-diff layer (Epic #20 prep; also backs #19 A/B diff) ---
    svr.Post("/snapshot", auth_wrap(cfg, Handlers::handle_snapshot_create));
    svr.Get("/snapshot", auth_wrap(cfg, Handlers::handle_snapshot_list));
    svr.Get("/snapshot/diff", auth_wrap(cfg, Handlers::handle_snapshot_diff));
    svr.Get(R"(/snapshot/(\d+))",
            auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                const_cast<httplib::Request&>(req).path_params["id"] = req.matches[1];
                Handlers::handle_snapshot_get(req, res);
            }));
    svr.Delete(R"(/snapshot/(\d+))",
               auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                   const_cast<httplib::Request&>(req).path_params["id"] = req.matches[1];
                   Handlers::handle_snapshot_delete(req, res);
               }));

    // --- Learned suggestions (Epic #20) — local-first, opt-in correction mining ---
    svr.Get("/suggestions", auth_wrap(cfg, Learning::handle_suggestions));
    svr.Get("/learn/stats", auth_wrap(cfg, Learning::handle_learn_stats));

    // Markers & regions.
    svr.Get("/state/markers", auth_wrap(cfg, Handlers::handle_markers_get));
    svr.Post("/state/markers", auth_wrap(cfg, Handlers::handle_markers_post));
    svr.Delete(R"(/state/markers/(\d+))",
               auth_wrap(cfg, [](const httplib::Request& req, httplib::Response& res) {
                   const_cast<httplib::Request&>(req).path_params["id"] = req.matches[1];
                   Handlers::handle_markers_delete(req, res);
               }));

    // Tempo / time-signature map.
    svr.Get("/state/tempo", auth_wrap(cfg, Handlers::handle_tempo_get));
    svr.Post("/state/tempo", auth_wrap(cfg, Handlers::handle_tempo_post));

    // --- Project (dirty/length/notes) + undo/redo + time conversion ---
    svr.Get("/undo", auth_wrap(cfg, Handlers::handle_undo_state));
    svr.Post("/undo", auth_wrap(cfg, Handlers::handle_undo));
    svr.Post("/redo", auth_wrap(cfg, Handlers::handle_redo));
    svr.Get("/project", auth_wrap(cfg, Handlers::handle_project_get));
    svr.Post("/project/notes", auth_wrap(cfg, Handlers::handle_project_notes));
    svr.Get("/project/extstate", auth_wrap(cfg, Handlers::handle_extstate_get));
    svr.Post("/project/extstate", auth_wrap(cfg, Handlers::handle_extstate_post));
    svr.Delete("/project/extstate", auth_wrap(cfg, Handlers::handle_extstate_delete));

    // --- Project lifecycle (issue #34) ---
    svr.Post("/project/new", auth_wrap(cfg, Handlers::handle_project_new));
    svr.Post("/project/open", auth_wrap(cfg, Handlers::handle_project_open));
    svr.Post("/project/save", auth_wrap(cfg, Handlers::handle_project_save));
    svr.Post("/project/reset", auth_wrap(cfg, Handlers::handle_project_reset));
    svr.Get("/time", auth_wrap(cfg, Handlers::handle_time_convert));

    // Capability manifest.
    svr.Get("/capabilities", auth_wrap(cfg, Handlers::handle_capabilities));

    // --- Render (Epic #32 / issue #33) ---
    svr.Post("/render", auth_wrap(cfg, Handlers::handle_render));

    // --- Transport verbs (issue #49) ---
    svr.Post("/transport", auth_wrap(cfg, Handlers::handle_transport_action));
    svr.Post("/transport/cursor", auth_wrap(cfg, Handlers::handle_transport_cursor));
    svr.Post("/transport/loop", auth_wrap(cfg, Handlers::handle_transport_loop));

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

    // --- Catch-all: 404 for unmatched routes ---
    // httplib runs this for any response with status >= 400. Only supply the
    // generic body when a handler hasn't already set one, so handler-authored
    // 404 messages (e.g. "Item index out of range", "No visible window…") are
    // preserved instead of being flattened to "Not found".
    svr.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.status == 404 && res.body.empty()) {
            res.set_content(R"({"error":"Not found","code":"NOT_FOUND","context":{}})",
                            "application/json");
        }
    });
}

}  // namespace ReaClaw::Router

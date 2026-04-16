// REAPERAPI_IMPLEMENT must be defined in exactly one translation unit.
// It causes reaper_plugin_functions.h to define (not just declare) all
// function pointer globals populated by REAPERAPI_LoadAPI.
// clang-format off
#define REAPERAPI_IMPLEMENT
#include "reaper/api.h"
// clang-format on

#include "app.h"
#include "config/config.h"
#include "db/db.h"
#include "panel/panel.h"
#include "reaper/catalog.h"
#include "reaper/executor.h"
#include "server/server.h"
#include "util/logging.h"

#include <string>

#include <reaper_plugin_functions.h>

namespace ReaClaw {

// Required REAPER API functions for Phase 0 (verified non-null after LoadAPI).
// Any null pointer is logged as a warning; the extension still loads.
static constexpr const char* k_required_fns[] = {
        "GetResourcePath",
        "GetAppVersion",
        "ShowConsoleMsg",
        "kbd_enumerateActions",
        "SectionFromUniqueID",
        "CountTracks",
        "GetTrack",
        "GetTrackName",
        "GetSetMediaTrackInfo",
        "GetProjectTimeSignature2",
        "GetCursorPosition",
        "GetPlayState",
        "Main_OnCommand",
        "NamedCommandLookup",
        "plugin_register",
};

bool init(reaper_plugin_info_t* rec, void* hInstance) {
    // Sanity-check a few critical pointers
    if (!ShowConsoleMsg || !GetResourcePath || !GetAppVersion) {
        // Can't log yet — write to stderr
        fputs("ReaClaw: critical REAPER API functions are null after LoadAPI\n", stderr);
        return false;
    }

    // Initialise logging (REAPER console + optional file — file path resolved
    // after config load, so we pass null for the file path here and re-init
    // once config is loaded).
    Log::init(LogLevel::info, "", ShowConsoleMsg);
    Log::info("ReaClaw " REACLAW_VERSION " loading...");

    // Warn about any missing optional function pointers
    (void)rec;

    // Load configuration
    const char* resource_path = GetResourcePath();
    if (!resource_path) {
        Log::error("GetResourcePath() returned null");
        return false;
    }

    if (!Config::load(g_config, resource_path)) {
        Log::error("Failed to load config — aborting");
        return false;
    }

    // Re-initialise logging with resolved level and optional file path
    {
        LogLevel level = LogLevel::info;
        const std::string& lv = g_config.log_level;
        if (lv == "debug")
            level = LogLevel::debug;
        else if (lv == "warn")
            level = LogLevel::warn;
        else if (lv == "error")
            level = LogLevel::error;
        Log::init(level, g_config.log_file, ShowConsoleMsg, g_config.log_format);
    }

    // Open database
    if (!g_db.open(g_config.db_path)) {
        Log::error("Failed to open database: " + g_config.db_path);
        return false;
    }
    Log::info("Database: " + g_config.db_path);

    // Build action catalog (called from main thread — safe to use kbd_enumerateActions)
    Catalog::build(g_db, GetAppVersion() ? GetAppVersion() : "unknown");

    // Reconcile registered scripts: re-register any scripts that are in the
    // DB but may have been dropped from reaper-kb.ini (e.g., after reinstall)
    if (AddRemoveReaScript) {
        auto scripts = g_db.query("SELECT id, script_path FROM scripts", {});
        int reconciled = 0;
        for (auto& row : scripts) {
            const std::string& path = row.at("script_path");
            int cmd = AddRemoveReaScript(true, 0, path.c_str(), true);
            if (cmd != 0)
                reconciled++;
        }
        if (reconciled > 0) {
            Log::info("Re-registered " + std::to_string(reconciled) + " scripts");
        }
    }

    // Register main-thread timer callback (~30fps)
    if (plugin_register) {
        plugin_register("timer", reinterpret_cast<void*>(&ReaClaw::timer_callback));
    }

    // Start HTTPS server
    if (!Server::start(g_config)) {
        Log::error("Failed to start HTTPS server");
        // Non-fatal: extension still loads, catalog queries work
    }

    Log::info("ReaClaw ready — https://" + g_config.host + ":" + std::to_string(g_config.port));

    // Register dockable control panel (non-fatal if unavailable)
    Panel::init(rec, hInstance);

    return true;
}

void shutdown() {
    Log::info("ReaClaw shutting down...");

    Panel::destroy();

    // Unregister timer
    if (plugin_register) {
        plugin_register("-timer", reinterpret_cast<void*>(&ReaClaw::timer_callback));
    }

    // Stop server (blocks until thread joins)
    Server::stop();

    // Close database
    g_db.close();

    Log::info("ReaClaw stopped");
}

void timer_callback() {
    Executor::tick();
}

}  // namespace ReaClaw

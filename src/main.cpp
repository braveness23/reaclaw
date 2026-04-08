#include "app.h"
#include "config/config.h"
#include "db/db.h"
#include "reaper/api.h"

// Full REAPER SDK — provides reaper_plugin_info_t definition and REAPERAPI_LoadAPI.
// REAPERAPI_IMPLEMENT is NOT defined here; storage for function pointers lives in api.cpp.
#include <chrono>

#include <reaper_plugin_functions.h>

// ---------------------------------------------------------------------------
// Global singleton definitions (declared extern in app.h)
// ---------------------------------------------------------------------------
namespace ReaClaw {
Config g_config;
DB g_db;
std::chrono::steady_clock::time_point g_start_time = std::chrono::steady_clock::now();
}  // namespace ReaClaw

// ---------------------------------------------------------------------------
// REAPER extension entry point
//
// REAPER calls ReaperPluginEntry twice:
//   Load:   rec != nullptr — bind API, initialise subsystems, return 1
//   Unload: rec == nullptr — clean shutdown, return 0
//
// REAPERAPI_LoadAPI (called from ReaClaw::init) populates all REAPER API
// function pointers declared in reaper_plugin_functions.h.
// ---------------------------------------------------------------------------

extern "C" {

#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
        int ReaperPluginEntry(void* hInstance, reaper_plugin_info_t* rec) {
    (void)hInstance;

    if (!rec) {
        ReaClaw::shutdown();
        return 0;
    }

    if (rec->caller_version != REAPER_PLUGIN_VERSION)
        return 0;

    // Bind all REAPER API function pointers. Must succeed before any
    // REAPER API calls are made (including ShowConsoleMsg / GetResourcePath).
    // Note: REAPERAPI_LoadAPI returns 0 on success, non-zero on failure.
    if (REAPERAPI_LoadAPI(rec->GetFunc))
        return 0;

    ReaClaw::g_start_time = std::chrono::steady_clock::now();

    return ReaClaw::init(rec) ? 1 : 0;
}

}  // extern "C"

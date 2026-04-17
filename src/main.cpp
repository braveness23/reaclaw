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
#ifdef _WIN32
    // Diagnostic: step-by-step breadcrumb file written via raw Win32 handles
    // so it works even if the CRT, REAPER API, or logging subsystem fails.
    // Each step appends a line; the last line written shows how far init got.
    // Remove this block once the Windows load issue is fully diagnosed.
    auto diag_write = [](const char* msg) {
        char tmp[MAX_PATH];
        DWORD n = GetTempPathA(MAX_PATH, tmp);
        if (n == 0 || n >= MAX_PATH)
            return;
        lstrcatA(tmp, "reaclaw-diag.txt");
        HANDLE h = CreateFileA(
                tmp, GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return;
        SetFilePointer(h, 0, nullptr, FILE_END);
        DWORD written;
        WriteFile(h, msg, lstrlenA(msg), &written, nullptr);
        CloseHandle(h);
    };

    // Step 1 — DLL entry point reached
    diag_write(rec ? "1: ReaperPluginEntry (load)\n" : "1: ReaperPluginEntry (unload)\n");
#endif

    if (!rec) {
        ReaClaw::shutdown();
        return 0;
    }

#ifdef _WIN32
    // Step 2 — version check
    {
        char buf[64];
        wsprintfA(buf, "2: caller_version=0x%X expected=0x%X\n",
                  rec->caller_version, REAPER_PLUGIN_VERSION);
        diag_write(buf);
    }
#endif

    if (rec->caller_version != REAPER_PLUGIN_VERSION)
        return 0;

    // Bind all REAPER API function pointers. Must succeed before any
    // REAPER API calls are made (including ShowConsoleMsg / GetResourcePath).
    // Note: REAPERAPI_LoadAPI returns the count of unresolved pointers (0 = all
    // resolved). We intentionally ignore a non-zero return: the full SDK header
    // declares functions added across many REAPER versions, so older hosts will
    // leave some pointers null. We validate the specific functions we need in
    // ReaClaw::init() instead.
    int unresolved = REAPERAPI_LoadAPI(rec->GetFunc);

#ifdef _WIN32
    // Step 3 — LoadAPI result + critical pointer check
    {
        char buf[128];
        wsprintfA(buf, "3: LoadAPI unresolved=%d ShowConsoleMsg=%s GetResourcePath=%s\n",
                  unresolved,
                  ShowConsoleMsg ? "ok" : "NULL",
                  GetResourcePath ? "ok" : "NULL");
        diag_write(buf);
    }
#endif

    ReaClaw::g_start_time = std::chrono::steady_clock::now();

    bool ok = ReaClaw::init(rec, hInstance);

#ifdef _WIN32
    diag_write(ok ? "4: init() returned true\n" : "4: init() returned false\n");
#endif

    return ok ? 1 : 0;
}

}  // extern "C"

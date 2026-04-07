#include "reaper/api.h"

// ReaperPluginEntry is the extension entry point called by REAPER on load and unload.
// Implementation will follow Phase 0 checklist items in ReaClaw_IMPLEMENTATION_CHECKLIST.md.

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

    if (rec->caller_version != REAPER_PLUGIN_VERSION) {
        return 0;
    }

    return ReaClaw::init(rec) ? 1 : 0;
}

}  // extern "C"

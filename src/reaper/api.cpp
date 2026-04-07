#include "reaper/api.h"

// Phase 0: bind REAPER API function pointers via REAPERAPI_LoadAPI.
// Full implementation per ReaClaw_IMPLEMENTATION_CHECKLIST.md §REAPER API Layer.

namespace ReaClaw {

bool init(reaper_plugin_info_t* rec) {
    (void)rec;
    // TODO Phase 0: REAPERAPI_LoadAPI, Config::load, DB::open, catalog index,
    //               TLS cert generation, server start, timer registration.
    return true;
}

void shutdown() {
    // TODO Phase 0: stop server thread, flush DB, unregister timer.
}

}  // namespace ReaClaw

#pragma once

// Forward-declare the REAPER plugin info struct to avoid pulling in the full
// SDK headers here. api.cpp defines REAPERAPI_IMPLEMENT before including the
// SDK, which allocates all function pointer globals exactly once.

struct reaper_plugin_info_t;

// Canonical REAPER plugin ABI version constant.
#ifndef REAPER_PLUGIN_VERSION
#define REAPER_PLUGIN_VERSION 0x20E
#endif

namespace ReaClaw {

// Called by ReaperPluginEntry after REAPERAPI_LoadAPI succeeds.
// Initialises config, DB, catalog, TLS, and starts the HTTPS server.
bool init(reaper_plugin_info_t* rec);

// Called by ReaperPluginEntry when rec == nullptr (REAPER shutting down).
void shutdown();

// Main-thread timer callback registered via plugin_register("timer", ...).
void timer_callback();

}  // namespace ReaClaw

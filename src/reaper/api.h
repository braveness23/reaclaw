#pragma once

// Forward-declare the REAPER plugin info type before the SDK header pulls in
// platform-specific includes. The SDK header is included in api.cpp with
// REAPERAPI_IMPLEMENT defined so function pointers are allocated only once.

struct reaper_plugin_info_t;

// Defined in reaper_plugin.h — use the value directly to avoid the full include here.
#ifndef REAPER_PLUGIN_VERSION
#define REAPER_PLUGIN_VERSION 0x20E
#endif

namespace ReaClaw {

// Called from ReaperPluginEntry when REAPER loads the extension.
// Binds REAPER API function pointers, initializes all subsystems, starts server.
bool init(reaper_plugin_info_t* rec);

// Called from ReaperPluginEntry when REAPER unloads the extension (rec == NULL).
void shutdown();

}  // namespace ReaClaw

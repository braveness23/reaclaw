#pragma once

struct reaper_plugin_info_t;

namespace ReaClaw {
namespace Menu {

// Initialize SWELL (non-Windows), register the ReaClaw actions, and add a
// "ReaClaw" submenu under REAPER's main "Extensions" menu via hookcustommenu.
// Must be called after REAPERAPI_LoadAPI in ReaperPluginEntry.
void init(reaper_plugin_info_t* rec, void* hInstance);

// Unregister all hooks and actions. Called at shutdown.
void destroy();

}  // namespace Menu
}  // namespace ReaClaw

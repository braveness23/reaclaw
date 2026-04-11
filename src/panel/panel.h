#pragma once

struct reaper_plugin_info_t;

namespace ReaClaw {
namespace Panel {

// Initialize SWELL (non-Windows), create the dockable dialog, and register
// the "ReaClaw: Control Panel" toggle action in REAPER's Actions list.
// Must be called after REAPERAPI_LoadAPI in ReaperPluginEntry.
void init(reaper_plugin_info_t* rec, void* hInstance);

// Toggle the panel visible/hidden.  Called by the registered REAPER action.
void show_toggle();

// Destroy the panel window and unregister all hooks.  Called at shutdown.
void destroy();

}  // namespace Panel
}  // namespace ReaClaw

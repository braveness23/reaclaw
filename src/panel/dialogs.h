#pragma once

// Polished SWELL/Win32 dialogs that replace the plain MessageBox popups in the
// Extensions > ReaClaw menu. Each is modeless (does not block REAPER's main
// thread) and single-instance (re-invoking brings the existing window forward).

namespace ReaClaw {
namespace Dialogs {

// Store the module instance used for CreateDialogParam resource lookup.
// Call once after SWELL is initialized (from Menu::init).
void init(void* hInstance);

void show_status();   // server state, address, auth, uptime, version (live)
void show_api_key();  // API key field + copy button + confirmation
void show_log();      // scrollable read-only log viewer with refresh

}  // namespace Dialogs
}  // namespace ReaClaw

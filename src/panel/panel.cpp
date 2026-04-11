// ReaClaw dockable control panel.
//
// On Linux/macOS the panel uses SWELL (provided by the REAPER host process).
// swell_stub.cpp holds the SWELL function pointer table; here we define the
// dialog resource via SWELL_DEFINE_DIALOG_RESOURCE_BEGIN/END macros and
// implement the dialog proc + REAPER dock integration.

#ifndef _WIN32

// Tell SWELL headers "function pointer storage is in swell_stub.cpp"
#define SWELL_PROVIDED_BY_APP
#include "WDL/swell/swell.h"

// ---- Dialog resource definition ----------------------------------------
// Macros redefine BEGIN, END, PUSHBUTTON, EDITTEXT, CONTROL, LTEXT, etc.
// Keep this block self-contained; restore standard macros afterwards.
#include "WDL/swell/swell-dlggen.h"

#include "panel/resource.h"

// clang-format off
// Dialog: 260 x 220 dialog units, scale 1.5
// Layout:
//   y=  6  [Enable checkbox]
//   y= 22  Host: [edit]
//   y= 38  Port: [edit]
//   y= 54  [Bypass TLS checkbox]
//   y= 70  Log: label            [Refresh btn]
//   y= 88  [log text area, 96 du tall]
//   y=200  [Apply btn]
SWELL_DEFINE_DIALOG_RESOURCE_BEGIN(IDD_REACLAW_PANEL, SWELL_DLG_WS_RESIZABLE, "ReaClaw", 260, 220, 1.5)
BEGIN
  CONTROL  "Enable ReaClaw server",        IDC_ENABLE,    "Button", BS_AUTOCHECKBOX|WS_TABSTOP,  6,   6, 200, 12
  LTEXT    "Host:",                         IDC_STATIC,               6,  22,  30, 10
  EDITTEXT IDC_HOST,                        40,  20, 160, 12, ES_AUTOHSCROLL|WS_TABSTOP
  LTEXT    "Port:",                         IDC_STATIC,               6,  38,  30, 10
  EDITTEXT IDC_PORT,                        40,  36,  60, 12, ES_AUTOHSCROLL|WS_TABSTOP
  CONTROL  "Bypass TLS cert validation",   IDC_TLS_BYPASS,"Button", BS_AUTOCHECKBOX|WS_TABSTOP,  6,  54, 220, 12
  LTEXT    "Log:",                          IDC_STATIC,               6,  70,  30, 10
  PUSHBUTTON "Refresh",                     IDC_REFRESH,            200,  68,  54, 14
  EDITTEXT IDC_LOG,                          6,  86, 248,  96, ES_READONLY|ES_MULTILINE|WS_VSCROLL|WS_TABSTOP
  PUSHBUTTON "Apply",                       IDC_APPLY,              200, 198,  54, 14
END
SWELL_DEFINE_DIALOG_RESOURCE_END(IDD_REACLAW_PANEL)
// clang-format on

#include "WDL/swell/swell-menugen.h"
// Restore BEGIN/END so they don't pollute later code
#undef BEGIN
#undef END

#endif  // !_WIN32

// ---- Normal includes (after the resource block) ------------------------
#include "panel/panel.h"

#include "app.h"
#include "config/config.h"
#include "server/server.h"
#include "util/logging.h"

#include <reaper_plugin.h>
#include <reaper_plugin_functions.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#ifdef _WIN32
// BST_CHECKED / BST_UNCHECKED live in <commctrl.h>; not pulled in by
// <windows.h> when WIN32_LEAN_AND_MEAN is defined.
#include <commctrl.h>
#endif

#ifndef _WIN32
#include <dlfcn.h>
// SWELL_dllMain is defined in swell_stub.cpp.
extern "C" int SWELL_dllMain(HINSTANCE hInst, DWORD callMode, LPVOID getFunc);
#endif

namespace ReaClaw {
namespace Panel {

static HWND g_hwnd  = nullptr;
static int  g_cmd_id = 0;
static custom_action_register_t g_action = {};
static void* g_hInstance = nullptr;

// ---- Helpers -----------------------------------------------------------

static std::string read_log() {
    if (g_config.log_file.empty())
        return "(no log file configured — ReaClaw messages go to REAPER console)";
    std::ifstream f(g_config.log_file);
    if (!f)
        return "(cannot open log file: " + g_config.log_file + ")";
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    // Cap at ~4 KB so the edit control stays responsive
    if (s.size() > 4096)
        s = "...\n" + s.substr(s.size() - 4096);
    return s;
}

static void populate(HWND hwnd) {
    CheckDlgButton(hwnd, IDC_ENABLE,    Server::is_running() ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemText(hwnd, IDC_HOST,      g_config.host.c_str());
    SetDlgItemText(hwnd, IDC_PORT,      std::to_string(g_config.port).c_str());
    CheckDlgButton(hwnd, IDC_TLS_BYPASS, !g_config.tls_enabled ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemText(hwnd, IDC_LOG,       read_log().c_str());
}

// ---- Dialog proc -------------------------------------------------------

static INT_PTR WINAPI dlgproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM /*lParam*/) {
    switch (msg) {
        case WM_INITDIALOG:
            populate(hwnd);
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_REFRESH:
                    SetDlgItemText(hwnd, IDC_LOG, read_log().c_str());
                    return 0;

                case IDC_APPLY: {
                    bool want_enabled = (IsDlgButtonChecked(hwnd, IDC_ENABLE) == BST_CHECKED);

                    char buf[512];
                    GetDlgItemText(hwnd, IDC_HOST, buf, (int)sizeof(buf));
                    if (buf[0]) g_config.host = buf;

                    GetDlgItemText(hwnd, IDC_PORT, buf, (int)sizeof(buf));
                    int p = std::atoi(buf);
                    if (p > 0 && p < 65536) g_config.port = p;

                    g_config.tls_enabled = !(IsDlgButtonChecked(hwnd, IDC_TLS_BYPASS) == BST_CHECKED);

                    g_config.save();

                    bool running = Server::is_running();
                    if (want_enabled && !running) {
                        Server::start(g_config);
                        Log::info("ReaClaw: server started via control panel");
                    } else if (!want_enabled && running) {
                        Server::stop();
                        Log::info("ReaClaw: server stopped via control panel");
                    } else if (want_enabled && running) {
                        // Restart to pick up host/port/TLS changes
                        Server::stop();
                        Server::start(g_config);
                        Log::info("ReaClaw: server restarted via control panel");
                    }

                    // Reflect actual running state back to the checkbox
                    CheckDlgButton(hwnd, IDC_ENABLE, Server::is_running() ? BST_CHECKED : BST_UNCHECKED);
                    return 0;
                }
            }
            return 0;

        case WM_CLOSE:
            // Hide rather than destroy so dock state is preserved
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            g_hwnd = nullptr;
            return 0;
    }
    return 0;
}

// ---- REAPER callbacks --------------------------------------------------

static bool hookcommand(int cmd, int /*flag*/) {
    if (cmd == g_cmd_id) {
        show_toggle();
        return true;
    }
    return false;
}

static int toggleaction(int cmd) {
    if (cmd == g_cmd_id)
        return (g_hwnd && IsWindowVisible(g_hwnd)) ? 1 : 0;
    return -1;
}

// ---- Public API --------------------------------------------------------

void init(reaper_plugin_info_t* rec, void* hInstance) {
    g_hInstance = hInstance;

#ifndef _WIN32
    // Populate SWELL function pointers from REAPER's SWELL implementation.
    // REAPER exports SWELLAPI_GetFunc — it resolves SWELL function names to
    // the GTK-backed implementations compiled into REAPER itself.
    // rec->GetFunc is the REAPER SDK getter (CountTracks etc.) and must NOT
    // be passed here; we need the SWELL-specific getter.
    {
        using SwellGetFunc_t = void*(*)(const char*);
        auto swellGetFunc = reinterpret_cast<SwellGetFunc_t>(
            dlsym(RTLD_DEFAULT, "SWELLAPI_GetFunc")
        );
        if (swellGetFunc) {
            SWELL_dllMain((HINSTANCE)hInstance, DLL_PROCESS_ATTACH,
                          reinterpret_cast<void*>(swellGetFunc));
        } else {
            Log::warn("ReaClaw: SWELLAPI_GetFunc not found — control panel unavailable");
            return;
        }
    }
#endif

    if (!plugin_register || !GetMainHwnd) return;

    // Register the toggle action so it appears in REAPER's Actions list
    // and can be bound to a toolbar button or keyboard shortcut.
    g_action.uniqueSectionId = 0;  // Main section
    g_action.idStr  = "REACLAW_PANEL_TOGGLE";
    g_action.name   = "ReaClaw: Control Panel";
    g_action.extra  = nullptr;
    g_cmd_id = plugin_register("custom_action", &g_action);
    if (!g_cmd_id) {
        Log::warn("ReaClaw: failed to register control panel action");
        return;
    }

    plugin_register("hookcommand",  (void*)hookcommand);
    plugin_register("toggleaction", (void*)toggleaction);

    // Create modeless dockable dialog.
    // On Linux/macOS: CreateDialogParam is a SWELL macro → SWELL_CreateDialog,
    //   which finds the inline resource registered via SWELL_DEFINE_DIALOG_RESOURCE_BEGIN.
    // On Windows: native Win32 CreateDialogParam, resource comes from panel.rc.
    g_hwnd = CreateDialogParam(
        (HINSTANCE)hInstance,
        MAKEINTRESOURCE(IDD_REACLAW_PANEL),
        GetMainHwnd(),
        dlgproc,
        0
    );
    if (!g_hwnd) {
        Log::warn("ReaClaw: failed to create control panel window");
        return;
    }

    if (DockWindowAddEx) {
        // allowShow=true: REAPER restores the panel's visibility state from
        // the previous session (e.g. if user left it docked).
        DockWindowAddEx(g_hwnd, "ReaClaw", "reaclaw_panel", true);
    }

    Log::info("ReaClaw: control panel ready (action ID " + std::to_string(g_cmd_id) + ")");
}

void show_toggle() {
    if (!g_hwnd) return;
    if (IsWindowVisible(g_hwnd)) {
        ShowWindow(g_hwnd, SW_HIDE);
    } else {
        populate(g_hwnd);
        ShowWindow(g_hwnd, SW_SHOW);
        if (DockWindowActivate) DockWindowActivate(g_hwnd);
    }
}

void destroy() {
    if (g_cmd_id && plugin_register) {
        plugin_register("-hookcommand",  (void*)hookcommand);
        plugin_register("-toggleaction", (void*)toggleaction);
        plugin_register("-custom_action", &g_action);
    }
    if (g_hwnd) {
        if (DockWindowRemove) DockWindowRemove(g_hwnd);
        DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
    }
}

}  // namespace Panel
}  // namespace ReaClaw

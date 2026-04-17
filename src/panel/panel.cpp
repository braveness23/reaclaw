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
#include "app.h"
#include "config/config.h"
#include "panel/panel.h"
#include "server/server.h"
#include "util/logging.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include <reaper_plugin.h>
#include <reaper_plugin_functions.h>

#ifdef _WIN32
// Resource IDs (IDC_*, IDD_*) — on non-Windows these come from the SWELL
// dialog resource block above; on Windows they must be included explicitly.
#include "panel/resource.h"
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

static HWND g_hwnd = nullptr;
static int g_cmd_id = 0;
static custom_action_register_t g_action = {};
static void* g_hInstance = nullptr;

// ---- Layout state (captured once at WM_INITDIALOG) ---------------------

struct CtrlRect {
    int x, y, w, h;
};

static int g_init_cx = 0;
static int g_init_cy = 0;
static CtrlRect g_log_r = {};
static CtrlRect g_refresh_r = {};
static CtrlRect g_apply_r = {};

static CtrlRect get_ctrl_rect(HWND parent, int id) {
    RECT rc = {};
    GetWindowRect(GetDlgItem(parent, id), &rc);
    MapWindowPoints(HWND_DESKTOP, parent, reinterpret_cast<POINT*>(&rc), 2);
    return {rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top};
}

static void capture_layout(HWND hwnd) {
    RECT client = {};
    GetClientRect(hwnd, &client);
    g_init_cx = client.right;
    g_init_cy = client.bottom;
    g_log_r = get_ctrl_rect(hwnd, IDC_LOG);
    g_refresh_r = get_ctrl_rect(hwnd, IDC_REFRESH);
    g_apply_r = get_ctrl_rect(hwnd, IDC_APPLY);
}

static void relayout(HWND hwnd, int cx, int cy) {
    int dx = cx - g_init_cx;
    int dy = cy - g_init_cy;

    // Batch all moves into a single deferred update to prevent flicker.
    HDWP dwp = BeginDeferWindowPos(3);
    if (!dwp)
        return;

    auto defer = [&](int id, const CtrlRect& r, int dw, int dh, int mx, int my) {
        DeferWindowPos(dwp, GetDlgItem(hwnd, id), nullptr,
                       r.x + mx, r.y + my, r.w + dw, r.h + dh,
                       SWP_NOZORDER | SWP_NOACTIVATE);
    };

    defer(IDC_LOG,     g_log_r,     dx, dy, 0,  0);   // stretch right + down
    defer(IDC_REFRESH, g_refresh_r, 0,  0,  dx, 0);   // slide right
    defer(IDC_APPLY,   g_apply_r,   0,  0,  dx, dy);  // slide right + down

    EndDeferWindowPos(dwp);
}

// ---- Helpers -----------------------------------------------------------

static std::string read_log() {
    if (g_config.log_file.empty())
        return "(no log file configured - ReaClaw messages go to REAPER console)";
    std::ifstream f(g_config.log_file);
    if (!f)
        return "(cannot open log file: " + g_config.log_file + ")";
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    // Cap at ~4 KB so the edit control stays responsive
    if (s.size() > 4096)
        s = "...\n" + s.substr(s.size() - 4096);
#ifdef _WIN32
    // Win32 ES_MULTILINE edit controls require CRLF to break lines;
    // the log file uses Unix LF. Convert LF -> CRLF here.
    std::string out;
    out.reserve(s.size() + 64);
    for (char c : s) {
        if (c == '\n')
            out += '\r';
        out += c;
    }
    return out;
#else
    return s;
#endif
}

static void populate(HWND hwnd) {
    CheckDlgButton(hwnd, IDC_ENABLE, Server::is_running() ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemText(hwnd, IDC_HOST, g_config.host.c_str());
    SetDlgItemText(hwnd, IDC_PORT, std::to_string(g_config.port).c_str());
    CheckDlgButton(hwnd, IDC_TLS_BYPASS, !g_config.tls_enabled ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemText(hwnd, IDC_LOG, read_log().c_str());
}

static bool create_window();  // defined after init(); forward-declared for dlgproc

// ---- Dialog proc -------------------------------------------------------

static INT_PTR WINAPI dlgproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG:
            populate(hwnd);
            capture_layout(hwnd);
            return 0;

        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED)
                relayout(hwnd, LOWORD(lParam), HIWORD(lParam));
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
                    if (buf[0])
                        g_config.host = buf;

                    GetDlgItemText(hwnd, IDC_PORT, buf, (int)sizeof(buf));
                    int p = std::atoi(buf);
                    if (p > 0 && p < 65536)
                        g_config.port = p;

                    g_config.tls_enabled = !(IsDlgButtonChecked(hwnd, IDC_TLS_BYPASS) ==
                                             BST_CHECKED);

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
                    CheckDlgButton(
                            hwnd, IDC_ENABLE, Server::is_running() ? BST_CHECKED : BST_UNCHECKED);
                    return 0;
                }
            }
            return 0;

        case WM_CONTEXTMENU: {
            bool running = Server::is_running();
            bool docked = DockIsChildOfDock && DockIsChildOfDock(hwnd, nullptr) >= 0;

            HMENU menu = CreatePopupMenu();
            AppendMenuA(menu, MF_STRING, 1, running ? "Stop Server" : "Start Server");
            AppendMenuA(menu, MF_STRING, 2, "Refresh Log");
            AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuA(menu, MF_STRING | (docked ? MF_CHECKED : 0), 3,
                        "Dock ReaClaw in Docker");
            AppendMenuA(menu, MF_STRING, 4, "Close");

            // lParam carries screen coordinates; cast to SHORT for multi-monitor
            // support (coordinates can be negative on non-primary monitors).
            POINT pt = {(SHORT)LOWORD(lParam), (SHORT)HIWORD(lParam)};
            int cmd = (int)TrackPopupMenu(
                    menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);

            switch (cmd) {
                case 1:
                    if (running) {
                        Server::stop();
                        Log::info("ReaClaw: server stopped via context menu");
                    } else {
                        Server::start(g_config);
                        Log::info("ReaClaw: server started via context menu");
                    }
                    CheckDlgButton(hwnd, IDC_ENABLE,
                                   Server::is_running() ? BST_CHECKED : BST_UNCHECKED);
                    break;
                case 2:
                    SetDlgItemText(hwnd, IDC_LOG, read_log().c_str());
                    break;
                case 3:
                    if (docked) {
                        if (DockWindowRemove)
                            DockWindowRemove(hwnd);
                        // REAPER may destroy the HWND during DockWindowRemove.
                        // Recreate the dialog if needed, then show it floating.
                        if (create_window()) {
                            populate(g_hwnd);
                            ShowWindow(g_hwnd, SW_SHOW);
                            SetForegroundWindow(g_hwnd);
                        }
                    } else {
                        if (DockWindowAddEx)
                            DockWindowAddEx(hwnd, "ReaClaw", "reaclaw_panel", true);
                        if (DockWindowActivate)
                            DockWindowActivate(hwnd);
                    }
                    break;
                case 4:
                    ShowWindow(hwnd, SW_HIDE);
                    break;
            }
            return 0;
        }

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
    if (cmd != g_cmd_id)
        return -1;
    if (!g_hwnd)
        return 0;
    // When docked, inactive tabs are hidden by the docker but the panel is
    // still "open" — report it as on so toolbar buttons stay lit.
    bool docked = DockIsChildOfDock && DockIsChildOfDock(g_hwnd, nullptr) >= 0;
    return (docked || IsWindowVisible(g_hwnd)) ? 1 : 0;
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
        using SwellGetFunc_t = void* (*)(const char*);
        auto swellGetFunc = reinterpret_cast<SwellGetFunc_t>(
                dlsym(RTLD_DEFAULT, "SWELLAPI_GetFunc"));
        if (swellGetFunc) {
            SWELL_dllMain((HINSTANCE)hInstance,
                          DLL_PROCESS_ATTACH,
                          reinterpret_cast<void*>(swellGetFunc));
        } else {
            Log::warn("ReaClaw: SWELLAPI_GetFunc not found — control panel unavailable");
            return;
        }
    }
#endif

    if (!plugin_register || !GetMainHwnd)
        return;

    // Register the toggle action so it appears in REAPER's Actions list
    // and can be bound to a toolbar button or keyboard shortcut.
    g_action.uniqueSectionId = 0;  // Main section
    g_action.idStr = "REACLAW_PANEL_TOGGLE";
    g_action.name = "ReaClaw: Control Panel";
    g_action.extra = nullptr;
    g_cmd_id = plugin_register("custom_action", &g_action);
    if (!g_cmd_id) {
        Log::warn("ReaClaw: failed to register control panel action");
        return;
    }

    plugin_register("hookcommand", (void*)hookcommand);
    plugin_register("toggleaction", (void*)toggleaction);

    // Create modeless dockable dialog.
    // On Linux/macOS: CreateDialogParam is a SWELL macro → SWELL_CreateDialog,
    //   which finds the inline resource registered via SWELL_DEFINE_DIALOG_RESOURCE_BEGIN.
    // On Windows: native Win32 CreateDialogParam, resource comes from panel.rc.
    g_hwnd = CreateDialogParam(
            (HINSTANCE)hInstance, MAKEINTRESOURCE(IDD_REACLAW_PANEL), GetMainHwnd(), dlgproc, 0);
    if (!g_hwnd) {
#ifdef _WIN32
        // Log the Win32 error code via the breadcrumb file since the REAPER
        // console may not be available at this point.
        {
            char tmp[MAX_PATH];
            DWORD n = GetTempPathA(MAX_PATH, tmp);
            if (n > 0 && n < MAX_PATH) {
                lstrcatA(tmp, "reaclaw-diag.txt");
                HANDLE h = CreateFileA(tmp, GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                                       FILE_ATTRIBUTE_NORMAL, nullptr);
                if (h != INVALID_HANDLE_VALUE) {
                    char buf[128];
                    wsprintfA(buf, "5: CreateDialogParam failed GLE=%lu hInstance=%p\n",
                              GetLastError(), hInstance);
                    SetFilePointer(h, 0, nullptr, FILE_END);
                    DWORD written;
                    WriteFile(h, buf, lstrlenA(buf), &written, nullptr);
                    CloseHandle(h);
                }
            }
        }
#endif
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

// Create (or recreate) the dialog window.  REAPER can destroy the HWND during
// DockWindowRemove on Windows, so callers must not assume g_hwnd survives an
// undock operation.  Returns true if g_hwnd is valid on exit.
static bool create_window() {
    if (g_hwnd && IsWindow(g_hwnd))
        return true;
    g_hwnd = nullptr;  // clear any stale handle
    if (!GetMainHwnd)
        return false;
    g_hwnd = CreateDialogParam(
            (HINSTANCE)g_hInstance, MAKEINTRESOURCE(IDD_REACLAW_PANEL),
            GetMainHwnd(), dlgproc, 0);
    if (!g_hwnd) {
        Log::warn("ReaClaw: failed to (re)create control panel window");
        return false;
    }
    capture_layout(g_hwnd);
    return true;
}

void show_toggle() {
    // Recreate the window if REAPER destroyed it (e.g. during DockWindowRemove).
    if (!create_window())
        return;

    bool docked = DockIsChildOfDock && DockIsChildOfDock(g_hwnd, nullptr) >= 0;
    if (docked) {
        // Docked: inactive tabs are hidden by the docker — always activate.
        ShowWindow(g_hwnd, SW_SHOW);
        if (DockWindowActivate)
            DockWindowActivate(g_hwnd);
    } else if (IsWindowVisible(g_hwnd)) {
        // Floating and visible: hide it.
        ShowWindow(g_hwnd, SW_HIDE);
    } else {
        // Floating and hidden (including after undock): show and bring to front.
        populate(g_hwnd);
        ShowWindow(g_hwnd, SW_SHOW);
        SetForegroundWindow(g_hwnd);
    }
}

void destroy() {
    if (g_cmd_id && plugin_register) {
        plugin_register("-hookcommand", (void*)hookcommand);
        plugin_register("-toggleaction", (void*)toggleaction);
        plugin_register("-custom_action", &g_action);
    }
    if (g_hwnd) {
        if (DockWindowRemove) {
            DockWindowRemove(g_hwnd);
        }
        DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
    }
}

}  // namespace Panel
}  // namespace ReaClaw

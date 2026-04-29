// ReaClaw dockable control panel.
//
// Layout (260×224 dialog units):
//   Row 1  y=4-18    [LED] Status text              [Start/Stop]
//   Sep    y=22      Horizontal rule
//   Row 2  y=26-56   Host: [___]  Port: [___]
//                    [x] Bypass TLS cert validation
//   Sep    y=60      Horizontal rule
//   Row 3  y=64-204  Log:  [Refresh] [Clear]
//                    [log area — stretches with window]
//   Row 4  y=206     [Apply]
//
// UX notes:
//   - Start/Stop acts immediately; no Apply needed for server toggle.
//   - Apply saves host/port/TLS and restarts the server if it was running.
//   - A 2-second WM_TIMER keeps the status LED and text current.
//   - Log refresh scrolls to the bottom automatically.

#ifndef _WIN32

// Tell SWELL headers "function pointer storage is in swell_stub.cpp"
#define SWELL_PROVIDED_BY_APP
#include "WDL/swell/swell.h"

// ---- Dialog resource definition (non-Windows) ----------------------------
// SWELL_DLG_WS_DEFAULT_SCALING was added to WDL in 2025 but is only defined
// under SWELL_TARGET_OSX. Guard it here so Linux builds don't fail when the
// WDL repo advances past the version we vendored.
#ifndef SWELL_DLG_WS_DEFAULT_SCALING
#define SWELL_DLG_WS_DEFAULT_SCALING 0
#endif
#include "WDL/swell/swell-dlggen.h"
#include "panel/resource.h"

// clang-format off
SWELL_DEFINE_DIALOG_RESOURCE_BEGIN(IDD_REACLAW_PANEL, SWELL_DLG_WS_RESIZABLE, "ReaClaw", 260, 224, 1.5)
BEGIN
  LTEXT    "",                              IDC_STATUS_LED,    6,   6,  10, 10
  LTEXT    "Stopped",                       IDC_STATUS_TEXT,  20,   6, 172, 10
  PUSHBUTTON "Start",                       IDC_START_STOP,  198,   4,  56, 14
  CONTROL  "", IDC_SEP1, "Static", SS_ETCHEDHORZ,             6,  22, 248,  1
  LTEXT    "Host:",                         IDC_STATIC,        6,  30,  26, 10
  EDITTEXT IDC_HOST,                        36,  28, 128, 12, ES_AUTOHSCROLL|WS_TABSTOP
  LTEXT    "Port:",                         IDC_STATIC,      170,  30,  22, 10
  EDITTEXT IDC_PORT,                        196,  28,  58, 12, ES_AUTOHSCROLL|WS_TABSTOP
  CONTROL  "Bypass TLS cert validation",    IDC_TLS_BYPASS, "Button", BS_AUTOCHECKBOX|WS_TABSTOP, 6, 44, 220, 12
  CONTROL  "", IDC_SEP2, "Static", SS_ETCHEDHORZ,             6,  60, 248,  1
  LTEXT    "Log:",                          IDC_STATIC,        6,  68,  20, 10
  PUSHBUTTON "Refresh",                     IDC_REFRESH,     168,  66,  40, 12
  PUSHBUTTON "Clear",                       IDC_CLEAR_LOG,   212,  66,  42, 12
  EDITTEXT IDC_LOG,                          6,  80, 248, 122, ES_READONLY|ES_MULTILINE|WS_VSCROLL|WS_TABSTOP
  PUSHBUTTON "Apply",                       IDC_APPLY,       200, 206,  54, 14
END
SWELL_DEFINE_DIALOG_RESOURCE_END(IDD_REACLAW_PANEL)
// clang-format on

#include "WDL/swell/swell-menugen.h"
// Restore BEGIN/END so they don't pollute later code
#undef BEGIN
#undef END

#endif  // !_WIN32

// ---- Normal includes (after the resource block) ---------------------------
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
// Resource IDs live in panel.rc on Windows; include them explicitly.
#include "panel/resource.h"
// BST_CHECKED / BST_UNCHECKED
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

// ---- Status LED brushes --------------------------------------------------
// Created in WM_INITDIALOG, destroyed in WM_DESTROY.
// The dialog may be destroyed and recreated on Windows during dock/undock,
// so each lifecycle pair (INITDIALOG … DESTROY) is self-contained.
static HBRUSH g_led_running_brush = nullptr;  // green
static HBRUSH g_led_stopped_brush = nullptr;  // gray

static const UINT k_refresh_timer = 1;
static const UINT k_refresh_interval = 2000;  // ms — status LED / text update

// ---- Layout state (captured once at WM_INITDIALOG) -----------------------

struct CtrlRect {
    int x, y, w, h;
};

static int g_init_cx = 0;
static int g_init_cy = 0;
static CtrlRect g_status_text_r = {};
static CtrlRect g_start_stop_r = {};
static CtrlRect g_sep1_r = {};
static CtrlRect g_sep2_r = {};
static CtrlRect g_log_r = {};
static CtrlRect g_refresh_r = {};
static CtrlRect g_clear_log_r = {};
static CtrlRect g_apply_r = {};

static CtrlRect get_ctrl_rect(HWND parent, int id) {
    RECT rc = {};
    GetWindowRect(GetDlgItem(parent, id), &rc);
    // ScreenToClient is cross-platform (Win32 + SWELL); MapWindowPoints is Win32-only.
    POINT pt = {rc.left, rc.top};
    ScreenToClient(parent, &pt);
    return {pt.x, pt.y, rc.right - rc.left, rc.bottom - rc.top};
}

static void capture_layout(HWND hwnd) {
    RECT client = {};
    GetClientRect(hwnd, &client);
    g_init_cx = client.right;
    g_init_cy = client.bottom;
    g_status_text_r = get_ctrl_rect(hwnd, IDC_STATUS_TEXT);
    g_start_stop_r = get_ctrl_rect(hwnd, IDC_START_STOP);
    g_sep1_r = get_ctrl_rect(hwnd, IDC_SEP1);
    g_sep2_r = get_ctrl_rect(hwnd, IDC_SEP2);
    g_log_r = get_ctrl_rect(hwnd, IDC_LOG);
    g_refresh_r = get_ctrl_rect(hwnd, IDC_REFRESH);
    g_clear_log_r = get_ctrl_rect(hwnd, IDC_CLEAR_LOG);
    g_apply_r = get_ctrl_rect(hwnd, IDC_APPLY);
}

static void relayout(HWND hwnd, int cx, int cy) {
    int dx = cx - g_init_cx;
    int dy = cy - g_init_cy;

    // BeginDeferWindowPos/DeferWindowPos/EndDeferWindowPos are Win32-only;
    // use individual SetWindowPos calls which work on Win32 and SWELL.
    auto move = [&](int id, const CtrlRect& r, int dw, int dh, int mx, int my) {
        SetWindowPos(GetDlgItem(hwnd, id),
                     nullptr,
                     r.x + mx,
                     r.y + my,
                     r.w + dw,
                     r.h + dh,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    };

    move(IDC_STATUS_TEXT, g_status_text_r, dx, 0, 0, 0);  // stretch right
    move(IDC_START_STOP, g_start_stop_r, 0, 0, dx, 0);    // slide right
    move(IDC_SEP1, g_sep1_r, dx, 0, 0, 0);                // stretch right
    move(IDC_SEP2, g_sep2_r, dx, 0, 0, 0);                // stretch right
    move(IDC_LOG, g_log_r, dx, dy, 0, 0);                 // stretch right+down
    move(IDC_REFRESH, g_refresh_r, 0, 0, dx, 0);          // slide right
    move(IDC_CLEAR_LOG, g_clear_log_r, 0, 0, dx, 0);      // slide right
    move(IDC_APPLY, g_apply_r, 0, 0, dx, dy);             // slide right+down
}

// ---- Helpers ---------------------------------------------------------------

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
#ifdef _WIN32
    // Win32 ES_MULTILINE edit controls require CRLF; log files use Unix LF.
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

// Scroll the log edit control to the last line so the newest entries are visible.
static void scroll_log_to_bottom(HWND hwnd) {
    HWND log = GetDlgItem(hwnd, IDC_LOG);
    if (!log)
        return;
    // WM_GETTEXTLENGTH and EM_SCROLLCARET are Win32-only; use GetWindowTextLength
    // and EM_SCROLL (SB_BOTTOM) which are supported on Win32 and SWELL.
    int len = GetWindowTextLength(log);
    SendMessage(log, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(log, EM_SCROLL, SB_BOTTOM, 0);
}

// Load log from disk, push it into the edit control, and scroll to bottom.
static void refresh_log(HWND hwnd) {
    SetDlgItemText(hwnd, IDC_LOG, read_log().c_str());
    scroll_log_to_bottom(hwnd);
}

// Update the status LED color, status text, and Start/Stop button label to
// reflect the current server state.  Called from WM_TIMER and after any
// action that may change server state.
static void update_status(HWND hwnd) {
    bool running = Server::is_running();

    SetDlgItemText(hwnd, IDC_START_STOP, running ? "Stop" : "Start");

    std::string status;
    if (running) {
        const char* scheme = g_config.tls_enabled ? "https" : "http";
        status = std::string("Running  ") + scheme + "://" + g_config.host + ":" +
                 std::to_string(g_config.port);
    } else {
        status = "Stopped";
    }
    SetDlgItemText(hwnd, IDC_STATUS_TEXT, status.c_str());

    // Trigger a repaint of the LED square so WM_CTLCOLORSTATIC re-evaluates
    // the running state and returns the correct brush color.
    InvalidateRect(GetDlgItem(hwnd, IDC_STATUS_LED), nullptr, TRUE);
}

static void populate(HWND hwnd) {
    SetDlgItemText(hwnd, IDC_HOST, g_config.host.c_str());
    SetDlgItemText(hwnd, IDC_PORT, std::to_string(g_config.port).c_str());
    CheckDlgButton(hwnd, IDC_TLS_BYPASS, !g_config.tls_enabled ? BST_CHECKED : BST_UNCHECKED);
    refresh_log(hwnd);
    update_status(hwnd);
}

static bool create_window();  // defined after init(); forward-declared for dlgproc

// ---- Dialog proc -----------------------------------------------------------

static INT_PTR WINAPI dlgproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG:
            // Create LED brushes for the status indicator square.
            g_led_running_brush = CreateSolidBrush(RGB(0, 192, 64));     // green
            g_led_stopped_brush = CreateSolidBrush(RGB(110, 110, 110));  // gray
            populate(hwnd);
            capture_layout(hwnd);
            // Periodic timer: keeps status text and LED current without
            // requiring the user to manually refresh.
            SetTimer(hwnd, k_refresh_timer, k_refresh_interval, nullptr);
            return 0;

        case WM_TIMER:
            if (wParam == k_refresh_timer)
                update_status(hwnd);
            return 0;

        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED)
                relayout(hwnd, LOWORD(lParam), HIWORD(lParam));
            return 0;

        // Paint the status LED square: return the appropriate solid brush so
        // the static control's background becomes the LED color.
        case WM_CTLCOLORSTATIC: {
            // GetDlgCtrlID is Win32-only; compare HWNDs directly (works everywhere).
            if ((HWND)lParam == GetDlgItem(hwnd, IDC_STATUS_LED)) {
                bool running = Server::is_running();
                COLORREF clr = running ? RGB(0, 192, 64) : RGB(110, 110, 110);
                SetBkColor((HDC)wParam, clr);
                return (LRESULT)(running ? g_led_running_brush : g_led_stopped_brush);
            }
            // All other static controls: use default dialog handling.
            return 0;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                // Start/Stop acts immediately — no Apply required.
                case IDC_START_STOP: {
                    if (Server::is_running()) {
                        Server::stop();
                        Log::info("ReaClaw: server stopped via control panel");
                    } else {
                        Server::start(g_config);
                        Log::info("ReaClaw: server started via control panel");
                    }
                    update_status(hwnd);
                    return 0;
                }

                case IDC_REFRESH:
                    refresh_log(hwnd);
                    return 0;

                case IDC_CLEAR_LOG:
                    SetDlgItemText(hwnd, IDC_LOG, "");
                    return 0;

                case IDC_APPLY: {
                    // Read current field values into config.
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

                    // Restart server to pick up the changed host/port/TLS settings.
                    if (Server::is_running()) {
                        Server::stop();
                        Server::start(g_config);
                        Log::info("ReaClaw: server restarted with new settings");
                    }

                    update_status(hwnd);
                    return 0;
                }
            }
            return 0;

        case WM_CONTEXTMENU: {
            bool running = Server::is_running();
            bool docked = DockIsChildOfDock && DockIsChildOfDock(hwnd, nullptr) >= 0;

            // AppendMenuA is Win32-only; InsertMenu (aliased to SWELL_InsertMenu on
            // Linux/macOS) works cross-platform with MF_BYPOSITION to append.
            HMENU menu = CreatePopupMenu();
            InsertMenu(menu,
                       0,
                       MF_BYPOSITION | MF_STRING,
                       1,
                       running ? "Stop Server" : "Start Server");
            InsertMenu(menu, 1, MF_BYPOSITION | MF_STRING, 2, "Refresh Log");
            InsertMenu(menu, 2, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
            InsertMenu(menu,
                       3,
                       MF_BYPOSITION | MF_STRING | (docked ? MF_CHECKED : 0),
                       3,
                       "Dock in Docker");
            InsertMenu(menu, 4, MF_BYPOSITION | MF_STRING, 4, "Close");

            // Cast to SHORT for multi-monitor support (coordinates can be negative).
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
                    update_status(hwnd);
                    break;
                case 2:
                    refresh_log(hwnd);
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
            // Hide rather than destroy so dock state is preserved.
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, k_refresh_timer);
            if (g_led_running_brush) {
                DeleteObject(g_led_running_brush);
                g_led_running_brush = nullptr;
            }
            if (g_led_stopped_brush) {
                DeleteObject(g_led_stopped_brush);
                g_led_stopped_brush = nullptr;
            }
            g_hwnd = nullptr;
            return 0;
    }
    return 0;
}

// ---- REAPER callbacks ------------------------------------------------------

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

// ---- Public API ------------------------------------------------------------

void init(reaper_plugin_info_t* rec, void* hInstance) {
    g_hInstance = hInstance;

#ifndef _WIN32
    // Populate SWELL function pointers from REAPER's SWELL implementation.
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

    // Register the toggle action so it appears in REAPER's Actions list.
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

    g_hwnd = CreateDialogParam(
            (HINSTANCE)hInstance, MAKEINTRESOURCE(IDD_REACLAW_PANEL), GetMainHwnd(), dlgproc, 0);
    if (!g_hwnd) {
#ifdef _WIN32
        {
            char tmp[MAX_PATH];
            DWORD n = GetTempPathA(MAX_PATH, tmp);
            if (n > 0 && n < MAX_PATH) {
                lstrcatA(tmp, "reaclaw-diag.txt");
                HANDLE h = CreateFileA(tmp,
                                       GENERIC_WRITE,
                                       0,
                                       nullptr,
                                       OPEN_ALWAYS,
                                       FILE_ATTRIBUTE_NORMAL,
                                       nullptr);
                if (h != INVALID_HANDLE_VALUE) {
                    char buf[128];
                    wsprintfA(buf,
                              "5: CreateDialogParam failed GLE=%lu hInstance=%p\n",
                              GetLastError(),
                              hInstance);
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
    g_hwnd = nullptr;
    if (!GetMainHwnd)
        return false;
    g_hwnd = CreateDialogParam(
            (HINSTANCE)g_hInstance, MAKEINTRESOURCE(IDD_REACLAW_PANEL), GetMainHwnd(), dlgproc, 0);
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
        if (DockWindowRemove)
            DockWindowRemove(g_hwnd);
        DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
    }
}

}  // namespace Panel
}  // namespace ReaClaw

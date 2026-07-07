// ReaClaw menu dialogs — Status, API key, Log — for the Extensions > ReaClaw
// menu. Each dialog is modeless (created with CreateDialogParam, so it
// never blocks REAPER's main thread) and single-instance.
//
// The non-Windows dialog resources are defined inline via SWELL's dialog-gen
// macros; on Windows the equivalent resources live in dialogs.rc.

#ifndef _WIN32

// Tell SWELL headers "function pointer storage is in swell_stub.cpp".
#ifndef SWELL_PROVIDED_BY_APP
#define SWELL_PROVIDED_BY_APP
#endif
#include "WDL/swell/swell.h"

// SWELL_DLG_WS_DEFAULT_SCALING is only defined under SWELL_TARGET_OSX in newer
// WDL; guard so Linux builds keep working if the vendored WDL advances.
#ifndef SWELL_DLG_WS_DEFAULT_SCALING
#define SWELL_DLG_WS_DEFAULT_SCALING 0
#endif
#include "WDL/swell/swell-dlggen.h"
#include "panel/resource.h"

// clang-format off
SWELL_DEFINE_DIALOG_RESOURCE_BEGIN(IDD_REACLAW_STATUS, 0, "ReaClaw Status", 250, 108, 1.8)
BEGIN
  LTEXT      "",          IDC_ST_LED,        8,   8,  10, 10
  LTEXT      "Stopped",   IDC_ST_STATE,     22,   8, 130, 10
  PUSHBUTTON "Copy address", IDC_ST_COPYADDR, 168, 6,  76, 14
  CONTROL    "", IDC_STATIC, "Static", SS_ETCHEDHORZ, 8, 24, 236, 1
  LTEXT      "Address:",  IDC_STATIC,        8,  32,  56, 10
  LTEXT      "",          IDC_ST_ADDR,      66,  32, 178, 10
  LTEXT      "Auth:",     IDC_STATIC,        8,  46,  56, 10
  LTEXT      "",          IDC_ST_AUTH,      66,  46, 178, 10
  LTEXT      "Uptime:",   IDC_STATIC,        8,  60,  56, 10
  LTEXT      "",          IDC_ST_UPTIME,    66,  60, 178, 10
  LTEXT      "Version:",  IDC_STATIC,        8,  74,  56, 10
  LTEXT      "",          IDC_ST_VERSION,   66,  74, 178, 10
  DEFPUSHBUTTON "Close",  IDOK,            194,  90,  50, 14
END
SWELL_DEFINE_DIALOG_RESOURCE_END(IDD_REACLAW_STATUS)

SWELL_DEFINE_DIALOG_RESOURCE_BEGIN(IDD_REACLAW_APIKEY, 0, "ReaClaw API Key", 300, 82, 1.8)
BEGIN
  LTEXT      "API key:",  IDC_STATIC,        8,   8,  80, 10
  EDITTEXT   IDC_AK_KEY,                      8,  20, 284, 14, ES_AUTOHSCROLL|ES_READONLY|WS_TABSTOP
  PUSHBUTTON "Copy to clipboard", IDC_AK_COPY, 8, 42, 104, 14
  LTEXT      "",          IDC_AK_CONFIRM,  120,  44, 120, 10
  DEFPUSHBUTTON "Close",  IDOK,            242,  60,  50, 14
END
SWELL_DEFINE_DIALOG_RESOURCE_END(IDD_REACLAW_APIKEY)

SWELL_DEFINE_DIALOG_RESOURCE_BEGIN(IDD_REACLAW_LOG, SWELL_DLG_WS_RESIZABLE, "ReaClaw Log", 420, 300, 1.5)
BEGIN
  EDITTEXT   IDC_LG_TEXT,                     6,   6, 408, 266, ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP
  PUSHBUTTON "Refresh",   IDC_LG_REFRESH,     6, 278,  60, 16
  DEFPUSHBUTTON "Close",  IDOK,            358, 278,  56, 16
END
SWELL_DEFINE_DIALOG_RESOURCE_END(IDD_REACLAW_LOG)
// clang-format on

// Restore BEGIN/END so they don't leak into the code below.
#undef BEGIN
#undef END

#endif  // !_WIN32

// ---- Normal includes (after the resource block) ---------------------------
#include "app.h"
#include "config/config.h"
#include "panel/dialogs.h"
#include "server/server.h"
#include "util/logging.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include <reaper_plugin.h>
#include <reaper_plugin_functions.h>

#ifdef _WIN32
#include "panel/resource.h"
#endif

namespace ReaClaw {
namespace Dialogs {

namespace {

void* g_hInstance = nullptr;

HWND g_status_hwnd = nullptr;
HWND g_apikey_hwnd = nullptr;
HWND g_log_hwnd = nullptr;

HBRUSH g_led_running = nullptr;
HBRUSH g_led_stopped = nullptr;

const UINT k_status_timer = 1;
const UINT k_status_interval = 1000;  // ms

HWND main_hwnd() {
    return GetMainHwnd ? GetMainHwnd() : nullptr;
}

std::string current_address() {
    const char* scheme = g_config.tls_enabled ? "https" : "http";
    return std::string(scheme) + "://" + g_config.host + ":" + std::to_string(g_config.port);
}

std::string format_uptime() {
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() -
                                                                 g_start_time)
                        .count();
    long h = (long)(secs / 3600);
    long m = (long)((secs % 3600) / 60);
    long s = (long)(secs % 60);
    char buf[64];
    snprintf(buf, sizeof(buf), "%ldh %ldm %lds", h, m, s);
    return buf;
}

// Copy text to the system clipboard (Win32 + SWELL share this API surface).
void copy_to_clipboard(HWND owner, const std::string& text) {
    if (!OpenClipboard(owner))
        return;
    EmptyClipboard();
    HANDLE hmem = GlobalAlloc(GMEM_MOVEABLE, (int)text.size() + 1);
    if (hmem) {
        void* p = GlobalLock(hmem);
        if (p) {
            std::memcpy(p, text.c_str(), text.size() + 1);
            GlobalUnlock(hmem);
            SetClipboardData(CF_TEXT, hmem);
        }
    }
    CloseClipboard();
}

std::string read_log() {
    if (g_config.log_file.empty())
        return "(no log file configured — ReaClaw messages go to the REAPER console:\n"
               "View > Show console output. Set logging.file in config.json to write to a file.)";
    std::ifstream f(g_config.log_file);
    if (!f)
        return "(cannot open log file: " + g_config.log_file + ")";
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    // Cap so the edit control stays responsive; keep the newest content.
    if (s.size() > 64 * 1024)
        s = "...\n" + s.substr(s.size() - 64 * 1024);
#ifdef _WIN32
    // Win32 multiline edits need CRLF; log files use LF.
    std::string out;
    out.reserve(s.size() + 128);
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

// ---- Status dialog ---------------------------------------------------------

void status_update(HWND hwnd) {
    bool running = Server::is_running();
    SetDlgItemText(hwnd, IDC_ST_STATE, running ? "Running" : "Stopped");
    SetDlgItemText(hwnd, IDC_ST_ADDR, current_address().c_str());
    SetDlgItemText(hwnd, IDC_ST_AUTH, g_config.auth_type.c_str());
    SetDlgItemText(hwnd, IDC_ST_UPTIME, format_uptime().c_str());
    SetDlgItemText(hwnd, IDC_ST_VERSION, REACLAW_VERSION);
    InvalidateRect(GetDlgItem(hwnd, IDC_ST_LED), nullptr, TRUE);
}

INT_PTR WINAPI status_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG:
            if (!g_led_running)
                g_led_running = CreateSolidBrush(RGB(0, 192, 64));
            if (!g_led_stopped)
                g_led_stopped = CreateSolidBrush(RGB(110, 110, 110));
            status_update(hwnd);
            SetTimer(hwnd, k_status_timer, k_status_interval, nullptr);
            return 0;

        case WM_TIMER:
            if (wParam == k_status_timer)
                status_update(hwnd);
            return 0;

        case WM_CTLCOLORSTATIC:
            if ((HWND)lParam == GetDlgItem(hwnd, IDC_ST_LED)) {
                bool running = Server::is_running();
                COLORREF clr = running ? RGB(0, 192, 64) : RGB(110, 110, 110);
                SetBkColor((HDC)wParam, clr);
                return (LRESULT)(running ? g_led_running : g_led_stopped);
            }
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_ST_COPYADDR:
                    copy_to_clipboard(hwnd, current_address());
                    return 0;
                case IDOK:
                case IDCANCEL:
                    DestroyWindow(hwnd);
                    return 0;
            }
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, k_status_timer);
            g_status_hwnd = nullptr;
            return 0;
    }
    return 0;
}

// ---- API key dialog --------------------------------------------------------

INT_PTR WINAPI apikey_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM /*lParam*/) {
    switch (msg) {
        case WM_INITDIALOG:
            SetDlgItemText(hwnd, IDC_AK_KEY, g_config.auth_key.c_str());
            SetDlgItemText(hwnd, IDC_AK_CONFIRM, "");
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_AK_COPY:
                    copy_to_clipboard(hwnd, g_config.auth_key);
                    SetDlgItemText(hwnd, IDC_AK_CONFIRM, "Copied!");
                    return 0;
                case IDOK:
                case IDCANCEL:
                    DestroyWindow(hwnd);
                    return 0;
            }
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            g_apikey_hwnd = nullptr;
            return 0;
    }
    return 0;
}

// ---- Log dialog ------------------------------------------------------------

void log_refresh(HWND hwnd) {
    SetDlgItemText(hwnd, IDC_LG_TEXT, read_log().c_str());
    HWND edit = GetDlgItem(hwnd, IDC_LG_TEXT);
    if (edit) {
        int len = GetWindowTextLength(edit);
        SendMessage(edit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        SendMessage(edit, EM_SCROLL, SB_BOTTOM, 0);
    }
}

INT_PTR WINAPI log_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM /*lParam*/) {
    switch (msg) {
        case WM_INITDIALOG:
            log_refresh(hwnd);
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_LG_REFRESH:
                    log_refresh(hwnd);
                    return 0;
                case IDOK:
                case IDCANCEL:
                    DestroyWindow(hwnd);
                    return 0;
            }
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            g_log_hwnd = nullptr;
            return 0;
    }
    return 0;
}

// Show a modeless dialog, reusing the existing instance if already open.
void show_modeless(HWND& slot, int dlg_id, DLGPROC proc) {
    if (!GetMainHwnd) {
        Log::warn("ReaClaw: cannot show dialog — REAPER main window unavailable");
        return;
    }
    if (slot && IsWindow(slot)) {
        ShowWindow(slot, SW_SHOW);
        SetForegroundWindow(slot);
        return;
    }
    slot = CreateDialogParam((HINSTANCE)g_hInstance, MAKEINTRESOURCE(dlg_id), main_hwnd(), proc, 0);
    if (!slot) {
        Log::warn("ReaClaw: failed to create dialog " + std::to_string(dlg_id));
        return;
    }
    // Position near the top-center of REAPER's main window.
    RECT mr{}, dr{};
    GetWindowRect(main_hwnd(), &mr);
    GetWindowRect(slot, &dr);
    int dw = dr.right - dr.left, dh = dr.bottom - dr.top;
    SetWindowPos(slot,
                 nullptr,
                 mr.left + ((mr.right - mr.left) - dw) / 2,
                 mr.top + 80,
                 dw,
                 dh,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(slot, SW_SHOW);
    SetForegroundWindow(slot);
}

}  // namespace

// ---- Public API ------------------------------------------------------------

void init(void* hInstance) {
    g_hInstance = hInstance;
}

void show_status() {
    show_modeless(g_status_hwnd, IDD_REACLAW_STATUS, status_proc);
}

void show_api_key() {
    show_modeless(g_apikey_hwnd, IDD_REACLAW_APIKEY, apikey_proc);
}

void show_log() {
    show_modeless(g_log_hwnd, IDD_REACLAW_LOG, log_proc);
}

}  // namespace Dialogs
}  // namespace ReaClaw

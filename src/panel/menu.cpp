// ReaClaw "Extensions" menu.
//
// Replaces the former dockable control panel with a lightweight section under
// REAPER's main "Extensions" menu:
//
//   Extensions > ReaClaw >
//     Start/stop server   (checked while the server is running)
//     Status...           (host:port, uptime, version — message box)
//     Open config file
//     View log
//     ----
//     Copy API key
//
// Each item is a registered REAPER action, so it also appears in the Actions
// list and can be bound to a key or toolbar button. The submenu is built in the
// hookcustommenu callback per the REAPER SDK contract:
//   flag 0 -> add the structure (items / submenu)
//   flag 1 -> set dynamic state (the Start/stop check mark)

#ifndef _WIN32
// Tell SWELL headers "function pointer storage is in swell_stub.cpp".
// (CMake also defines this globally; guard against redefinition.)
#ifndef SWELL_PROVIDED_BY_APP
#define SWELL_PROVIDED_BY_APP
#endif
#include "WDL/swell/swell.h"

#include <dlfcn.h>
// SWELL_dllMain is defined in swell_stub.cpp.
extern "C" int SWELL_dllMain(HINSTANCE hInst, DWORD callMode, LPVOID getFunc);
#else
// clang-format off
// windows.h must precede shellapi.h — it defines the base Win32 types
// (UINT, DWORD, POINT, EXTERN_C, ...) that shellapi.h relies on. Pinned so
// clang-format's alphabetical include sort doesn't reorder them.
#include <windows.h>
#include <shellapi.h>
// clang-format on
#endif

#include "app.h"
#include "config/config.h"
#include "panel/menu.h"
#include "server/server.h"
#include "util/logging.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <reaper_plugin.h>
#include <reaper_plugin_functions.h>

namespace ReaClaw {
namespace Menu {

// One registered action per menu item. The struct must outlive registration,
// so these are file-static.
static custom_action_register_t g_a_toggle = {};
static custom_action_register_t g_a_status = {};
static custom_action_register_t g_a_config = {};
static custom_action_register_t g_a_log = {};
static custom_action_register_t g_a_copykey = {};

static int g_cmd_toggle = 0;
static int g_cmd_status = 0;
static int g_cmd_config = 0;
static int g_cmd_log = 0;
static int g_cmd_copykey = 0;

// ---- Helpers ---------------------------------------------------------------

// Register a custom action in the Main section; returns its command id (0 = fail).
static int reg_action(custom_action_register_t& a, const char* id_str, const char* name) {
    a.uniqueSectionId = 0;  // Main section
    a.idStr = id_str;
    a.name = name;
    a.extra = nullptr;
    return plugin_register("custom_action", &a);
}

// Open a file with the OS default handler. Paths are config-derived (under the
// REAPER resource directory), not attacker-controlled.
static void open_with_default_app(const std::string& path) {
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::string cmd = "open \"" + path + "\" >/dev/null 2>&1 &";
    if (std::system(cmd.c_str()) != 0) { /* best-effort */
    }
#else
    std::string cmd = "xdg-open \"" + path + "\" >/dev/null 2>&1 &";
    if (std::system(cmd.c_str()) != 0) { /* best-effort */
    }
#endif
}

// Copy text to the system clipboard (Win32 + SWELL share this API surface).
static void copy_to_clipboard(HWND owner, const std::string& text) {
    if (!OpenClipboard(owner))
        return;
    EmptyClipboard();
    HANDLE h = GlobalAlloc(GMEM_MOVEABLE, (int)text.size() + 1);
    if (h) {
        void* p = GlobalLock(h);
        if (p) {
            std::memcpy(p, text.c_str(), text.size() + 1);
            GlobalUnlock(h);
            SetClipboardData(CF_TEXT, h);
        }
    }
    CloseClipboard();
}

static std::string format_uptime() {
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

static void show_status() {
    bool running = Server::is_running();
    const char* scheme = g_config.tls_enabled ? "https" : "http";
    std::string msg;
    msg += "Server:   ";
    msg += running ? "running" : "stopped";
    msg += "\n";
    msg += "Address:  ";
    msg += scheme;
    msg += "://" + g_config.host + ":" + std::to_string(g_config.port) + "\n";
    msg += "Auth:     " + g_config.auth_type + "\n";
    msg += "Uptime:   " + format_uptime() + "\n";
    msg += "Version:  " REACLAW_VERSION;
    MessageBox(GetMainHwnd ? GetMainHwnd() : nullptr, msg.c_str(), "ReaClaw Status", MB_OK);
}

static void toggle_server() {
    if (Server::is_running()) {
        Server::stop();
        Log::info("ReaClaw: server stopped via Extensions menu");
    } else {
        Server::start(g_config);
        Log::info("ReaClaw: server started via Extensions menu");
    }
}

// ---- REAPER callbacks ------------------------------------------------------

// custom_action-registered actions dispatch through hookcommand2 (not the
// older main-section-only hookcommand) — see custom_action_register_t in
// reaper_plugin.h. Command IDs are globally unique, so the section is ignored.
static bool hookcommand2(KbdSectionInfo* /*sec*/,
                         int cmd,
                         int /*val*/,
                         int /*valhw*/,
                         int /*relmode*/,
                         HWND /*hwnd*/) {
    if (cmd == 0)
        return false;
    if (cmd == g_cmd_toggle) {
        toggle_server();
        return true;
    }
    if (cmd == g_cmd_status) {
        show_status();
        return true;
    }
    if (cmd == g_cmd_config) {
        open_with_default_app(g_config.resource_dir + "config.json");
        return true;
    }
    if (cmd == g_cmd_log) {
        if (g_config.log_file.empty())
            MessageBox(GetMainHwnd ? GetMainHwnd() : nullptr,
                       "No log file is configured.\nReaClaw is logging to the REAPER console "
                       "(View > Show console output).\n\nSet logging.file in config.json to write "
                       "to a file.",
                       "ReaClaw Log",
                       MB_OK);
        else
            open_with_default_app(g_config.log_file);
        return true;
    }
    if (cmd == g_cmd_copykey) {
        copy_to_clipboard(GetMainHwnd ? GetMainHwnd() : nullptr, g_config.auth_key);
        std::string msg = "API key copied to clipboard:\n\n" + g_config.auth_key;
        MessageBox(GetMainHwnd ? GetMainHwnd() : nullptr, msg.c_str(), "ReaClaw API Key", MB_OK);
        return true;
    }
    return false;
}

// Report the Start/stop action's toggle state so the Actions list / toolbar
// buttons reflect whether the server is running.
static int toggleaction(int cmd) {
    if (cmd == g_cmd_toggle)
        return Server::is_running() ? 1 : 0;
    return -1;
}

// Build / update the "ReaClaw" submenu under the main "Extensions" menu.
static void menuhook(const char* menuidstr, HMENU menu, int flag) {
    if (!menu || !menuidstr || strcmp(menuidstr, "Main extensions") != 0)
        return;

    if (flag == 0) {
        // Add the static submenu structure.
        HMENU sub = CreatePopupMenu();
        InsertMenu(sub, 0, MF_BYPOSITION | MF_STRING, (UINT_PTR)g_cmd_toggle, "Start/stop server");
        InsertMenu(sub, 1, MF_BYPOSITION | MF_STRING, (UINT_PTR)g_cmd_status, "Status...");
        InsertMenu(sub, 2, MF_BYPOSITION | MF_STRING, (UINT_PTR)g_cmd_config, "Open config file");
        InsertMenu(sub, 3, MF_BYPOSITION | MF_STRING, (UINT_PTR)g_cmd_log, "View log");
        InsertMenu(sub, 4, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
        InsertMenu(sub, 5, MF_BYPOSITION | MF_STRING, (UINT_PTR)g_cmd_copykey, "Copy API key");

        int pos = GetMenuItemCount(menu);
        InsertMenu(menu, pos, MF_BYPOSITION | MF_POPUP | MF_STRING, (UINT_PTR)sub, "ReaClaw");
    } else if (flag == 1) {
        // Reflect live server state on the toggle item (by command, recursing
        // into our submenu).
        CheckMenuItem(menu,
                      g_cmd_toggle,
                      MF_BYCOMMAND | (Server::is_running() ? MF_CHECKED : MF_UNCHECKED));
    }
}

// ---- Public API ------------------------------------------------------------

void init(reaper_plugin_info_t* /*rec*/, void* hInstance) {
#ifndef _WIN32
    // Populate SWELL function pointers from REAPER's embedded SWELL.
    {
        using SwellGetFunc_t = void* (*)(const char*);
        auto swellGetFunc = reinterpret_cast<SwellGetFunc_t>(
                dlsym(RTLD_DEFAULT, "SWELLAPI_GetFunc"));
        if (swellGetFunc) {
            SWELL_dllMain((HINSTANCE)hInstance,
                          DLL_PROCESS_ATTACH,
                          reinterpret_cast<void*>(swellGetFunc));
        } else {
            Log::warn("ReaClaw: SWELLAPI_GetFunc not found — Extensions menu unavailable");
            return;
        }
    }
#else
    (void)hInstance;
#endif

    if (!plugin_register) {
        Log::warn("ReaClaw: plugin_register null — Extensions menu unavailable");
        return;
    }

    g_cmd_toggle = reg_action(g_a_toggle, "REACLAW_SERVER_TOGGLE", "ReaClaw: Start/stop server");
    g_cmd_status = reg_action(g_a_status, "REACLAW_STATUS", "ReaClaw: Status");
    g_cmd_config = reg_action(g_a_config, "REACLAW_OPEN_CONFIG", "ReaClaw: Open config file");
    g_cmd_log = reg_action(g_a_log, "REACLAW_VIEW_LOG", "ReaClaw: View log");
    g_cmd_copykey = reg_action(g_a_copykey, "REACLAW_COPY_API_KEY", "ReaClaw: Copy API key");

    plugin_register("hookcommand2", (void*)hookcommand2);
    plugin_register("toggleaction", (void*)toggleaction);
    plugin_register("hookcustommenu", (void*)menuhook);

    if (AddExtensionsMainMenu)
        AddExtensionsMainMenu();

    Log::info("ReaClaw: Extensions menu ready");
}

void destroy() {
    if (!plugin_register)
        return;
    plugin_register("-hookcustommenu", (void*)menuhook);
    plugin_register("-hookcommand2", (void*)hookcommand2);
    plugin_register("-toggleaction", (void*)toggleaction);
    plugin_register("-custom_action", &g_a_toggle);
    plugin_register("-custom_action", &g_a_status);
    plugin_register("-custom_action", &g_a_config);
    plugin_register("-custom_action", &g_a_log);
    plugin_register("-custom_action", &g_a_copykey);
}

}  // namespace Menu
}  // namespace ReaClaw

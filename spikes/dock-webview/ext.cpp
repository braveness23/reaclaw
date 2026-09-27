// Dock spike: can a web view live inside a REAPER dock on Linux?
//
// The web view runs in a separate helper process (dockspike-webhost, GTK +
// WebKitGTK). REAPER's libSwell drives GDK but does not forward events to GTK
// widgets, so an in-process GTK widget would never see input. The helper's
// GtkPlug X window is reparented into whichever REAPER toplevel currently
// holds our panel, and kept lined up over the panel's client area on a timer.

#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define SWELL_PROVIDED_BY_APP
#include "WDL/swell/swell.h"
#ifndef SWELL_DLG_WS_DEFAULT_SCALING
#define SWELL_DLG_WS_DEFAULT_SCALING 0
#endif
#include "WDL/swell/swell-dlggen.h"

#define REAPERAPI_IMPLEMENT
#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_plugin_register
#define REAPERAPI_WANT_GetMainHwnd
#define REAPERAPI_WANT_DockWindowAddEx
#define REAPERAPI_WANT_DockWindowActivate
#define REAPERAPI_WANT_DockWindowRemove
#define REAPERAPI_WANT_DockIsChildOfDock
#define REAPERAPI_WANT_GetExtState
#define REAPERAPI_WANT_SetExtState
#define REAPERAPI_WANT_ShowConsoleMsg
#define REAPERAPI_WANT_AddExtensionsMainMenu
#include "reaper_plugin_functions.h"

extern "C" int SWELL_dllMain(HINSTANCE hInst, DWORD callMode, LPVOID getFunc);

enum { IDD_DOCKED = 100, IDD_FLOAT = 101, IDC_LABEL = 1001 };

// clang-format off
SWELL_DEFINE_DIALOG_RESOURCE_BEGIN(IDD_DOCKED, SWELL_DLG_WS_CHILD | SWELL_DLG_WS_RESIZABLE, "ReaClaw Chat (dock test)", 300, 200, 1.8)
BEGIN
  LTEXT "waiting for web view...", IDC_LABEL, 4, 4, 200, 10
END
SWELL_DEFINE_DIALOG_RESOURCE_END(IDD_DOCKED)

SWELL_DEFINE_DIALOG_RESOURCE_BEGIN(IDD_FLOAT, SWELL_DLG_WS_RESIZABLE, "ReaClaw Chat (dock test)", 300, 200, 1.8)
BEGIN
  LTEXT "waiting for web view...", IDC_LABEL, 4, 4, 200, 10
END
SWELL_DEFINE_DIALOG_RESOURCE_END(IDD_FLOAT)
// clang-format on

static const char* kIdent = "reaclaw_dockspike";
static const char* kSection = "reaclaw_dockspike";

static HINSTANCE g_inst;
static HWND g_hwnd;
static bool g_docked = true;

static custom_action_register_t g_a_toggle, g_a_dock, g_a_shrink;
static int g_cmd_toggle, g_cmd_dock, g_cmd_shrink;

// ---- Xlib / GDK, resolved at runtime (libSwell loads both) ----------------

typedef void* (*gdk_display_get_default_t)();
typedef void* (*gdk_x11_display_get_xdisplay_t)(void*);
typedef unsigned long (*gdk_x11_window_get_xid_t)(void*);
typedef int (*XReparentWindow_t)(void*, unsigned long, unsigned long, int, int);
typedef int (*XMoveResizeWindow_t)(void*, unsigned long, int, int, unsigned, unsigned);
typedef int (*XWin_t)(void*, unsigned long);
typedef int (*XFlush_t)(void*);

static struct {
    gdk_display_get_default_t display_get_default;
    gdk_x11_display_get_xdisplay_t get_xdisplay;
    gdk_x11_window_get_xid_t get_xid;
    XReparentWindow_t reparent;
    XMoveResizeWindow_t move_resize;
    XWin_t map, unmap, raise;
    XFlush_t flush;
    void* dpy;
    bool ok;
} X;

static void* sym(const char* name) {
    void* p = dlsym(RTLD_DEFAULT, name);
    if (p)
        return p;
    for (const char* lib : {"libgdk-3.so.0", "libX11.so.6"}) {
        void* h = dlopen(lib, RTLD_NOW | RTLD_NOLOAD);
        if (h && (p = dlsym(h, name)))
            return p;
    }
    return nullptr;
}

static bool x_init() {
    if (X.ok)
        return true;
    X.display_get_default = (gdk_display_get_default_t)sym("gdk_display_get_default");
    X.get_xdisplay = (gdk_x11_display_get_xdisplay_t)sym("gdk_x11_display_get_xdisplay");
    X.get_xid = (gdk_x11_window_get_xid_t)sym("gdk_x11_window_get_xid");
    X.reparent = (XReparentWindow_t)sym("XReparentWindow");
    X.move_resize = (XMoveResizeWindow_t)sym("XMoveResizeWindow");
    X.map = (XWin_t)sym("XMapWindow");
    X.unmap = (XWin_t)sym("XUnmapWindow");
    X.raise = (XWin_t)sym("XRaiseWindow");
    X.flush = (XFlush_t)sym("XFlush");
    if (!X.display_get_default || !X.get_xdisplay || !X.get_xid || !X.reparent ||
        !X.move_resize || !X.map || !X.unmap || !X.raise || !X.flush)
        return false;
    void* gd = X.display_get_default();
    X.dpy = gd ? X.get_xdisplay(gd) : nullptr;
    X.ok = X.dpy != nullptr;
    return X.ok;
}

// ---- helper process --------------------------------------------------------

static pid_t g_child = -1;
static int g_to_child = -1, g_from_child = -1;
static unsigned long g_plug = 0;  // helper's GtkPlug X window
static std::string g_rx;

static struct {
    unsigned long parent = 0;
    int x = -1, y = -1, w = -1, h = -1;
    bool mapped = false;
} g_applied;

static std::string helper_path() {
    Dl_info info{};
    dladdr((void*)&helper_path, &info);
    std::string p = info.dli_fname ? info.dli_fname : "";
    size_t slash = p.rfind('/');
    p = (slash == std::string::npos ? std::string(".") : p.substr(0, slash)) +
        "/dockspike-webhost";
    return p;
}

static void log(const char* msg) {
    if (ShowConsoleMsg)
        ShowConsoleMsg(msg);
    fprintf(stderr, "%s", msg);
}

static void stop_helper() {
    if (g_to_child >= 0)
        close(g_to_child);
    if (g_from_child >= 0)
        close(g_from_child);
    g_to_child = g_from_child = -1;
    if (g_child > 0) {
        kill(g_child, SIGTERM);
        waitpid(g_child, nullptr, 0);
    }
    g_child = -1;
    g_plug = 0;
    g_rx.clear();
    g_applied = {};
}

static void start_helper(unsigned long parent_xid) {
    int in[2], out[2];
    if (pipe(in) || pipe(out))
        return;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, in[0], 0);
    posix_spawn_file_actions_adddup2(&fa, out[1], 1);
    posix_spawn_file_actions_addclose(&fa, in[1]);
    posix_spawn_file_actions_addclose(&fa, out[0]);
    std::string path = helper_path();
    char xid[32];
    snprintf(xid, sizeof(xid), "%lu", parent_xid);
    char* argv[] = {(char*)path.c_str(), xid, nullptr};
    extern char** environ;
    if (posix_spawn(&g_child, path.c_str(), &fa, nullptr, argv, environ) != 0) {
        log("dockspike: could not start webhost helper\n");
        g_child = -1;
    }
    posix_spawn_file_actions_destroy(&fa);
    close(in[0]);
    close(out[1]);
    g_to_child = in[1];
    g_from_child = out[0];
    fcntl(g_from_child, F_SETFL, O_NONBLOCK);
    fcntl(g_to_child, F_SETFD, FD_CLOEXEC);
    fcntl(g_from_child, F_SETFD, FD_CLOEXEC);
}

static void read_helper() {
    if (g_from_child < 0)
        return;
    char buf[256];
    ssize_t n;
    while ((n = read(g_from_child, buf, sizeof(buf))) > 0)
        g_rx.append(buf, (size_t)n);
    size_t nl;
    while ((nl = g_rx.find('\n')) != std::string::npos) {
        std::string line = g_rx.substr(0, nl);
        g_rx.erase(0, nl + 1);
        if (line.rfind("XID ", 0) == 0)
            g_plug = strtoul(line.c_str() + 4, nullptr, 10);
    }
}

// ---- keeping the web view over the panel ------------------------------------

// The first ancestor (or self) that owns a real OS window. GetParent falls back
// to the owner for toplevels, so stop at the first window with a GdkWindow.
static HWND os_toplevel(HWND h) {
    while (h && !SWELL_GetOSWindow(h, "GdkWindow"))
        h = GetParent(h);
    return h;
}

static RECT screen_client_rect(HWND h) {
    RECT r;
    GetClientRect(h, &r);
    POINT p = {0, 0};
    ClientToScreen(h, &p);
    return RECT{p.x, p.y, p.x + (r.right - r.left), p.y + (r.bottom - r.top)};
}

static void sync_webview() {
    if (!g_hwnd || !x_init())
        return;
    HWND top = os_toplevel(g_hwnd);
    void* osw = top ? SWELL_GetOSWindow(top, "GdkWindow") : nullptr;
    unsigned long parent = osw ? X.get_xid(osw) : 0;

    if (g_child < 0 && parent)
        start_helper(parent);
    read_helper();
    if (!g_plug || !parent)
        return;

    // SWELL clips children to their ancestors when drawing, but our X window
    // is a separate native window, so clip it ourselves: a panel can be
    // larger than the docker showing it.
    RECT vis = screen_client_rect(g_hwnd);
    for (HWND a = GetParent(g_hwnd); a; a = GetParent(a)) {
        RECT ar = screen_client_rect(a);
        if (!IntersectRect(&vis, &vis, &ar))
            vis = RECT{0, 0, 0, 0};
        if (a == top)
            break;
    }
    RECT tr;
    GetWindowRect(top, &tr);
    int x = vis.left - tr.left, y = vis.top - tr.top;
    int w = vis.right - vis.left, h = vis.bottom - vis.top;
    bool visible = IsWindowVisible(g_hwnd) && w > 1 && h > 1;

    if (parent != g_applied.parent) {
        X.unmap(X.dpy, g_plug);
        X.reparent(X.dpy, g_plug, parent, x, y);
        g_applied.parent = parent;
        g_applied.mapped = false;
        g_applied.x = g_applied.y = g_applied.w = g_applied.h = -1;
    }
    if (visible && (x != g_applied.x || y != g_applied.y || w != g_applied.w ||
                    h != g_applied.h)) {
        X.move_resize(X.dpy, g_plug, x, y, (unsigned)w, (unsigned)h);
        g_applied.x = x;
        g_applied.y = y;
        g_applied.w = w;
        g_applied.h = h;
    }
    if (visible != g_applied.mapped) {
        if (visible) {
            X.map(X.dpy, g_plug);
            X.raise(X.dpy, g_plug);
        } else {
            X.unmap(X.dpy, g_plug);
        }
        g_applied.mapped = visible;
    }
    X.flush(X.dpy);
}

// ---- the panel --------------------------------------------------------------

static void destroy_panel();

static INT_PTR dlgproc(HWND hwnd, UINT msg, WPARAM, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            return 1;
        case WM_CLOSE:
            SetExtState(kSection, "open", "0", true);
            destroy_panel();
            return 1;
        case WM_DESTROY:
            if (hwnd == g_hwnd) {
                stop_helper();
                g_hwnd = nullptr;
            }
            return 0;
    }
    return 0;
}

static void create_panel() {
    if (g_hwnd)
        return;
    HWND main = GetMainHwnd();
    g_hwnd = CreateDialogParam(g_inst, MAKEINTRESOURCE(g_docked ? IDD_DOCKED : IDD_FLOAT), main,
                               dlgproc, 0);
    if (!g_hwnd)
        return;
    if (g_docked) {
        DockWindowAddEx(g_hwnd, "ReaClaw Chat (dock test)", kIdent, true);
        DockWindowActivate(g_hwnd);
    } else {
        SetWindowPos(g_hwnd, nullptr, 200, 150, 520, 420, SWP_NOZORDER);
        ShowWindow(g_hwnd, SW_SHOW);
    }
    SetExtState(kSection, "open", "1", true);
    SetExtState(kSection, "docked", g_docked ? "1" : "0", true);
}

static void destroy_panel() {
    if (!g_hwnd)
        return;
    HWND h = g_hwnd;
    stop_helper();
    if (g_docked)
        DockWindowRemove(h);
    g_hwnd = nullptr;
    DestroyWindow(h);
}

static void toggle_panel() {
    if (g_hwnd) {
        SetExtState(kSection, "open", "0", true);
        destroy_panel();
    } else {
        create_panel();
    }
}

static void toggle_dock() {
    bool was_open = g_hwnd != nullptr;
    destroy_panel();
    g_docked = !g_docked;
    SetExtState(kSection, "docked", g_docked ? "1" : "0", true);
    if (was_open)
        create_panel();
}

// ---- REAPER hooks -----------------------------------------------------------

static bool hookcommand2(KbdSectionInfo*, int cmd, int, int, int, HWND) {
    if (cmd && cmd == g_cmd_toggle) {
        toggle_panel();
        return true;
    }
    if (cmd && cmd == g_cmd_dock) {
        toggle_dock();
        return true;
    }
    if (cmd && cmd == g_cmd_shrink) {
        // Test-only: xdotool can't resize REAPER's window without a WM.
        static bool small = false;
        small = !small;
        SetWindowPos(GetMainHwnd(), nullptr, 0, 0, small ? 1000 : 1280, small ? 620 : 800,
                     SWP_NOZORDER);
        return true;
    }
    return false;
}

static int toggleaction(int cmd) {
    if (cmd && cmd == g_cmd_toggle)
        return g_hwnd ? 1 : 0;
    if (cmd && cmd == g_cmd_dock)
        return g_docked ? 1 : 0;
    return -1;
}

static void timer() {
    static bool restored = false;
    if (!restored) {
        restored = true;
        const char* d = GetExtState(kSection, "docked");
        g_docked = !(d && !strcmp(d, "0"));
        const char* o = GetExtState(kSection, "open");
        if (o && !strcmp(o, "1"))
            create_panel();
    }
    sync_webview();
}

extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE hInstance,
                                                                  reaper_plugin_info_t* rec) {
    if (!rec) {
        destroy_panel();
        if (plugin_register) {
            plugin_register("-timer", (void*)timer);
            plugin_register("-hookcommand2", (void*)hookcommand2);
            plugin_register("-toggleaction", (void*)toggleaction);
        }
        return 0;
    }
    if (rec->caller_version != REAPER_PLUGIN_VERSION || REAPERAPI_LoadAPI(rec->GetFunc) != 0)
        return 0;

    auto swell_get = (void* (*)(const char*))dlsym(RTLD_DEFAULT, "SWELLAPI_GetFunc");
    if (!swell_get)
        return 0;
    SWELL_dllMain((HINSTANCE)hInstance, DLL_PROCESS_ATTACH, (void*)swell_get);
    g_inst = (HINSTANCE)hInstance;

    g_a_toggle = {0, "DOCKSPIKE_TOGGLE", "Dock test: show/hide ReaClaw Chat panel", nullptr};
    g_a_dock = {0, "DOCKSPIKE_DOCK", "Dock test: dock/undock ReaClaw Chat panel", nullptr};
    g_cmd_toggle = plugin_register("custom_action", &g_a_toggle);
    g_cmd_dock = plugin_register("custom_action", &g_a_dock);
    g_a_shrink = {0, "DOCKSPIKE_SHRINKMAIN", "Dock test: shrink/restore REAPER window", nullptr};
    g_cmd_shrink = plugin_register("custom_action", &g_a_shrink);
    plugin_register("hookcommand2", (void*)hookcommand2);
    plugin_register("toggleaction", (void*)toggleaction);
    plugin_register("timer", (void*)timer);
    return 1;
}

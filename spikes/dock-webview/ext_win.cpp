// Dock spike: can a web view live inside a REAPER dock on Windows?
//
// Unlike Linux (see ext.cpp), REAPER's dock on Windows uses real native HWNDs,
// not a SWELL translation layer, so a web view control created *inside this
// extension's own process* receives mouse/keyboard input normally — no
// separate helper process or window reparenting needed. The web view is
// WebView2 (Edge/Chromium), created as a child control of our panel HWND and
// resized to fill it on every WM_SIZE.

#include <windows.h>

#include <wrl.h>

#include <cstdio>
#include <string>

#include "WebView2.h"

#define REAPERAPI_IMPLEMENT
#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_plugin_register
#define REAPERAPI_WANT_GetMainHwnd
#define REAPERAPI_WANT_DockWindowAddEx
#define REAPERAPI_WANT_DockWindowActivate
#define REAPERAPI_WANT_DockWindowRemove
#define REAPERAPI_WANT_Dock_UpdateDockID
#define REAPERAPI_WANT_GetExtState
#define REAPERAPI_WANT_SetExtState
#define REAPERAPI_WANT_ShowConsoleMsg
#include "reaper_plugin_functions.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

const char* kIdent = "reaclaw_dockspike";
const char* kSection = "reaclaw_dockspike";
const wchar_t* kWndClass = L"ReaClawDockSpikeWnd";
const wchar_t* kPanelTitle = L"ReaClaw Chat (dock test)";

// Identical test page to the Linux spike (webhost.c's kPage) so both
// platforms are judged against the same fixture.
const char* kPage =
        "<!doctype html><html><head><meta charset=utf-8><style>"
        "html,body{margin:0;height:100%;font:14px/1.45 sans-serif;background:#1d2533;color:#e6e9ef}"
        "body{display:flex;flex-direction:column}"
        "header{padding:8px 12px;background:#2b3a55;display:flex;justify-content:space-between}"
        "#log{flex:1;overflow:auto;padding:12px}"
        ".m{max-width:80%;padding:8px 10px;border-radius:8px;margin:0 0 10px}"
        ".u{background:#3b5b8c;margin-left:auto}.a{background:#2a3242}"
        "code,pre{font-family:monospace;background:#0f141d;border-radius:4px}"
        "pre{padding:8px;white-space:pre-wrap}"
        "footer{display:flex;gap:6px;padding:8px;background:#2b3a55}"
        "input{flex:1;padding:6px;border:0;border-radius:4px}"
        "</style></head><body>"
        "<header><b>ReaClaw Chat &middot; dock test</b><span id=info></span></header>"
        "<div id=log>"
        "<div class='m u'>make the kick louder</div>"
        "<div class='m a'>Raised <b>Kick</b> by <code>+3 dB</code> (now -4.2 dB). Undo?"
        "<pre>POST /state/tracks/0 {\"volume_db\": -4.2}</pre></div>"
        "</div>"
        "<footer><input id=inp placeholder='Type a message'><button>Send</button></footer>"
        "<script>"
        "function tick(){document.getElementById('info').textContent="
        "innerWidth+'\\u00d7'+innerHeight+' px \\u00b7 '+new Date().toLocaleTimeString()}"
        "tick();setInterval(tick,500);addEventListener('resize',tick);"
        "document.querySelector('button').onclick=function(){var i=document.getElementById('inp');"
        "if(!i.value)return;var d=document.createElement('div');d.className='m u';"
        "d.textContent=i.value;document.getElementById('log').appendChild(d);i.value=''};"
        "</script></body></html>";

HINSTANCE g_inst;
HWND g_hwnd;
bool g_docked = true;

ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webview;

custom_action_register_t g_a_toggle, g_a_dock, g_a_shrink, g_a_nextdock;
int g_cmd_toggle, g_cmd_dock, g_cmd_shrink, g_cmd_nextdock;

void log(const char* msg) {
    if (ShowConsoleMsg)
        ShowConsoleMsg(msg);
    fprintf(stderr, "%s", msg);
}

void resize_webview() {
    if (!g_controller || !g_hwnd)
        return;
    RECT bounds;
    GetClientRect(g_hwnd, &bounds);
    g_controller->put_Bounds(bounds);
}

// WebView2 setup is inherently async: environment -> controller -> webview.
// The user-data folder must be writable; REAPER extensions don't get one for
// free, so this uses a spike-only folder under %TEMP%.
void init_webview(HWND hwnd) {
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    std::wstring user_data = std::wstring(temp) + L"reaclaw-dockspike-wv2";

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, user_data.c_str(), nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                    [hwnd](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                        if (FAILED(result) || !env) {
                            log("dockspike: WebView2 environment creation failed — is the "
                                "WebView2 Runtime installed?\n");
                            return result;
                        }
                        env->CreateCoreWebView2Controller(
                                hwnd,
                                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                                        [](HRESULT result2, ICoreWebView2Controller* controller) -> HRESULT {
                                            if (FAILED(result2) || !controller) {
                                                log("dockspike: WebView2 controller creation failed\n");
                                                return result2;
                                            }
                                            g_controller = controller;
                                            g_controller->get_CoreWebView2(&g_webview);
                                            if (g_webview)
                                                g_webview->NavigateToString(
                                                        std::wstring(kPage, kPage + strlen(kPage)).c_str());
                                            resize_webview();
                                            return S_OK;
                                        })
                                        .Get());
                        return S_OK;
                    })
                    .Get());
    if (FAILED(hr))
        log("dockspike: CreateCoreWebView2EnvironmentWithOptions call failed\n");
}

void destroy_panel();

LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            init_webview(hwnd);
            return 0;
        case WM_SIZE:
            resize_webview();
            return 0;
        case WM_CLOSE:
            SetExtState(kSection, "open", "0", true);
            destroy_panel();
            return 0;
        case WM_DESTROY:
            if (hwnd == g_hwnd) {
                g_webview.Reset();
                if (g_controller) {
                    g_controller->Close();
                    g_controller.Reset();
                }
                g_hwnd = nullptr;
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void register_class() {
    static bool done = false;
    if (done)
        return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = g_inst;
    wc.lpszClassName = kWndClass;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    done = true;
}

void create_panel() {
    if (g_hwnd)
        return;
    register_class();
    HWND main = GetMainHwnd();
    if (g_docked) {
        g_hwnd = CreateWindowExW(0, kWndClass, kPanelTitle, WS_CHILD | WS_VISIBLE, 0, 0, 300, 200,
                                  main, nullptr, g_inst, nullptr);
        if (!g_hwnd)
            return;
        DockWindowAddEx(g_hwnd, "ReaClaw Chat (dock test)", kIdent, true);
        DockWindowActivate(g_hwnd);
    } else {
        g_hwnd = CreateWindowExW(0, kWndClass, kPanelTitle,
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE, 200, 150, 520, 420, main,
                                  nullptr, g_inst, nullptr);
        if (!g_hwnd)
            return;
    }
    SetExtState(kSection, "open", "1", true);
    SetExtState(kSection, "docked", g_docked ? "1" : "0", true);
}

void destroy_panel() {
    if (!g_hwnd)
        return;
    HWND h = g_hwnd;
    if (g_docked)
        DockWindowRemove(h);
    DestroyWindow(h);  // WM_DESTROY clears g_hwnd/g_webview/g_controller
}

void toggle_panel() {
    if (g_hwnd) {
        SetExtState(kSection, "open", "0", true);
        destroy_panel();
    } else {
        create_panel();
    }
}

void toggle_dock() {
    bool was_open = g_hwnd != nullptr;
    destroy_panel();
    g_docked = !g_docked;
    SetExtState(kSection, "docked", g_docked ? "1" : "0", true);
    if (was_open)
        create_panel();
}

bool hookcommand2(KbdSectionInfo*, int cmd, int, int, int, HWND) {
    if (cmd && cmd == g_cmd_toggle) {
        toggle_panel();
        return true;
    }
    if (cmd && cmd == g_cmd_dock) {
        toggle_dock();
        return true;
    }
    if (cmd && cmd == g_cmd_nextdock) {
        static int which = 0;
        which = (which + 1) % 4;
        destroy_panel();
        g_docked = true;
        Dock_UpdateDockID(kIdent, which);
        create_panel();
        return true;
    }
    if (cmd && cmd == g_cmd_shrink) {
        static bool shrunk = false;
        shrunk = !shrunk;
        SetWindowPos(GetMainHwnd(), nullptr, 0, 0, shrunk ? 1000 : 1280, shrunk ? 620 : 800,
                     SWP_NOZORDER | SWP_NOMOVE);
        return true;
    }
    return false;
}

int toggleaction(int cmd) {
    if (cmd && cmd == g_cmd_toggle)
        return g_hwnd ? 1 : 0;
    if (cmd && cmd == g_cmd_dock)
        return g_docked ? 1 : 0;
    return -1;
}

void timer() {
    static bool restored = false;
    if (!restored) {
        restored = true;
        const char* d = GetExtState(kSection, "docked");
        g_docked = !(d && !strcmp(d, "0"));
        const char* o = GetExtState(kSection, "open");
        if (o && !strcmp(o, "1"))
            create_panel();
    }
}

}  // namespace

extern "C" __declspec(dllexport) int REAPER_PLUGIN_ENTRYPOINT(HINSTANCE hInstance,
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

    g_inst = hInstance;

    g_a_toggle = {0, "DOCKSPIKE_TOGGLE", "Dock test: show/hide ReaClaw Chat panel", nullptr};
    g_a_dock = {0, "DOCKSPIKE_DOCK", "Dock test: dock/undock ReaClaw Chat panel", nullptr};
    g_cmd_toggle = plugin_register("custom_action", &g_a_toggle);
    g_cmd_dock = plugin_register("custom_action", &g_a_dock);
    g_a_shrink = {0, "DOCKSPIKE_SHRINKMAIN", "Dock test: shrink/restore REAPER window", nullptr};
    g_cmd_shrink = plugin_register("custom_action", &g_a_shrink);
    g_a_nextdock = {0, "DOCKSPIKE_NEXTDOCK", "Dock test: move panel to next docker", nullptr};
    g_cmd_nextdock = plugin_register("custom_action", &g_a_nextdock);
    plugin_register("hookcommand2", (void*)hookcommand2);
    plugin_register("toggleaction", (void*)toggleaction);
    plugin_register("timer", (void*)timer);
    return 1;
}

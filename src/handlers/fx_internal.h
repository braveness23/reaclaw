#pragma once

// FX param-reference/named-config helpers shared across handlers that touch
// TrackFX_*/TakeFX_* by name rather than slot index (handlers/fx.cpp) and
// handlers that drive a specific plugin's own JSFX config state
// (handlers/reastream.cpp, for ReaStream's IP/port/mode params). Header-only
// templates so each handler TU inlines them without a link dependency, same
// shape as handlers/handler_util.h.

#include <string>

#include <json.hpp>

namespace ReaClaw::Handlers {

// Resolve an FX param reference ({"index":N} or {"name":"Threshold"}) to a
// 0-based param index, or -1 if not found. Main-thread only. Shared between
// track FX and take FX via the API family's GetNumParams/GetParamName.
template <typename H, typename FnNum, typename FnName>
int resolve_fx_param_idx(H* h, int fx, const nlohmann::json& p, FnNum fn_num, FnName fn_name) {
    if (p.contains("index") && p["index"].is_number_integer())
        return p["index"].get<int>();
    if (p.contains("name") && p["name"].is_string()) {
        std::string want = p["name"].get<std::string>();
        int n = fn_num(h, fx);
        for (int i = 0; i < n; i++) {
            char nm[256] = {};
            fn_name(h, fx, i, nm, sizeof(nm));
            if (want == nm)
                return i;
        }
    }
    return -1;
}

// Fetch a plugin/param named-config value as a string ("" if unsupported).
// TrackFX_GetNamedConfigParm / TakeFX_GetNamedConfigParm share this shape.
template <typename H, typename FnGet>
std::string get_named_config(H* h, int fx, const std::string& name, FnGet fn_get) {
    char buf[128] = {};
    if (fn_get(h, fx, name.c_str(), buf, sizeof(buf)))
        return std::string(buf);
    return std::string();
}

}  // namespace ReaClaw::Handlers

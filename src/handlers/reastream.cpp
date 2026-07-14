#include "handlers/reastream.h"

#include "handlers/common.h"
#include "handlers/fx_internal.h"
#include "handlers/handler_util.h"
#include "handlers/state.h"
#include "reaper/executor.h"
#include "util/logging.h"

#include <httplib.h>

#include <algorithm>
#include <string>
#include <vector>

#include <reaper_plugin_functions.h>

#include <json.hpp>

namespace ReaClaw::Handlers {

namespace {

// See the IMPORTANT note in handlers/reastream.h: this mapping is an
// unverified best-effort guess, not a confirmed ReaStream param layout. Each
// candidate name is tried in order via resolve_fx_param_idx; the first match
// wins. If none match, the field is reported in the response's `unresolved`
// array instead of silently doing nothing.
const std::vector<std::string>& candidate_names(const std::string& field) {
    static const std::vector<std::string> ip = {"IP", "IP Address", "Remote IP", "ip"};
    static const std::vector<std::string> port = {"Port", "Remote Port", "port"};
    static const std::vector<std::string> mode = {"Mode", "mode"};
    static const std::vector<std::string> channel = {"Channel", "channel", "Chan"};
    static const std::vector<std::string> ident = {"Ident", "Identifier", "ident"};
    static const std::vector<std::string> empty;
    if (field == "ip")
        return ip;
    if (field == "port")
        return port;
    if (field == "mode")
        return mode;
    if (field == "channel")
        return channel;
    if (field == "ident")
        return ident;
    return empty;
}

int resolve_candidate(MediaTrack* track, int fx, const std::string& field) {
    for (const auto& name : candidate_names(field)) {
        nlohmann::json ref = {{"name", name}};
        int idx = resolve_fx_param_idx(track, fx, ref, TrackFX_GetNumParams, TrackFX_GetParamName);
        if (idx >= 0)
            return idx;
    }
    return -1;
}

}  // namespace

void handle_reastream_configure(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    if (!path_int(req, res, "index", index))
        return;
    nlohmann::json body;
    if (!parse_body(req, res, body))
        return;

    auto result = Executor::post([index, body]() -> nlohmann::json {
        return with_undo("ReaClaw: configure ReaStream", [&]() -> nlohmann::json {
            if (index < 0 || index >= CountTracks(nullptr))
                return {{"_not_found", true}};
            MediaTrack* t = GetTrack(nullptr, index);
            if (!t)
                return {{"_not_found", true}};

            // instantiate=true: find the existing instance, or add one if
            // this track doesn't have it yet.
            int slot = TrackFX_GetByName(t, "ReaStream", true);
            if (slot < 0)
                return {{"_bad_request", true},
                        {"_message",
                         "ReaStream not found — it ships with ReaPlugs; install ReaPlugs, or "
                         "add a differently-named streaming plugin via POST "
                         ".../fx instead"}};

            nlohmann::json unresolved = nlohmann::json::array();
            auto try_set = [&](const char* field, const nlohmann::json& value) {
                if (!value.is_number())
                    return;
                int idx = resolve_candidate(t, slot, field);
                if (idx < 0) {
                    unresolved.push_back(field);
                    return;
                }
                double v = std::max(0.0, std::min(1.0, value.get<double>()));
                TrackFX_SetParamNormalized(t, slot, idx, v);
            };

            // Normalized (0..1) slider writes — not real units. Read `params`
            // back (below) for each slider's actual name/range and convert;
            // see the unverified-mapping note in handlers/reastream.h.
            if (body.contains("ip"))
                try_set("ip", body["ip"]);
            if (body.contains("port"))
                try_set("port", body["port"]);
            if (body.contains("mode"))
                try_set("mode", body["mode"]);
            if (body.contains("channel"))
                try_set("channel", body["channel"]);
            if (body.contains("ident"))
                try_set("ident", body["ident"]);

            if (body.contains("enabled") && body["enabled"].is_boolean())
                TrackFX_SetEnabled(t, slot, body["enabled"].get<bool>());

            int n = TrackFX_GetNumParams(t, slot);
            nlohmann::json params = nlohmann::json::array();
            for (int i = 0; i < n; i++) {
                char nm[256] = {};
                TrackFX_GetParamName(t, slot, i, nm, sizeof(nm));
                double norm = TrackFX_GetParamNormalized(t, slot, i);
                char fmt[256] = {};
                TrackFX_FormatParamValue(t, slot, i, norm, fmt, sizeof(fmt));
                params.push_back({{"index", i}, {"name", nm}, {"value", norm}, {"formatted", fmt}});
            }

            return {{"track", index},
                    {"slot", slot},
                    {"enabled", TrackFX_GetEnabled(t, slot)},
                    {"params", params},
                    {"unresolved", unresolved},
                    {"note",
                     "ReaStream's slider layout is not verified against a live instance — "
                     "inspect 'params' for the real names/ranges and adjust via POST "
                     ".../fx/{slot} if 'unresolved' is non-empty"}};
        });
    });
    if (executor_error(res, result))
        return;
    invalidate_state_cache();
    Log::info("Track " + std::to_string(index) + " configure ReaStream");
    json_ok(res, result);
}

}  // namespace ReaClaw::Handlers

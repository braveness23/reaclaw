#include "handlers/fx.h"

#include "handlers/common.h"
#include "handlers/fx_internal.h"
#include "handlers/handler_util.h"
#include "handlers/hints.h"
#include "handlers/learning.h"
#include "handlers/state.h"
#include "reaper/executor.h"
#include "util/logging.h"

#include <httplib.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include <reaper_plugin_functions.h>

#include <json.hpp>

namespace ReaClaw::Handlers {

namespace {

int resolve_fx_param(MediaTrack* track, int fx, const nlohmann::json& p) {
    return resolve_fx_param_idx(track, fx, p, TrackFX_GetNumParams, TrackFX_GetParamName);
}

int to_int_or(const std::string& s, int def) {
    if (s.empty())
        return def;
    try {
        return std::stoi(s);
    } catch (...) {
        return def;
    }
}

double to_double_or(const std::string& s, double def) {
    if (s.empty())
        return def;
    try {
        return std::stod(s);
    } catch (...) {
        return def;
    }
}

// Issue #100: report whether a param is already wired to an LFO, audio-rate
// (ACS) modulation, or a parameter-link/MIDI-CC binding — so an agent doesn't
// set a value via the API only to have it silently overridden. Detail on the
// MIDI-CC binding (param.X.plink.*) is only included when a link is active,
// since it's meaningless otherwise.
template <typename H, typename FnGet>
nlohmann::json fx_param_modulation_json(H* h, int fx, int param_idx, FnGet fn_get) {
    auto get = [&](const std::string& suffix) {
        return get_named_config(h, fx, "param." + std::to_string(param_idx) + "." + suffix, fn_get);
    };
    bool lfo_active = get("lfo.active") == "1";
    bool acs_active = get("acs.active") == "1";
    bool plink_active = get("plink.active") == "1";
    nlohmann::json mod = {
            {"lfo_active", lfo_active}, {"acs_active", acs_active}, {"plink_active", plink_active}};
    if (plink_active) {
        // effect=-100 is REAPER's sentinel for "MIDI CC link" (vs. linking to
        // another FX's parameter) — see param.X.plink.* in the SDK header.
        mod["plink"] = {{"effect", to_int_or(get("plink.effect"), -1)},
                        {"param", to_int_or(get("plink.param"), -1)},
                        {"midi_bus", to_int_or(get("plink.midi_bus"), 0)},
                        {"midi_chan", to_int_or(get("plink.midi_chan"), -1)},
                        {"midi_msg", to_int_or(get("plink.midi_msg"), 0)},
                        {"midi_msg2", to_int_or(get("plink.midi_msg2"), 0)},
                        {"scale", to_double_or(get("plink.scale"), 1.0)},
                        {"offset", to_double_or(get("plink.offset"), 0.0)}};
    }
    return mod;
}

// Write side of #100: apply any provided param.X.plink.* fields via
// *FX_SetNamedConfigParm. Passing {"active": false} clears the link.
template <typename H, typename FnSet>
void apply_plink(H* h, int fx, int param_idx, const nlohmann::json& plink, FnSet fn_set) {
    static const char* keys[] = {"active",
                                 "effect",
                                 "param",
                                 "midi_bus",
                                 "midi_chan",
                                 "midi_msg",
                                 "midi_msg2",
                                 "scale",
                                 "offset"};
    for (const char* key : keys) {
        if (!plink.contains(key))
            continue;
        const auto& v = plink[key];
        std::string value;
        if (v.is_boolean())
            value = v.get<bool>() ? "1" : "0";
        else if (v.is_number_integer())
            value = std::to_string(v.get<long long>());
        else if (v.is_number())
            value = std::to_string(v.get<double>());
        else if (v.is_string())
            value = v.get<std::string>();
        else
            continue;
        std::string name = "param." + std::to_string(param_idx) + ".plink." + key;
        fn_set(h, fx, name.c_str(), value.c_str());
    }
}

// JSON snapshot of one FX slot's parameters: index, name, normalized value,
// formatted display string, the real-unit range (min/max/mid + current raw
// value from *FX_GetParamEx) so an agent can reason in real units instead of
// guessing what a normalized 0..1 maps to, and (#100) whether the param is
// already under LFO/ACS/MIDI-link modulation.
//
// Issue #74: large plugins (e.g. Surge XT: 2147 params) need pagination and
// name search rather than dumping every param on every GET. limit<0 (the
// default, used by the mutating POST /fx responses) means unlimited — only
// the GET routes pass real limit/offset/q values.
//
// Shared between track FX and take FX by passing the API family's function
// pointers (identical shapes modulo the handle type).
template <typename H,
          typename FnNum,
          typename FnName,
          typename FnNorm,
          typename FnFmt,
          typename FnEx,
          typename FnGetCfg>
nlohmann::json fx_params_json_impl(H* h,
                                   int fx,
                                   int limit,
                                   int offset,
                                   const std::string& q,
                                   FnNum fn_num,
                                   FnName fn_name,
                                   FnNorm fn_norm,
                                   FnFmt fn_fmt,
                                   FnEx fn_ex,
                                   FnGetCfg fn_get_cfg) {
    nlohmann::json arr = nlohmann::json::array();
    int n = fn_num(h, fx);
    int matched = 0;
    for (int i = 0; i < n; i++) {
        char nm[256] = {};
        fn_name(h, fx, i, nm, sizeof(nm));
        if (!q.empty() && !ci_contains(nm, q))
            continue;
        if (matched++ < offset)
            continue;
        if (limit >= 0 && static_cast<int>(arr.size()) >= limit)
            continue;
        double norm = fn_norm(h, fx, i);
        char fmt[256] = {};
        fn_fmt(h, fx, i, norm, fmt, sizeof(fmt));
        double pmin = 0.0, pmax = 1.0, pmid = 0.5;
        double raw = fn_ex(h, fx, i, &pmin, &pmax, &pmid);
        arr.push_back({{"index", i},
                       {"name", nm},
                       {"value", norm},
                       {"formatted", fmt},
                       {"raw", raw},
                       {"min", pmin},
                       {"max", pmax},
                       {"mid", pmid},
                       {"modulation", fx_param_modulation_json(h, fx, i, fn_get_cfg)}});
    }
    return arr;
}

nlohmann::json fx_params_json(MediaTrack* track,
                              int fx,
                              int limit = -1,
                              int offset = 0,
                              const std::string& q = "") {
    return fx_params_json_impl(track,
                               fx,
                               limit,
                               offset,
                               q,
                               TrackFX_GetNumParams,
                               TrackFX_GetParamName,
                               TrackFX_GetParamNormalized,
                               TrackFX_FormatParamValue,
                               TrackFX_GetParamEx,
                               TrackFX_GetNamedConfigParm);
}

nlohmann::json take_fx_params_json(MediaItem_Take* take,
                                   int fx,
                                   int limit = -1,
                                   int offset = 0,
                                   const std::string& q = "") {
    return fx_params_json_impl(take,
                               fx,
                               limit,
                               offset,
                               q,
                               TakeFX_GetNumParams,
                               TakeFX_GetParamName,
                               TakeFX_GetParamNormalized,
                               TakeFX_FormatParamValue,
                               TakeFX_GetParamEx,
                               TakeFX_GetNamedConfigParm);
}

// Apply an array of {index|name, value?, plink?} param writes. `value`
// (normalized 0..1) and `plink` (issue #100 — MIDI-CC/param-link binding) are
// independent — either, both, or neither may be present per entry.
void apply_fx_params(MediaTrack* track, int fx, const nlohmann::json& params) {
    if (!params.is_array())
        return;
    for (const auto& p : params) {
        if (!p.is_object())
            continue;
        int idx = resolve_fx_param(track, fx, p);
        if (idx < 0)
            continue;
        if (p.contains("value") && p["value"].is_number()) {
            double v = std::max(0.0, std::min(1.0, p["value"].get<double>()));
            TrackFX_SetParamNormalized(track, fx, idx, v);
        }
        if (p.contains("plink") && p["plink"].is_object())
            apply_plink(track, fx, idx, p["plink"], TrackFX_SetNamedConfigParm);
    }
}

// Apply normalized params (+ plink) to a take FX slot — mirrors apply_fx_params().
void apply_take_fx_params(MediaItem_Take* take, int slot, const nlohmann::json& params) {
    if (!params.is_array())
        return;
    for (const auto& p : params) {
        if (!p.is_object())
            continue;
        int idx = resolve_fx_param_idx(take, slot, p, TakeFX_GetNumParams, TakeFX_GetParamName);
        if (idx < 0)
            continue;
        if (p.contains("value") && p["value"].is_number()) {
            double v = std::max(0.0, std::min(1.0, p["value"].get<double>()));
            TakeFX_SetParamNormalized(take, slot, idx, v);
        }
        if (p.contains("plink") && p["plink"].is_object())
            apply_plink(take, slot, idx, p["plink"], TakeFX_SetNamedConfigParm);
    }
}

// Parse the ?limit=&offset=&q= param-pagination query trio (issue #74).
void parse_param_page(const httplib::Request& req, int& limit, int& offset, std::string& q) {
    limit = -1;
    if (req.has_param("limit"))
        try {
            limit = std::max(0, std::stoi(req.get_param_value("limit")));
        } catch (...) {
        }
    offset = 0;
    if (req.has_param("offset"))
        try {
            offset = std::max(0, std::stoi(req.get_param_value("offset")));
        } catch (...) {
        }
    q = req.has_param("q") ? req.get_param_value("q") : "";
}

// Issue #102: stable identity for one FX instance, independent of chain slot.
template <typename H, typename FnGuid>
std::string fx_guid_str(H* h, int fx, FnGuid fn_guid) {
    if (auto* g = fn_guid(h, fx))
        return guid_to_string(g);
    return "";
}

// Issue #101: a plugin's I/O pin count and how each pin's channels map onto
// the track/take's own channels. TrackFX_GetPinMappings/TakeFX_GetPinMappings
// return a 64-bit-per-pin channel bitmask split across a low32 return value
// and a high32Out param.
template <typename H, typename FnIO, typename FnPin>
nlohmann::json fx_pins_json(H* h, int fx, FnIO fn_io, FnPin fn_pin) {
    int n_in = 0, n_out = 0;
    int rv = fn_io(h, fx, &n_in, &n_out);
    auto pin_list = [&](bool isoutput, int n) {
        nlohmann::json arr = nlohmann::json::array();
        for (int p = 0; p < n; p++) {
            int high32 = 0;
            int low32 = fn_pin(h, fx, isoutput ? 1 : 0, p, &high32);
            uint64_t mask = (static_cast<uint64_t>(static_cast<uint32_t>(high32)) << 32) |
                            static_cast<uint32_t>(low32);
            nlohmann::json channels = nlohmann::json::array();
            for (int b = 0; b < 64; b++)
                if (mask & (1ULL << b))
                    channels.push_back(b);
            arr.push_back({{"pin", p}, {"channels", channels}});
        }
        return arr;
    };
    return {{"supported", rv != -1},
            {"input_pins", n_in},
            {"output_pins", n_out},
            {"inputs", pin_list(false, n_in)},
            {"outputs", pin_list(true, n_out)}};
}

// Write side of #101: body {"pins": [{"pin":N, "output":bool, "channels":[...]}]}.
// Each entry replaces that pin's full channel mapping (not additive).
template <typename H, typename FnSet>
void apply_fx_pins(H* h, int fx, const nlohmann::json& pins, FnSet fn_set) {
    if (!pins.is_array())
        return;
    for (const auto& entry : pins) {
        if (!entry.is_object() || !entry.contains("pin") || !entry["pin"].is_number_integer())
            continue;
        int pin = entry["pin"].get<int>();
        bool isoutput = entry.value("output", false);
        uint64_t mask = 0;
        if (entry.contains("channels") && entry["channels"].is_array()) {
            for (const auto& ch : entry["channels"]) {
                if (ch.is_number_integer()) {
                    int c = ch.get<int>();
                    if (c >= 0 && c < 64)
                        mask |= (1ULL << c);
                }
            }
        }
        int low32 = static_cast<int>(static_cast<uint32_t>(mask & 0xFFFFFFFFULL));
        int hi32 = static_cast<int>(static_cast<uint32_t>(mask >> 32));
        fn_set(h, fx, isoutput ? 1 : 0, pin, low32, hi32);
    }
}

}  // namespace

// POST /state/tracks/{index}/fx — add an FX by name.
// Body: { "name": "ReaComp", "enabled": true, "params": [ {index|name, value} ] }
void handle_state_add_fx(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    if (!path_int(req, res, "index", index))
        return;
    nlohmann::json body;
    if (!parse_body(req, res, body))
        return;
    if (!body.contains("name") || !body["name"].is_string()) {
        json_error(res, 400, "Missing required field: name", "BAD_REQUEST");
        return;
    }
    std::string fx_name = body["name"].get<std::string>();
    auto result = Executor::post([index, fx_name, body]() -> nlohmann::json {
        return with_undo("ReaClaw: add FX", [&]() -> nlohmann::json {
            if (index < 0 || index >= CountTracks(nullptr))
                return {{"_not_found", true}};
            MediaTrack* t = GetTrack(nullptr, index);
            if (!t)
                return {{"_not_found", true}};
            int slot = TrackFX_AddByName(t, fx_name.c_str(), false, -1);
            if (slot < 0)
                return {{"_bad_request", true}, {"_message", "FX not found: " + fx_name}};
            if (body.contains("enabled") && body["enabled"].is_boolean())
                TrackFX_SetEnabled(t, slot, body["enabled"].get<bool>());
            if (body.contains("offline") && body["offline"].is_boolean())
                TrackFX_SetOffline(t, slot, body["offline"].get<bool>());
            if (body.contains("params"))
                apply_fx_params(t, slot, body["params"]);
            char resolved[256] = {};
            TrackFX_GetFXName(t, slot, resolved, sizeof(resolved));
            return {{"track", index},
                    {"slot", slot},
                    {"guid", fx_guid_str(t, slot, TrackFX_GetFXGUID)},
                    {"name", resolved},
                    {"enabled", TrackFX_GetEnabled(t, slot)},
                    {"offline", TrackFX_GetOffline(t, slot)},
                    {"hints", Hints::for_fx(t, index, slot)}};
        });
    });
    if (executor_error(res, result))
        return;
    invalidate_state_cache();
    if (Learning::enabled())
        Learning::note(agent_id(req), "fx.add");
    Log::info("Track " + std::to_string(index) + " add FX: " + fx_name);
    json_ok(res, result);
}

// GET /state/tracks/{index}/fx/{slot}[?limit=&offset=&q=] — list an FX
// slot's parameters. {slot} accepts a numeric chain index or a GUID string
// (issue #102). Issue #74: large plugins (Surge XT: 2147 params) need
// pagination/search rather than dumping everything on every call.
void handle_state_get_fx(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    if (!path_int(req, res, "index", index))
        return;
    std::string slot_token = req.path_params.at("slot");
    int limit = -1, offset = 0;
    std::string q;
    parse_param_page(req, limit, offset, q);

    auto result = Executor::post([index, slot_token, limit, offset, q]() -> nlohmann::json {
        if (index < 0 || index >= CountTracks(nullptr))
            return {{"_not_found", true}};
        MediaTrack* t = GetTrack(nullptr, index);
        if (!t)
            return {{"_not_found", true}};
        int slot = resolve_fx_slot(t, slot_token, TrackFX_GetCount, TrackFX_GetFXGUID);
        if (slot < 0)
            return {{"_bad_request", true}, {"_message", "FX slot out of range or GUID not found"}};
        char nm[256] = {};
        TrackFX_GetFXName(t, slot, nm, sizeof(nm));
        return {{"track", index},
                {"slot", slot},
                {"guid", fx_guid_str(t, slot, TrackFX_GetFXGUID)},
                {"name", nm},
                {"enabled", TrackFX_GetEnabled(t, slot)},
                {"offline", TrackFX_GetOffline(t, slot)},
                {"param_count", TrackFX_GetNumParams(t, slot)},
                {"params", fx_params_json(t, slot, limit, offset, q)}};
    });
    if (executor_error(res, result))
        return;
    json_ok(res, result);
}

// POST /state/tracks/{index}/fx/{slot} — set FX enabled state and/or params.
// {slot} accepts a numeric chain index or a GUID string (issue #102).
void handle_state_set_fx(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    if (!path_int(req, res, "index", index))
        return;
    std::string slot_token = req.path_params.at("slot");
    nlohmann::json body;
    if (!parse_body(req, res, body))
        return;
    auto result = Executor::post([index, slot_token, body]() -> nlohmann::json {
        return with_undo("ReaClaw: set FX", [&]() -> nlohmann::json {
            if (index < 0 || index >= CountTracks(nullptr))
                return {{"_not_found", true}};
            MediaTrack* t = GetTrack(nullptr, index);
            if (!t)
                return {{"_not_found", true}};
            int slot = resolve_fx_slot(t, slot_token, TrackFX_GetCount, TrackFX_GetFXGUID);
            if (slot < 0)
                return {{"_bad_request", true},
                        {"_message", "FX slot out of range or GUID not found"}};
            if (body.contains("enabled") && body["enabled"].is_boolean())
                TrackFX_SetEnabled(t, slot, body["enabled"].get<bool>());
            if (body.contains("offline") && body["offline"].is_boolean())
                TrackFX_SetOffline(t, slot, body["offline"].get<bool>());
            if (body.contains("params"))
                apply_fx_params(t, slot, body["params"]);
            char nm[256] = {};
            TrackFX_GetFXName(t, slot, nm, sizeof(nm));
            return {{"track", index},
                    {"slot", slot},
                    {"guid", fx_guid_str(t, slot, TrackFX_GetFXGUID)},
                    {"name", nm},
                    {"enabled", TrackFX_GetEnabled(t, slot)},
                    {"offline", TrackFX_GetOffline(t, slot)},
                    {"params", fx_params_json(t, slot)}};
        });
    });
    if (executor_error(res, result))
        return;
    invalidate_state_cache();
    json_ok(res, result);
}

// GET /state/tracks/{index}/fx/{slot}/pins — plugin I/O pin count + channel
// routing (issue #101). {slot} accepts a numeric chain index or GUID (#102).
void handle_fx_get_pins(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    if (!path_int(req, res, "index", index))
        return;
    std::string slot_token = req.path_params.at("slot");
    auto result = Executor::post([index, slot_token]() -> nlohmann::json {
        if (index < 0 || index >= CountTracks(nullptr))
            return {{"_not_found", true}};
        MediaTrack* t = GetTrack(nullptr, index);
        if (!t)
            return {{"_not_found", true}};
        int slot = resolve_fx_slot(t, slot_token, TrackFX_GetCount, TrackFX_GetFXGUID);
        if (slot < 0)
            return {{"_bad_request", true}, {"_message", "FX slot out of range or GUID not found"}};
        nlohmann::json j = fx_pins_json(t, slot, TrackFX_GetIOSize, TrackFX_GetPinMappings);
        j["track"] = index;
        j["slot"] = slot;
        j["guid"] = fx_guid_str(t, slot, TrackFX_GetFXGUID);
        return j;
    });
    if (executor_error(res, result))
        return;
    json_ok(res, result);
}

// POST /state/tracks/{index}/fx/{slot}/pins — set channel mapping for one or
// more pins. Body: { "pins": [ { "pin": 0, "output": false, "channels": [0,1] } ] }
void handle_fx_set_pins(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    if (!path_int(req, res, "index", index))
        return;
    std::string slot_token = req.path_params.at("slot");
    nlohmann::json body;
    if (!parse_body(req, res, body))
        return;
    auto result = Executor::post([index, slot_token, body]() -> nlohmann::json {
        return with_undo("ReaClaw: set FX pin mapping", [&]() -> nlohmann::json {
            if (index < 0 || index >= CountTracks(nullptr))
                return {{"_not_found", true}};
            MediaTrack* t = GetTrack(nullptr, index);
            if (!t)
                return {{"_not_found", true}};
            int slot = resolve_fx_slot(t, slot_token, TrackFX_GetCount, TrackFX_GetFXGUID);
            if (slot < 0)
                return {{"_bad_request", true},
                        {"_message", "FX slot out of range or GUID not found"}};
            apply_fx_pins(
                    t, slot, body.value("pins", nlohmann::json::array()), TrackFX_SetPinMappings);
            nlohmann::json j = fx_pins_json(t, slot, TrackFX_GetIOSize, TrackFX_GetPinMappings);
            j["track"] = index;
            j["slot"] = slot;
            j["guid"] = fx_guid_str(t, slot, TrackFX_GetFXGUID);
            return j;
        });
    });
    if (executor_error(res, result))
        return;
    invalidate_state_cache();
    json_ok(res, result);
}

// POST /state/tracks/{index}/fx/{slot}/copy — copy (or move) this FX to another
// track. Body: { "to_track": j, "to_slot": -1, "move": false }. to_slot -1
// appends to the destination chain. {slot} accepts a numeric chain index or a
// GUID string (issue #102).
void handle_fx_copy(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    if (!path_int(req, res, "index", index))
        return;
    std::string slot_token = req.path_params.at("slot");
    nlohmann::json body;
    if (!parse_body(req, res, body))
        return;
    if (!body.contains("to_track") || !body["to_track"].is_number_integer()) {
        json_error(res, 400, "Missing required field: to_track", "BAD_REQUEST");
        return;
    }
    int to_track = body["to_track"].get<int>();
    int to_slot = body.value("to_slot", -1);
    bool move = body.value("move", false);
    auto result = Executor::post([index, slot_token, to_track, to_slot, move]() -> nlohmann::json {
        return with_undo("ReaClaw: copy FX", [&]() -> nlohmann::json {
            int n = CountTracks(nullptr);
            if (index < 0 || index >= n || to_track < 0 || to_track >= n)
                return {{"_not_found", true}};
            MediaTrack* src = GetTrack(nullptr, index);
            MediaTrack* dst = GetTrack(nullptr, to_track);
            if (!src || !dst)
                return {{"_not_found", true}};
            int slot = resolve_fx_slot(src, slot_token, TrackFX_GetCount, TrackFX_GetFXGUID);
            if (slot < 0)
                return {{"_bad_request", true},
                        {"_message", "FX slot out of range or GUID not found"}};
            std::string guid = fx_guid_str(src, slot, TrackFX_GetFXGUID);
            TrackFX_CopyToTrack(src, slot, dst, to_slot, move);
            return {{"from_track", index},
                    {"from_slot", slot},
                    {"from_guid", guid},
                    {"to_track", to_track},
                    {"moved", move},
                    {"dest_fx_count", TrackFX_GetCount(dst)}};
        });
    });
    if (executor_error(res, result))
        return;
    invalidate_state_cache();
    Log::info("FX copy: track " + std::to_string(index) + " slot " + slot_token + " -> track " +
              std::to_string(to_track));
    json_ok(res, result);
}

// DELETE /state/tracks/{index}/fx/{slot} — {slot} accepts a numeric chain
// index or a GUID string (issue #102).
void handle_state_delete_fx(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    if (!path_int(req, res, "index", index))
        return;
    std::string slot_token = req.path_params.at("slot");
    auto result = Executor::post([index, slot_token]() -> nlohmann::json {
        return with_undo("ReaClaw: delete FX", [&]() -> nlohmann::json {
            if (index < 0 || index >= CountTracks(nullptr))
                return {{"_not_found", true}};
            MediaTrack* t = GetTrack(nullptr, index);
            if (!t)
                return {{"_not_found", true}};
            int slot = resolve_fx_slot(t, slot_token, TrackFX_GetCount, TrackFX_GetFXGUID);
            if (slot < 0)
                return {{"_bad_request", true},
                        {"_message", "FX slot out of range or GUID not found"}};
            std::string guid = fx_guid_str(t, slot, TrackFX_GetFXGUID);
            bool ok = TrackFX_Delete(t, slot);
            return {{"deleted", ok}, {"track", index}, {"slot", slot}, {"guid", guid}};
        });
    });
    if (executor_error(res, result))
        return;
    invalidate_state_cache();
    json_ok(res, result);
}

// GET /state/tracks/{index}/fx/{slot}/preset — current preset + count.
// {slot} accepts a numeric chain index or a GUID string (issue #102).
void handle_fx_get_preset(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    if (!path_int(req, res, "index", index))
        return;
    std::string slot_token = req.path_params.at("slot");
    auto result = Executor::post([index, slot_token]() -> nlohmann::json {
        if (index < 0 || index >= CountTracks(nullptr))
            return {{"_not_found", true}};
        MediaTrack* t = GetTrack(nullptr, index);
        if (!t)
            return {{"_not_found", true}};
        int slot = resolve_fx_slot(t, slot_token, TrackFX_GetCount, TrackFX_GetFXGUID);
        if (slot < 0)
            return {{"_bad_request", true}, {"_message", "FX slot out of range or GUID not found"}};
        char pname[512] = {};
        TrackFX_GetPreset(t, slot, pname, sizeof(pname));
        int total = 0;
        int cur = TrackFX_GetPresetIndex(t, slot, &total);
        return {{"track", index},
                {"slot", slot},
                {"guid", fx_guid_str(t, slot, TrackFX_GetFXGUID)},
                {"preset", pname},
                {"preset_index", cur},
                {"preset_count", total}};
    });
    if (executor_error(res, result))
        return;
    json_ok(res, result);
}

// POST /state/tracks/{index}/fx/{slot}/preset — load a preset.
// Body: { "name": "Bus Comp" }  OR  { "navigate": -1 | 1 }  (prev/next preset).
// {slot} accepts a numeric chain index or a GUID string (issue #102).
void handle_fx_set_preset(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    if (!path_int(req, res, "index", index))
        return;
    std::string slot_token = req.path_params.at("slot");
    nlohmann::json body;
    if (!parse_body(req, res, body))
        return;
    bool has_name = body.contains("name") && body["name"].is_string();
    bool has_nav = body.contains("navigate") && body["navigate"].is_number_integer();
    if (!has_name && !has_nav) {
        json_error(res, 400, "Provide 'name' or 'navigate'", "BAD_REQUEST");
        return;
    }
    auto result = Executor::post([index, slot_token, body, has_name, has_nav]() -> nlohmann::json {
        return with_undo("ReaClaw: load FX preset", [&]() -> nlohmann::json {
            if (index < 0 || index >= CountTracks(nullptr))
                return {{"_not_found", true}};
            MediaTrack* t = GetTrack(nullptr, index);
            if (!t)
                return {{"_not_found", true}};
            int slot = resolve_fx_slot(t, slot_token, TrackFX_GetCount, TrackFX_GetFXGUID);
            if (slot < 0)
                return {{"_bad_request", true},
                        {"_message", "FX slot out of range or GUID not found"}};
            bool ok = false;
            if (has_name)
                ok = TrackFX_SetPreset(t, slot, body["name"].get<std::string>().c_str());
            else
                ok = TrackFX_NavigatePresets(t, slot, body["navigate"].get<int>());
            if (!ok)
                return {{"_bad_request", true}, {"_message", "Preset not found / not changed"}};
            char pname[512] = {};
            TrackFX_GetPreset(t, slot, pname, sizeof(pname));
            int total = 0;
            int cur = TrackFX_GetPresetIndex(t, slot, &total);
            return {{"track", index},
                    {"slot", slot},
                    {"guid", fx_guid_str(t, slot, TrackFX_GetFXGUID)},
                    {"preset", pname},
                    {"preset_index", cur},
                    {"preset_count", total}};
        });
    });
    if (executor_error(res, result))
        return;
    invalidate_state_cache();
    json_ok(res, result);
}

// ---------------------------------------------------------------------------
// Issue #50 — Take-FX verbs
//
// Mirrors the track-FX surface (handle_state_*_fx / handle_fx_*) for item
// takes. Route path params: "index" = item index, "take" = take index,
// "slot" = FX slot (numeric chain index or GUID string — issue #102).
// ---------------------------------------------------------------------------

// POST /state/items/{index}/takes/{take}/fx
void handle_take_add_fx(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    if (!parse_body(req, res, body))
        return;
    if (!body.contains("name") || !body["name"].is_string()) {
        json_error(res, 400, "Missing required field: name", "BAD_REQUEST");
        return;
    }
    std::string fx_name = body["name"].get<std::string>();

    int item_idx = 0, take_idx = 0;
    if (!path_int(req, res, "index", item_idx) || !path_int(req, res, "take", take_idx))
        return;

    auto result = Executor::post([item_idx, take_idx, fx_name, body]() -> nlohmann::json {
        return with_undo("ReaClaw: add take FX", [&]() -> nlohmann::json {
            int n_items = CountMediaItems(nullptr);
            if (item_idx < 0 || item_idx >= n_items)
                return {{"_not_found", true}};
            MediaItem* it = GetMediaItem(nullptr, item_idx);
            if (!it || take_idx >= GetMediaItemNumTakes(it))
                return {{"_not_found", true}};
            MediaItem_Take* tk = GetMediaItemTake(it, take_idx);
            if (!tk)
                return {{"_not_found", true}};
            int slot = TakeFX_AddByName(tk, fx_name.c_str(), -1);
            if (slot < 0)
                return {{"_bad_request", true}, {"_message", "FX not found: " + fx_name}};
            if (body.contains("enabled") && body["enabled"].is_boolean())
                TakeFX_SetEnabled(tk, slot, body["enabled"].get<bool>());
            if (body.contains("offline") && body["offline"].is_boolean())
                TakeFX_SetOffline(tk, slot, body["offline"].get<bool>());
            if (body.contains("params"))
                apply_take_fx_params(tk, slot, body["params"]);
            char resolved[256] = {};
            TakeFX_GetFXName(tk, slot, resolved, sizeof(resolved));
            return {{"item", item_idx},
                    {"take", take_idx},
                    {"slot", slot},
                    {"guid", fx_guid_str(tk, slot, TakeFX_GetFXGUID)},
                    {"name", resolved},
                    {"enabled", TakeFX_GetEnabled(tk, slot)},
                    {"offline", TakeFX_GetOffline(tk, slot)}};
        });
    });
    if (executor_error(res, result))
        return;
    Log::info("Item " + std::to_string(item_idx) + " take " + std::to_string(take_idx) +
              " add FX: " + fx_name);
    json_ok(res, result);
}

// GET /state/items/{index}/takes/{take}/fx/{slot}[?limit=&offset=&q=]
// Param pagination/search mirrors the track-FX GET (issue #74).
void handle_take_get_fx(const httplib::Request& req, httplib::Response& res) {
    int item_idx = 0, take_idx = 0;
    if (!path_int(req, res, "index", item_idx) || !path_int(req, res, "take", take_idx))
        return;
    std::string slot_token = req.path_params.at("slot");
    int limit = -1, offset = 0;
    std::string q;
    parse_param_page(req, limit, offset, q);
    auto result = Executor::post(
            [item_idx, take_idx, slot_token, limit, offset, q]() -> nlohmann::json {
                int n_items = CountMediaItems(nullptr);
                if (item_idx < 0 || item_idx >= n_items)
                    return {{"_not_found", true}};
                MediaItem* it = GetMediaItem(nullptr, item_idx);
                if (!it || take_idx >= GetMediaItemNumTakes(it))
                    return {{"_not_found", true}};
                MediaItem_Take* tk = GetMediaItemTake(it, take_idx);
                if (!tk)
                    return {{"_not_found", true}};
                int slot = resolve_fx_slot(tk, slot_token, TakeFX_GetCount, TakeFX_GetFXGUID);
                if (slot < 0)
                    return {{"_bad_request", true},
                            {"_message", "FX slot out of range or GUID not found"}};
                char nm[256] = {};
                TakeFX_GetFXName(tk, slot, nm, sizeof(nm));
                return {{"item", item_idx},
                        {"take", take_idx},
                        {"slot", slot},
                        {"guid", fx_guid_str(tk, slot, TakeFX_GetFXGUID)},
                        {"name", nm},
                        {"enabled", TakeFX_GetEnabled(tk, slot)},
                        {"offline", TakeFX_GetOffline(tk, slot)},
                        {"param_count", TakeFX_GetNumParams(tk, slot)},
                        {"params", take_fx_params_json(tk, slot, limit, offset, q)}};
            });
    if (executor_error(res, result))
        return;
    json_ok(res, result);
}

// POST /state/items/{index}/takes/{take}/fx/{slot}
void handle_take_set_fx(const httplib::Request& req, httplib::Response& res) {
    int item_idx = 0, take_idx = 0;
    if (!path_int(req, res, "index", item_idx) || !path_int(req, res, "take", take_idx))
        return;
    std::string slot_token = req.path_params.at("slot");
    nlohmann::json body;
    if (!parse_body(req, res, body))
        return;
    auto result = Executor::post([item_idx, take_idx, slot_token, body]() -> nlohmann::json {
        return with_undo("ReaClaw: set take FX", [&]() -> nlohmann::json {
            int n_items = CountMediaItems(nullptr);
            if (item_idx < 0 || item_idx >= n_items)
                return {{"_not_found", true}};
            MediaItem* it = GetMediaItem(nullptr, item_idx);
            if (!it || take_idx >= GetMediaItemNumTakes(it))
                return {{"_not_found", true}};
            MediaItem_Take* tk = GetMediaItemTake(it, take_idx);
            if (!tk)
                return {{"_not_found", true}};
            int slot = resolve_fx_slot(tk, slot_token, TakeFX_GetCount, TakeFX_GetFXGUID);
            if (slot < 0)
                return {{"_bad_request", true},
                        {"_message", "FX slot out of range or GUID not found"}};
            if (body.contains("enabled") && body["enabled"].is_boolean())
                TakeFX_SetEnabled(tk, slot, body["enabled"].get<bool>());
            if (body.contains("offline") && body["offline"].is_boolean())
                TakeFX_SetOffline(tk, slot, body["offline"].get<bool>());
            if (body.contains("params"))
                apply_take_fx_params(tk, slot, body["params"]);
            char nm[256] = {};
            TakeFX_GetFXName(tk, slot, nm, sizeof(nm));
            return {{"item", item_idx},
                    {"take", take_idx},
                    {"slot", slot},
                    {"guid", fx_guid_str(tk, slot, TakeFX_GetFXGUID)},
                    {"name", nm},
                    {"enabled", TakeFX_GetEnabled(tk, slot)},
                    {"offline", TakeFX_GetOffline(tk, slot)},
                    {"params", take_fx_params_json(tk, slot)}};
        });
    });
    if (executor_error(res, result))
        return;
    json_ok(res, result);
}

// GET /state/items/{index}/takes/{take}/fx/{slot}/pins — issue #101.
void handle_take_get_pins(const httplib::Request& req, httplib::Response& res) {
    int item_idx = 0, take_idx = 0;
    if (!path_int(req, res, "index", item_idx) || !path_int(req, res, "take", take_idx))
        return;
    std::string slot_token = req.path_params.at("slot");
    auto result = Executor::post([item_idx, take_idx, slot_token]() -> nlohmann::json {
        int n_items = CountMediaItems(nullptr);
        if (item_idx < 0 || item_idx >= n_items)
            return {{"_not_found", true}};
        MediaItem* it = GetMediaItem(nullptr, item_idx);
        if (!it || take_idx >= GetMediaItemNumTakes(it))
            return {{"_not_found", true}};
        MediaItem_Take* tk = GetMediaItemTake(it, take_idx);
        if (!tk)
            return {{"_not_found", true}};
        int slot = resolve_fx_slot(tk, slot_token, TakeFX_GetCount, TakeFX_GetFXGUID);
        if (slot < 0)
            return {{"_bad_request", true}, {"_message", "FX slot out of range or GUID not found"}};
        nlohmann::json j = fx_pins_json(tk, slot, TakeFX_GetIOSize, TakeFX_GetPinMappings);
        j["item"] = item_idx;
        j["take"] = take_idx;
        j["slot"] = slot;
        j["guid"] = fx_guid_str(tk, slot, TakeFX_GetFXGUID);
        return j;
    });
    if (executor_error(res, result))
        return;
    json_ok(res, result);
}

// POST /state/items/{index}/takes/{take}/fx/{slot}/pins — issue #101.
// Body: { "pins": [ { "pin": 0, "output": false, "channels": [0,1] } ] }
void handle_take_set_pins(const httplib::Request& req, httplib::Response& res) {
    int item_idx = 0, take_idx = 0;
    if (!path_int(req, res, "index", item_idx) || !path_int(req, res, "take", take_idx))
        return;
    std::string slot_token = req.path_params.at("slot");
    nlohmann::json body;
    if (!parse_body(req, res, body))
        return;
    auto result = Executor::post([item_idx, take_idx, slot_token, body]() -> nlohmann::json {
        return with_undo("ReaClaw: set take FX pin mapping", [&]() -> nlohmann::json {
            int n_items = CountMediaItems(nullptr);
            if (item_idx < 0 || item_idx >= n_items)
                return {{"_not_found", true}};
            MediaItem* it = GetMediaItem(nullptr, item_idx);
            if (!it || take_idx >= GetMediaItemNumTakes(it))
                return {{"_not_found", true}};
            MediaItem_Take* tk = GetMediaItemTake(it, take_idx);
            if (!tk)
                return {{"_not_found", true}};
            int slot = resolve_fx_slot(tk, slot_token, TakeFX_GetCount, TakeFX_GetFXGUID);
            if (slot < 0)
                return {{"_bad_request", true},
                        {"_message", "FX slot out of range or GUID not found"}};
            apply_fx_pins(
                    tk, slot, body.value("pins", nlohmann::json::array()), TakeFX_SetPinMappings);
            nlohmann::json j = fx_pins_json(tk, slot, TakeFX_GetIOSize, TakeFX_GetPinMappings);
            j["item"] = item_idx;
            j["take"] = take_idx;
            j["slot"] = slot;
            j["guid"] = fx_guid_str(tk, slot, TakeFX_GetFXGUID);
            return j;
        });
    });
    if (executor_error(res, result))
        return;
    json_ok(res, result);
}

// DELETE /state/items/{index}/takes/{take}/fx/{slot}
void handle_take_delete_fx(const httplib::Request& req, httplib::Response& res) {
    int item_idx = 0, take_idx = 0;
    if (!path_int(req, res, "index", item_idx) || !path_int(req, res, "take", take_idx))
        return;
    std::string slot_token = req.path_params.at("slot");
    auto result = Executor::post([item_idx, take_idx, slot_token]() -> nlohmann::json {
        return with_undo("ReaClaw: delete take FX", [&]() -> nlohmann::json {
            int n_items = CountMediaItems(nullptr);
            if (item_idx < 0 || item_idx >= n_items)
                return {{"_not_found", true}};
            MediaItem* it = GetMediaItem(nullptr, item_idx);
            if (!it || take_idx >= GetMediaItemNumTakes(it))
                return {{"_not_found", true}};
            MediaItem_Take* tk = GetMediaItemTake(it, take_idx);
            if (!tk)
                return {{"_not_found", true}};
            int slot = resolve_fx_slot(tk, slot_token, TakeFX_GetCount, TakeFX_GetFXGUID);
            if (slot < 0)
                return {{"_bad_request", true},
                        {"_message", "FX slot out of range or GUID not found"}};
            std::string guid = fx_guid_str(tk, slot, TakeFX_GetFXGUID);
            bool ok = TakeFX_Delete(tk, slot);
            return {{"deleted", ok},
                    {"item", item_idx},
                    {"take", take_idx},
                    {"slot", slot},
                    {"guid", guid}};
        });
    });
    if (executor_error(res, result))
        return;
    json_ok(res, result);
}

// POST /state/items/{index}/takes/{take}/fx/{slot}/copy
// Body: { "to_item": i, "to_take": t, "to_slot": -1, "move": false }
void handle_take_copy_fx(const httplib::Request& req, httplib::Response& res) {
    int item_idx = 0, take_idx = 0;
    if (!path_int(req, res, "index", item_idx) || !path_int(req, res, "take", take_idx))
        return;
    std::string slot_token = req.path_params.at("slot");
    nlohmann::json body;
    if (!parse_body(req, res, body))
        return;
    if (!body.contains("to_item") || !body["to_item"].is_number_integer() ||
        !body.contains("to_take") || !body["to_take"].is_number_integer()) {
        json_error(res, 400, "Missing required fields: to_item, to_take", "BAD_REQUEST");
        return;
    }
    int to_item = body["to_item"].get<int>();
    int to_take = body["to_take"].get<int>();
    int to_slot = body.value("to_slot", -1);
    bool move = body.value("move", false);
    auto result = Executor::post([item_idx, take_idx, slot_token, to_item, to_take, to_slot, move]()
                                         -> nlohmann::json {
        return with_undo("ReaClaw: copy take FX", [&]() -> nlohmann::json {
            int n_items = CountMediaItems(nullptr);
            if (item_idx < 0 || item_idx >= n_items || to_item < 0 || to_item >= n_items)
                return {{"_not_found", true}};
            MediaItem* src_it = GetMediaItem(nullptr, item_idx);
            MediaItem* dst_it = GetMediaItem(nullptr, to_item);
            if (!src_it || !dst_it)
                return {{"_not_found", true}};
            if (take_idx >= GetMediaItemNumTakes(src_it) || to_take >= GetMediaItemNumTakes(dst_it))
                return {{"_not_found", true}};
            MediaItem_Take* src_tk = GetMediaItemTake(src_it, take_idx);
            MediaItem_Take* dst_tk = GetMediaItemTake(dst_it, to_take);
            if (!src_tk || !dst_tk)
                return {{"_not_found", true}};
            int slot = resolve_fx_slot(src_tk, slot_token, TakeFX_GetCount, TakeFX_GetFXGUID);
            if (slot < 0)
                return {{"_bad_request", true},
                        {"_message", "FX slot out of range or GUID not found"}};
            std::string guid = fx_guid_str(src_tk, slot, TakeFX_GetFXGUID);
            TakeFX_CopyToTake(src_tk, slot, dst_tk, to_slot, move);
            return {{"from_item", item_idx},
                    {"from_take", take_idx},
                    {"from_slot", slot},
                    {"from_guid", guid},
                    {"to_item", to_item},
                    {"to_take", to_take},
                    {"moved", move},
                    {"dest_fx_count", TakeFX_GetCount(dst_tk)}};
        });
    });
    if (executor_error(res, result))
        return;
    json_ok(res, result);
}

// GET /state/items/{index}/takes/{take}/fx/{slot}/preset
void handle_take_get_fx_preset(const httplib::Request& req, httplib::Response& res) {
    int item_idx = 0, take_idx = 0;
    if (!path_int(req, res, "index", item_idx) || !path_int(req, res, "take", take_idx))
        return;
    std::string slot_token = req.path_params.at("slot");
    auto result = Executor::post([item_idx, take_idx, slot_token]() -> nlohmann::json {
        int n_items = CountMediaItems(nullptr);
        if (item_idx < 0 || item_idx >= n_items)
            return {{"_not_found", true}};
        MediaItem* it = GetMediaItem(nullptr, item_idx);
        if (!it || take_idx >= GetMediaItemNumTakes(it))
            return {{"_not_found", true}};
        MediaItem_Take* tk = GetMediaItemTake(it, take_idx);
        if (!tk)
            return {{"_not_found", true}};
        int slot = resolve_fx_slot(tk, slot_token, TakeFX_GetCount, TakeFX_GetFXGUID);
        if (slot < 0)
            return {{"_bad_request", true}, {"_message", "FX slot out of range or GUID not found"}};
        char pname[512] = {};
        TakeFX_GetPreset(tk, slot, pname, sizeof(pname));
        int total = 0;
        int cur = TakeFX_GetPresetIndex(tk, slot, &total);
        return {{"item", item_idx},
                {"take", take_idx},
                {"slot", slot},
                {"guid", fx_guid_str(tk, slot, TakeFX_GetFXGUID)},
                {"preset", pname},
                {"preset_index", cur},
                {"preset_count", total}};
    });
    if (executor_error(res, result))
        return;
    json_ok(res, result);
}

// POST /state/items/{index}/takes/{take}/fx/{slot}/preset
// Body: { "name": "..." }  OR  { "navigate": -1 | 1 }
void handle_take_set_fx_preset(const httplib::Request& req, httplib::Response& res) {
    int item_idx = 0, take_idx = 0;
    if (!path_int(req, res, "index", item_idx) || !path_int(req, res, "take", take_idx))
        return;
    std::string slot_token = req.path_params.at("slot");
    nlohmann::json body;
    if (!parse_body(req, res, body))
        return;
    bool has_name = body.contains("name") && body["name"].is_string();
    bool has_nav = body.contains("navigate") && body["navigate"].is_number_integer();
    if (!has_name && !has_nav) {
        json_error(res, 400, "Provide 'name' or 'navigate'", "BAD_REQUEST");
        return;
    }
    auto result = Executor::post(
            [item_idx, take_idx, slot_token, body, has_name, has_nav]() -> nlohmann::json {
                return with_undo("ReaClaw: load take FX preset", [&]() -> nlohmann::json {
                    int n_items = CountMediaItems(nullptr);
                    if (item_idx < 0 || item_idx >= n_items)
                        return {{"_not_found", true}};
                    MediaItem* it = GetMediaItem(nullptr, item_idx);
                    if (!it || take_idx >= GetMediaItemNumTakes(it))
                        return {{"_not_found", true}};
                    MediaItem_Take* tk = GetMediaItemTake(it, take_idx);
                    if (!tk)
                        return {{"_not_found", true}};
                    int slot = resolve_fx_slot(tk, slot_token, TakeFX_GetCount, TakeFX_GetFXGUID);
                    if (slot < 0)
                        return {{"_bad_request", true},
                                {"_message", "FX slot out of range or GUID not found"}};
                    bool ok = false;
                    if (has_name)
                        ok = TakeFX_SetPreset(tk, slot, body["name"].get<std::string>().c_str());
                    else
                        ok = TakeFX_NavigatePresets(tk, slot, body["navigate"].get<int>());
                    if (!ok)
                        return {{"_bad_request", true},
                                {"_message", "Preset not found / not changed"}};
                    char pname[512] = {};
                    TakeFX_GetPreset(tk, slot, pname, sizeof(pname));
                    int total = 0;
                    int cur = TakeFX_GetPresetIndex(tk, slot, &total);
                    return {{"item", item_idx},
                            {"take", take_idx},
                            {"slot", slot},
                            {"guid", fx_guid_str(tk, slot, TakeFX_GetFXGUID)},
                            {"preset", pname},
                            {"preset_index", cur},
                            {"preset_count", total}};
                });
            });
    if (executor_error(res, result))
        return;
    json_ok(res, result);
}

}  // namespace ReaClaw::Handlers

#pragma once

// Shared HTTP-handler plumbing used by the state/FX handler families:
// path/body parsing, executor error mapping, undo wrapping, and string search.
// Header-only so each handler TU inlines them without a link dependency.

#include "handlers/common.h"

#include <httplib.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>

#include <reaper_plugin_functions.h>

#include <json.hpp>

namespace ReaClaw::Handlers {

// Parse a numeric path param; returns false (and writes a 400) if not an int.
inline bool
path_int(const httplib::Request& req, httplib::Response& res, const char* key, int& out) {
    try {
        out = std::stoi(req.path_params.at(key));
        return true;
    } catch (...) {
        json_error(res, 400, std::string(key) + " must be a numeric integer", "BAD_REQUEST");
        return false;
    }
}

inline bool parse_body(const httplib::Request& req, httplib::Response& res, nlohmann::json& out) {
    try {
        out = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
        return true;
    } catch (...) {
        json_error(res, 400, "Invalid JSON body", "BAD_REQUEST");
        return false;
    }
}

// Map the executor result's internal markers to an HTTP error; returns true if
// an error was written (caller should return), false if the result is success.
inline bool executor_error(httplib::Response& res, const nlohmann::json& result) {
    if (result.contains("_timeout")) {
        json_error(res, 408, "Main thread timeout", "TIMEOUT");
        return true;
    }
    if (result.contains("_error")) {
        json_error(res, 500, result["_error"].get<std::string>(), "INTERNAL_ERROR");
        return true;
    }
    if (result.contains("_not_found")) {
        json_error(res, 404, "Track index out of range", "NOT_FOUND");
        return true;
    }
    if (result.contains("_bad_request")) {
        std::string m = result.value("_message", "Bad request");
        json_error(res, 400, m, "BAD_REQUEST");
        return true;
    }
    return false;
}

// Run a mutating body inside a REAPER undo block so the change lands as one
// coherent, user-undoable step (Edit > Undo "<desc>"). Main-thread only — call
// from inside an Executor::post() lambda. When the body reports a validation
// error (it mutated nothing), the block is closed with extraflags 0 so REAPER
// creates no undo point, keeping the history clean on no-ops.
inline nlohmann::json with_undo(const char* desc, const std::function<nlohmann::json()>& body) {
    Undo_BeginBlock2(nullptr);
    nlohmann::json r = body();
    const bool changed = !(r.contains("_not_found") || r.contains("_bad_request") ||
                           r.contains("_error"));
    Undo_EndBlock2(nullptr, desc, changed ? -1 : 0);
    return r;
}

// Case-insensitive substring match (?q= searches).
inline bool ci_contains(const std::string& haystack, const std::string& needle) {
    auto it = std::search(haystack.begin(),
                          haystack.end(),
                          needle.begin(),
                          needle.end(),
                          [](unsigned char a, unsigned char b) {
                              return std::tolower(a) == std::tolower(b);
                          });
    return it != haystack.end();
}

// Strip optional surrounding braces and lowercase, for tolerant GUID compares
// (issue #102) — accepts both "{XXXX-...}" (matches guid_to_string's output)
// and the bare hyphenated form.
inline std::string normalize_guid(std::string s) {
    if (s.size() >= 2 && s.front() == '{' && s.back() == '}')
        s = s.substr(1, s.size() - 2);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

// Resolve an FX slot path token that is either a numeric chain index or a
// GUID string (issue #102) to a concrete 0-based slot index, or -1 if not
// found. Slot index is positional and shifts when the chain changes shape;
// GUID identifies a specific plugin instance regardless of chain position.
// Main-thread only — call from inside an Executor::post() lambda, once the
// track/take handle is in hand. FnCount/FnGuid are the API family's function
// pointers (TrackFX_GetCount/TrackFX_GetFXGUID or the TakeFX_ equivalents).
template <typename H, typename FnCount, typename FnGuid>
int resolve_fx_slot(H* h, const std::string& token, FnCount fn_count, FnGuid fn_guid) {
    int n = fn_count(h);
    size_t pos = 0;
    try {
        int idx = std::stoi(token, &pos);
        if (pos == token.size())
            return (idx >= 0 && idx < n) ? idx : -1;
    } catch (...) {
    }
    std::string want = normalize_guid(token);
    for (int i = 0; i < n; i++) {
        if (auto* g = fn_guid(h, i)) {
            if (normalize_guid(guid_to_string(g)) == want)
                return i;
        }
    }
    return -1;
}

}  // namespace ReaClaw::Handlers

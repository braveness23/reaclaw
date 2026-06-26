#pragma once

#include <string>

#include <json.hpp>

namespace ReaClaw::jsondiff {

// Pure, REAPER-free recursive diff of two JSON values. Emits a flat array of
// change records so the shared snapshot/state-diff layer (Epic #20 prep, also
// the #19 A/B diff) and the correction-mining can ask "what changed between
// these two snapshots". Header-only so it unit-tests on its own, like dsp.h.
//
// Each record is { "path", "op", "from"?, "to"? }:
//   op "changed" — scalar/leaf differs (carries from + to)
//   op "added"   — present in b, absent in a (carries to)
//   op "removed" — present in a, absent in b (carries from)
// Objects diff by key (union); arrays diff by index (to max length). A path is
// a slash-joined trail like "tracks/2/volume_db".

inline std::string join(const std::string& base, const std::string& key) {
    return base.empty() ? key : base + "/" + key;
}

inline void diff_into(const nlohmann::json& a,
                      const nlohmann::json& b,
                      const std::string& path,
                      nlohmann::json& out) {
    if (a.is_object() && b.is_object()) {
        // Union of keys, stable-ish: a's keys first, then b-only keys.
        for (auto it = a.begin(); it != a.end(); ++it) {
            if (b.contains(it.key()))
                diff_into(it.value(), b[it.key()], join(path, it.key()), out);
            else
                out.push_back(
                        {{"path", join(path, it.key())}, {"op", "removed"}, {"from", it.value()}});
        }
        for (auto it = b.begin(); it != b.end(); ++it) {
            if (!a.contains(it.key()))
                out.push_back(
                        {{"path", join(path, it.key())}, {"op", "added"}, {"to", it.value()}});
        }
        return;
    }
    if (a.is_array() && b.is_array()) {
        size_t n = std::max(a.size(), b.size());
        for (size_t i = 0; i < n; i++) {
            std::string p = join(path, std::to_string(i));
            if (i < a.size() && i < b.size())
                diff_into(a[i], b[i], p, out);
            else if (i < b.size())
                out.push_back({{"path", p}, {"op", "added"}, {"to", b[i]}});
            else
                out.push_back({{"path", p}, {"op", "removed"}, {"from", a[i]}});
        }
        return;
    }
    if (a != b)
        out.push_back({{"path", path}, {"op", "changed"}, {"from", a}, {"to", b}});
}

inline nlohmann::json diff(const nlohmann::json& a, const nlohmann::json& b) {
    nlohmann::json out = nlohmann::json::array();
    diff_into(a, b, "", out);
    return out;
}

}  // namespace ReaClaw::jsondiff

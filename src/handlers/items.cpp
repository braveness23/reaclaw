#include "handlers/items.h"

#include "handlers/common.h"
#include "handlers/hints.h"
#include "reaper/executor.h"
#include "util/logging.h"

#include <httplib.h>

#include <algorithm>
#include <functional>
#include <string>

#include <reaper_plugin_functions.h>

#include <json.hpp>

namespace ReaClaw::Handlers {

namespace {

// Shared executor-result → HTTP error mapping (mirrors state.cpp/project.cpp).
bool exec_error(httplib::Response& res, const nlohmann::json& result) {
    if (result.contains("_timeout")) {
        json_error(res, 408, "Main thread timeout", "TIMEOUT");
        return true;
    }
    if (result.contains("_error")) {
        json_error(res, 500, result["_error"].get<std::string>(), "INTERNAL_ERROR");
        return true;
    }
    if (result.contains("_not_found")) {
        json_error(res, 404, "Item index out of range", "NOT_FOUND");
        return true;
    }
    if (result.contains("_bad_request")) {
        json_error(res, 400, result.value("_message", "Bad request"), "BAD_REQUEST");
        return true;
    }
    return false;
}

bool path_int(const httplib::Request& req, httplib::Response& res, const char* key, int& out) {
    try {
        out = std::stoi(req.path_params.at(key));
        return true;
    } catch (...) {
        json_error(res, 400, std::string(key) + " must be a numeric integer", "BAD_REQUEST");
        return false;
    }
}

bool parse_body(const httplib::Request& req, httplib::Response& res, nlohmann::json& out) {
    try {
        out = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
        return true;
    } catch (...) {
        json_error(res, 400, "Invalid JSON body", "BAD_REQUEST");
        return false;
    }
}

// Run a mutating body inside a REAPER undo block (mirrors state.cpp::with_undo).
// On a validation no-op the block closes with extraflags 0 so no undo point is
// created, keeping the history clean.
nlohmann::json with_undo(const char* desc, const std::function<nlohmann::json()>& body) {
    Undo_BeginBlock2(nullptr);
    nlohmann::json r = body();
    const bool changed = !(r.contains("_not_found") || r.contains("_bad_request") ||
                           r.contains("_error"));
    Undo_EndBlock2(nullptr, desc, changed ? -1 : 0);
    return r;
}

int item_track_index(MediaItem* item) {
    MediaTrack* tr = GetMediaItem_Track(item);
    if (!tr)
        return -1;
    return static_cast<int>(GetMediaTrackInfo_Value(tr, "IP_TRACKNUMBER")) - 1;
}

// Source metadata for the active take (or null when the item has no source).
nlohmann::json source_json(MediaItem_Take* take) {
    if (!take)
        return nullptr;
    PCM_source* src = GetMediaItemTake_Source(take);
    if (!src)
        return nullptr;
    char file[4096] = {};
    GetMediaSourceFileName(src, file, sizeof(file));
    char type[64] = {};
    GetMediaSourceType(src, type, sizeof(type));
    bool is_qn = false;
    double length = GetMediaSourceLength(src, &is_qn);
    return {{"file", file},
            {"type", type},
            {"length", length},
            {"length_is_beats", is_qn},
            {"sample_rate", GetMediaSourceSampleRate(src)},
            {"num_channels", GetMediaSourceNumChannels(src)}};
}

// Active take properties (vol/pan/pitch/rate), or null for an empty item.
nlohmann::json take_json(MediaItem_Take* take) {
    if (!take)
        return nullptr;
    const char* nm = GetTakeName(take);
    double vol = GetMediaItemTakeInfo_Value(take, "D_VOL");
    return {{"name", nm ? nm : ""},
            {"volume_db", vol_to_db(std::fabs(vol))},
            {"polarity_flipped", vol < 0.0},
            {"pan", GetMediaItemTakeInfo_Value(take, "D_PAN")},
            {"pitch", GetMediaItemTakeInfo_Value(take, "D_PITCH")},
            {"playrate", GetMediaItemTakeInfo_Value(take, "D_PLAYRATE")},
            {"preserve_pitch", GetMediaItemTakeInfo_Value(take, "B_PPITCH") != 0.0}};
}

nlohmann::json item_to_json(MediaItem* item, int index) {
    if (!item)
        return nlohmann::json::object();
    MediaItem_Take* take = GetActiveTake(item);
    return {{"index", index},
            {"position", GetMediaItemInfo_Value(item, "D_POSITION")},
            {"length", GetMediaItemInfo_Value(item, "D_LENGTH")},
            {"track_index", item_track_index(item)},
            {"selected", GetMediaItemInfo_Value(item, "B_UISEL") != 0.0},
            {"muted", GetMediaItemInfo_Value(item, "B_MUTE") != 0.0},
            {"volume_db", vol_to_db(GetMediaItemInfo_Value(item, "D_VOL"))},
            {"fade_in", GetMediaItemInfo_Value(item, "D_FADEINLEN")},
            {"fade_out", GetMediaItemInfo_Value(item, "D_FADEOUTLEN")},
            {"take", take_json(take)},
            {"source", source_json(take)}};
}

// Apply the writable item/take fields present in a JSON object. Main-thread
// only. Returns the resolved MediaItem so the caller can re-serialize it. The
// `track` field moves the item to another track (MoveMediaItemToTrack).
void apply_item_props(MediaItem* item, const nlohmann::json& b) {
    if (b.contains("position") && b["position"].is_number())
        SetMediaItemInfo_Value(item, "D_POSITION", b["position"].get<double>());
    if (b.contains("length") && b["length"].is_number())
        SetMediaItemInfo_Value(item, "D_LENGTH", std::max(0.0, b["length"].get<double>()));
    if (b.contains("volume_db") && b["volume_db"].is_number())
        SetMediaItemInfo_Value(item, "D_VOL", db_to_vol(b["volume_db"].get<double>()));
    if (b.contains("muted") && b["muted"].is_boolean())
        SetMediaItemInfo_Value(item, "B_MUTE", b["muted"].get<bool>() ? 1.0 : 0.0);
    if (b.contains("selected") && b["selected"].is_boolean())
        SetMediaItemSelected(item, b["selected"].get<bool>());
    if (b.contains("fade_in") && b["fade_in"].is_number())
        SetMediaItemInfo_Value(item, "D_FADEINLEN", std::max(0.0, b["fade_in"].get<double>()));
    if (b.contains("fade_out") && b["fade_out"].is_number())
        SetMediaItemInfo_Value(item, "D_FADEOUTLEN", std::max(0.0, b["fade_out"].get<double>()));
    if (b.contains("track") && b["track"].is_number_integer()) {
        int t = b["track"].get<int>();
        if (t >= 0 && t < CountTracks(nullptr))
            MoveMediaItemToTrack(item, GetTrack(nullptr, t));
    }

    if (b.contains("take") && b["take"].is_object()) {
        MediaItem_Take* take = GetActiveTake(item);
        if (take) {
            const auto& tk = b["take"];
            if (tk.contains("name") && tk["name"].is_string()) {
                std::string nm = tk["name"].get<std::string>();
                GetSetMediaItemTakeInfo_String(take, "P_NAME", const_cast<char*>(nm.c_str()), true);
            }
            if (tk.contains("volume_db") && tk["volume_db"].is_number())
                SetMediaItemTakeInfo_Value(take, "D_VOL", db_to_vol(tk["volume_db"].get<double>()));
            if (tk.contains("pan") && tk["pan"].is_number())
                SetMediaItemTakeInfo_Value(
                        take, "D_PAN", std::max(-1.0, std::min(1.0, tk["pan"].get<double>())));
            if (tk.contains("pitch") && tk["pitch"].is_number())
                SetMediaItemTakeInfo_Value(take, "D_PITCH", tk["pitch"].get<double>());
            if (tk.contains("playrate") && tk["playrate"].is_number())
                SetMediaItemTakeInfo_Value(take, "D_PLAYRATE", tk["playrate"].get<double>());
            if (tk.contains("preserve_pitch") && tk["preserve_pitch"].is_boolean())
                SetMediaItemTakeInfo_Value(
                        take, "B_PPITCH", tk["preserve_pitch"].get<bool>() ? 1.0 : 0.0);
        }
    }
}

// Resolve a project-wide item index to a MediaItem*, or nullptr if out of range.
MediaItem* item_at(int index) {
    if (index < 0 || index >= CountMediaItems(nullptr))
        return nullptr;
    return GetMediaItem(nullptr, index);
}

}  // namespace

// GET /state/items — all media items, enriched with take + source metadata.
void handle_items_get(const httplib::Request& req, httplib::Response& res) {
    (void)req;
    auto result = Executor::post([]() -> nlohmann::json {
        int n = CountMediaItems(nullptr);
        nlohmann::json items = nlohmann::json::array();
        for (int i = 0; i < n; i++)
            items.push_back(item_to_json(GetMediaItem(nullptr, i), i));
        return {{"items", items}, {"total", n}};
    });
    if (exec_error(res, result))
        return;
    json_ok(res, result);
}

// GET /state/items/{index} — one media item in full.
void handle_item_get(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    if (!path_int(req, res, "index", index))
        return;
    auto result = Executor::post([index]() -> nlohmann::json {
        MediaItem* it = item_at(index);
        if (!it)
            return {{"_not_found", true}};
        return item_to_json(it, index);
    });
    if (exec_error(res, result))
        return;
    json_ok(res, result);
}

// POST /state/items — create and/or batch-update media items.
// Body: { "create": [ {track, position, length?, file?} ],
//         "update": [ {index, position?, length?, track?, selected?, muted?,
//                      volume_db?, fade_in?, fade_out?, take?:{...}} ] }
// A create with "file" loads that audio/MIDI file as the item's source; without
// it the item is empty (default length 1.0s unless given).
void handle_items_post(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    if (!parse_body(req, res, body))
        return;
    bool has_create = body.contains("create") && body["create"].is_array();
    bool has_update = body.contains("update") && body["update"].is_array();
    if (!has_create && !has_update) {
        json_error(res, 400, "Provide a 'create' and/or 'update' array", "BAD_REQUEST");
        return;
    }

    auto result = Executor::post([body, has_create, has_update]() -> nlohmann::json {
        return with_undo("ReaClaw: edit items", [&]() -> nlohmann::json {
            nlohmann::json created = nlohmann::json::array();
            nlohmann::json updated = nlohmann::json::array();
            if (has_create) {
                for (const auto& spec : body["create"]) {
                    if (!spec.is_object() || !spec.contains("track") ||
                        !spec["track"].is_number_integer())
                        continue;
                    int t = spec["track"].get<int>();
                    if (t < 0 || t >= CountTracks(nullptr))
                        continue;
                    MediaTrack* tr = GetTrack(nullptr, t);
                    MediaItem* it = AddMediaItemToTrack(tr);
                    if (!it)
                        continue;
                    double pos = spec.value("position", 0.0);
                    SetMediaItemInfo_Value(it, "D_POSITION", pos);
                    double length = spec.value("length", -1.0);
                    if (spec.contains("file") && spec["file"].is_string()) {
                        PCM_source* src = PCM_Source_CreateFromFile(
                                spec["file"].get<std::string>().c_str());
                        if (src) {
                            MediaItem_Take* tk = AddTakeToMediaItem(it);
                            SetMediaItemTake_Source(tk, src);
                            if (length < 0.0) {
                                bool qn = false;
                                length = GetMediaSourceLength(src, &qn);
                            }
                        }
                    }
                    SetMediaItemInfo_Value(it, "D_LENGTH", length >= 0.0 ? length : 1.0);
                    apply_item_props(it, spec);  // honor take/selection on create too
                    int idx = static_cast<int>(GetMediaItemInfo_Value(it, "IP_ITEMNUMBER"));
                    auto j = item_to_json(it, idx);
                    j["hints"] = Hints::for_item(it, idx);
                    created.push_back(j);
                }
            }
            if (has_update) {
                for (const auto& u : body["update"]) {
                    if (!u.is_object() || !u.contains("index") || !u["index"].is_number_integer())
                        continue;
                    MediaItem* it = item_at(u["index"].get<int>());
                    if (!it)
                        continue;
                    apply_item_props(it, u);
                    updated.push_back(item_to_json(it, u["index"].get<int>()));
                }
            }
            UpdateArrange();
            return {{"created", created}, {"updated", updated}};
        });
    });
    if (exec_error(res, result))
        return;
    Log::info("Items: created " + std::to_string(result["created"].size()) + ", updated " +
              std::to_string(result["updated"].size()));
    json_ok(res, result);
}

// POST /state/items/{index} — update one media item (same fields as a single
// element of the batch "update" array).
void handle_item_update(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    if (!path_int(req, res, "index", index))
        return;
    nlohmann::json body;
    if (!parse_body(req, res, body))
        return;
    auto result = Executor::post([index, body]() -> nlohmann::json {
        return with_undo("ReaClaw: update item", [&]() -> nlohmann::json {
            MediaItem* it = item_at(index);
            if (!it)
                return {{"_not_found", true}};
            apply_item_props(it, body);
            UpdateArrange();
            auto j = item_to_json(it, index);
            j["hints"] = Hints::for_item(it, index);
            return j;
        });
    });
    if (exec_error(res, result))
        return;
    json_ok(res, result);
}

// POST /state/items/{index}/split — split at a position (seconds).
// Body: { "position": SECONDS }. Returns the indices of both resulting items.
void handle_item_split(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    if (!path_int(req, res, "index", index))
        return;
    nlohmann::json body;
    if (!parse_body(req, res, body))
        return;
    if (!body.contains("position") || !body["position"].is_number()) {
        json_error(res, 400, "Missing required field: position", "BAD_REQUEST");
        return;
    }
    double position = body["position"].get<double>();
    auto result = Executor::post([index, position]() -> nlohmann::json {
        return with_undo("ReaClaw: split item", [&]() -> nlohmann::json {
            MediaItem* it = item_at(index);
            if (!it)
                return {{"_not_found", true}};
            MediaItem* right = SplitMediaItem(it, position);
            if (!right)
                return {{"_bad_request", true},
                        {"_message", "Split position is outside the item bounds"}};
            UpdateArrange();
            int left_idx = static_cast<int>(GetMediaItemInfo_Value(it, "IP_ITEMNUMBER"));
            int right_idx = static_cast<int>(GetMediaItemInfo_Value(right, "IP_ITEMNUMBER"));
            return {{"left", item_to_json(it, left_idx)},
                    {"right", item_to_json(right, right_idx)}};
        });
    });
    if (exec_error(res, result))
        return;
    json_ok(res, result);
}

// DELETE /state/items/{index} — remove a media item from its track.
void handle_item_delete(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    if (!path_int(req, res, "index", index))
        return;
    auto result = Executor::post([index]() -> nlohmann::json {
        return with_undo("ReaClaw: delete item", [&]() -> nlohmann::json {
            MediaItem* it = item_at(index);
            if (!it)
                return {{"_not_found", true}};
            MediaTrack* tr = GetMediaItem_Track(it);
            if (!tr)
                return {{"_not_found", true}};
            bool ok = DeleteTrackMediaItem(tr, it);
            UpdateArrange();
            return {{"deleted", ok}, {"index", index}};
        });
    });
    if (exec_error(res, result))
        return;
    Log::info("Item " + std::to_string(index) + " deleted");
    json_ok(res, result);
}

}  // namespace ReaClaw::Handlers

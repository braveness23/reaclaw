#include "handlers/midi.h"

#include "handlers/common.h"
#include "reaper/executor.h"
#include "util/logging.h"
#include "util/midi_util.h"

#include <httplib.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include <reaper_plugin_functions.h>

#include <json.hpp>

namespace ReaClaw::Handlers {

namespace {

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

nlohmann::json with_undo(const char* desc, const std::function<nlohmann::json()>& body) {
    Undo_BeginBlock2(nullptr);
    nlohmann::json r = body();
    const bool changed = !(r.contains("_not_found") || r.contains("_bad_request") ||
                           r.contains("_error"));
    Undo_EndBlock2(nullptr, desc, changed ? -1 : 0);
    return r;
}

// Resolve item index → active MIDI take. Sets is_midi=true only when the
// active take's source is a MIDI source. Returns nullptr on lookup failure.
MediaItem_Take* midi_take_at(int index, bool& is_midi) {
    is_midi = false;
    if (index < 0 || index >= CountMediaItems(nullptr))
        return nullptr;
    MediaItem* item = GetMediaItem(nullptr, index);
    if (!item)
        return nullptr;
    MediaItem_Take* take = GetActiveTake(item);
    if (!take)
        return nullptr;
    PCM_source* src = GetMediaItemTake_Source(take);
    if (!src)
        return nullptr;
    char type_buf[32] = {};
    GetMediaSourceType(src, type_buf, sizeof(type_buf));
    is_midi = (std::string(type_buf) == "MIDI");
    return take;
}

nlohmann::json note_to_json(MediaItem_Take* take, int idx) {
    bool selected = false, muted = false;
    double start_ppq = 0.0, end_ppq = 0.0;
    int chan = 0, pitch = 0, vel = 0;
    if (!MIDI_GetNote(take, idx, &selected, &muted, &start_ppq, &end_ppq, &chan, &pitch, &vel))
        return nullptr;
    double start_time = MIDI_GetProjTimeFromPPQPos(take, start_ppq);
    double end_time = MIDI_GetProjTimeFromPPQPos(take, end_ppq);
    return {{"index", idx},
            {"pitch", pitch},
            {"note", midi_util::pitch_to_note_name(pitch)},
            {"channel", chan},
            {"velocity", vel},
            {"start_ppq", start_ppq},
            {"end_ppq", end_ppq},
            {"start_time", start_time},
            {"end_time", end_time},
            {"selected", selected},
            {"muted", muted}};
}

nlohmann::json cc_to_json(MediaItem_Take* take, int idx) {
    bool selected = false, muted = false;
    double ppq = 0.0;
    int chanmsg = 0, chan = 0, msg2 = 0, msg3 = 0;
    if (!MIDI_GetCC(take, idx, &selected, &muted, &ppq, &chanmsg, &chan, &msg2, &msg3))
        return nullptr;
    double time = MIDI_GetProjTimeFromPPQPos(take, ppq);
    return {{"index", idx},
            {"chanmsg", chanmsg},
            {"number", msg2},
            {"value", msg3},
            {"channel", chan},
            {"ppq", ppq},
            {"time", time},
            {"selected", selected},
            {"muted", muted}};
}

// Parse a note spec from the POST body into PPQ values for MIDI_InsertNote.
// Accepts start_ppq/end_ppq directly, or start_time/end_time (project seconds)
// converted via MIDI_GetPPQPosFromProjTime. Returns false and fills err on failure.
bool parse_note_spec(const nlohmann::json& spec,
                     MediaItem_Take* take,
                     double& start_ppq,
                     double& end_ppq,
                     int& chan,
                     int& pitch,
                     int& vel,
                     std::string& err) {
    if (!spec.contains("pitch") || !spec["pitch"].is_number_integer()) {
        err = "note missing required field: pitch (0-127)";
        return false;
    }
    pitch = spec["pitch"].get<int>();
    if (pitch < 0 || pitch > 127) {
        err = "pitch must be 0-127";
        return false;
    }
    chan = spec.value("channel", 0);
    if (chan < 0 || chan > 15) {
        err = "channel must be 0-15";
        return false;
    }
    vel = spec.value("velocity", 100);
    if (vel < 1 || vel > 127) {
        err = "velocity must be 1-127";
        return false;
    }
    if (spec.contains("start_ppq") && spec["start_ppq"].is_number()) {
        start_ppq = spec["start_ppq"].get<double>();
        end_ppq = spec.contains("end_ppq") && spec["end_ppq"].is_number()
                          ? spec["end_ppq"].get<double>()
                          : start_ppq + 480.0;  // quarter-note default at 960 PPQ
    } else if (spec.contains("start_time") && spec["start_time"].is_number()) {
        start_ppq = MIDI_GetPPQPosFromProjTime(take, spec["start_time"].get<double>());
        end_ppq = spec.contains("end_time") && spec["end_time"].is_number()
                          ? MIDI_GetPPQPosFromProjTime(take, spec["end_time"].get<double>())
                          : start_ppq + 480.0;
    } else {
        err = "note missing position: provide start_ppq/end_ppq or start_time/end_time";
        return false;
    }
    if (end_ppq <= start_ppq) {
        err = "end position must be after start position";
        return false;
    }
    return true;
}

// Parse a CC spec from the POST body. chanmsg defaults to 0xB0 (Control Change).
bool parse_cc_spec(const nlohmann::json& spec,
                   MediaItem_Take* take,
                   double& ppq,
                   int& chanmsg,
                   int& chan,
                   int& number,
                   int& value,
                   std::string& err) {
    if (!spec.contains("number") || !spec["number"].is_number_integer()) {
        err = "cc missing required field: number (0-127)";
        return false;
    }
    number = spec["number"].get<int>();
    if (number < 0 || number > 127) {
        err = "cc number must be 0-127";
        return false;
    }
    if (!spec.contains("value") || !spec["value"].is_number_integer()) {
        err = "cc missing required field: value (0-127)";
        return false;
    }
    value = spec["value"].get<int>();
    if (value < 0 || value > 127) {
        err = "cc value must be 0-127";
        return false;
    }
    chan = spec.value("channel", 0);
    if (chan < 0 || chan > 15) {
        err = "channel must be 0-15";
        return false;
    }
    chanmsg = spec.value("chanmsg", 0xB0);
    if (spec.contains("ppq") && spec["ppq"].is_number()) {
        ppq = spec["ppq"].get<double>();
    } else if (spec.contains("time") && spec["time"].is_number()) {
        ppq = MIDI_GetPPQPosFromProjTime(take, spec["time"].get<double>());
    } else {
        err = "cc missing position: provide ppq or time (seconds)";
        return false;
    }
    return true;
}

}  // namespace

// GET /state/items/{index}/midi
//
// Returns all MIDI notes and CC events from the item's active take.
// Notes carry both PPQ positions (take-relative) and project times (seconds).
// Returns 400 when the active take is not a MIDI source.
void handle_midi_get(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    if (!path_int(req, res, "index", index))
        return;
    auto result = Executor::post([index]() -> nlohmann::json {
        bool is_midi = false;
        MediaItem_Take* take = midi_take_at(index, is_midi);
        if (!take)
            return {{"_not_found", true}};
        if (!is_midi)
            return {{"_bad_request", true}, {"_message", "active take is not a MIDI source"}};

        int notecnt = 0, cccnt = 0, textsyxcnt = 0;
        MIDI_CountEvts(take, &notecnt, &cccnt, &textsyxcnt);

        nlohmann::json notes = nlohmann::json::array();
        for (int i = 0; i < notecnt; i++) {
            auto n = note_to_json(take, i);
            if (!n.is_null())
                notes.push_back(std::move(n));
        }

        nlohmann::json cc = nlohmann::json::array();
        for (int i = 0; i < cccnt; i++) {
            auto c = cc_to_json(take, i);
            if (!c.is_null())
                cc.push_back(std::move(c));
        }

        return {{"item_index", index},
                {"note_count", notecnt},
                {"cc_count", cccnt},
                {"notes", notes},
                {"cc", cc}};
    });
    if (exec_error(res, result))
        return;
    json_ok(res, result);
}

// POST /state/items/{index}/midi
//
// Insert (or replace) MIDI notes and CC events in the active take.
//
// Body:
//   {
//     "notes": [
//       { "pitch": 60, "channel": 0, "velocity": 100,
//         "start_ppq": 0.0, "end_ppq": 480.0 }       // PPQ positions
//       { "pitch": 64, "channel": 0, "velocity": 80,
//         "start_time": 0.0, "end_time": 0.5 }        // project seconds
//     ],
//     "cc": [
//       { "number": 7, "value": 100, "channel": 0, "ppq": 0.0 }
//       { "number": 11, "value": 64, "channel": 0, "time": 1.0 }
//     ],
//     "replace": false   // default false; true = delete all existing notes+CC first
//   }
//
// Note defaults: channel=0, velocity=100. Duration defaults to one quarter note
// (480 PPQ at REAPER's default 960 PPQ resolution) when end position is omitted.
// CC chanmsg defaults to 0xB0 (Control Change).
// All changes are wrapped in a single undo block.
void handle_midi_post(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    if (!path_int(req, res, "index", index))
        return;
    nlohmann::json body;
    if (!parse_body(req, res, body))
        return;

    bool replace = body.value("replace", false);
    nlohmann::json notes_spec = (body.contains("notes") && body["notes"].is_array())
                                        ? body["notes"]
                                        : nlohmann::json::array();
    nlohmann::json cc_spec = (body.contains("cc") && body["cc"].is_array())
                                     ? body["cc"]
                                     : nlohmann::json::array();

    auto result = Executor::post([index, replace, notes_spec, cc_spec]() -> nlohmann::json {
        return with_undo("ReaClaw: edit MIDI", [&]() -> nlohmann::json {
            bool is_midi = false;
            MediaItem_Take* take = midi_take_at(index, is_midi);
            if (!take)
                return {{"_not_found", true}};
            if (!is_midi)
                return {{"_bad_request", true}, {"_message", "active take is not a MIDI source"}};

            int notes_deleted = 0, cc_deleted = 0;

            MIDI_DisableSort(take);

            if (replace) {
                int notecnt = 0, cccnt = 0, tmp = 0;
                MIDI_CountEvts(take, &notecnt, &cccnt, &tmp);
                for (int i = notecnt - 1; i >= 0; --i) {
                    MIDI_DeleteNote(take, i);
                    ++notes_deleted;
                }
                for (int i = cccnt - 1; i >= 0; --i) {
                    MIDI_DeleteCC(take, i);
                    ++cc_deleted;
                }
            }

            int notes_inserted = 0, cc_inserted = 0;
            std::vector<std::string> warnings;

            for (const auto& spec : notes_spec) {
                if (!spec.is_object())
                    continue;
                double start_ppq = 0.0, end_ppq = 0.0;
                int chan = 0, pitch = 0, vel = 0;
                std::string err;
                if (!parse_note_spec(spec, take, start_ppq, end_ppq, chan, pitch, vel, err)) {
                    warnings.push_back(err);
                    continue;
                }
                const bool no_sort = true;
                if (MIDI_InsertNote(
                            take, false, false, start_ppq, end_ppq, chan, pitch, vel, &no_sort))
                    ++notes_inserted;
            }

            for (const auto& spec : cc_spec) {
                if (!spec.is_object())
                    continue;
                double ppq = 0.0;
                int chanmsg = 0, chan = 0, number = 0, value = 0;
                std::string err;
                if (!parse_cc_spec(spec, take, ppq, chanmsg, chan, number, value, err)) {
                    warnings.push_back(err);
                    continue;
                }
                if (MIDI_InsertCC(take, false, false, ppq, chanmsg, chan, number, value))
                    ++cc_inserted;
            }

            MIDI_Sort(take);

            nlohmann::json r = {{"ok", true},
                                {"notes_inserted", notes_inserted},
                                {"cc_inserted", cc_inserted},
                                {"notes_deleted", notes_deleted},
                                {"cc_deleted", cc_deleted}};
            if (!warnings.empty())
                r["warnings"] = warnings;
            return r;
        });
    });
    if (exec_error(res, result))
        return;
    Log::info("MIDI edit item " + std::to_string(index) + ": " +
              std::to_string(result.value("notes_inserted", 0)) + " notes, " +
              std::to_string(result.value("cc_inserted", 0)) + " cc inserted");
    json_ok(res, result);
}

}  // namespace ReaClaw::Handlers

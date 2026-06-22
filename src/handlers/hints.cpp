#include "handlers/hints.h"

#include <cmath>
#include <string>

#include <reaper_plugin_functions.h>

namespace ReaClaw::Hints {

namespace {

void add(nlohmann::json& arr, const char* code, const char* severity, const std::string& msg) {
    arr.push_back({{"code", code}, {"severity", severity}, {"message", msg}});
}

double track_vol_db(MediaTrack* t) {
    double v = GetMediaTrackInfo_Value(t, "D_VOL");
    if (v <= 0.0)
        return -150.0;
    return 20.0 * std::log10(v);
}

bool track_muted(MediaTrack* t) {
    return GetMediaTrackInfo_Value(t, "B_MUTE") != 0.0;
}

// Does this track's audio ultimately reach the master? True if it sends to the
// parent/master directly, otherwise if any of its track sends lands somewhere
// that itself reaches the master. Bounded walk to avoid cycles.
bool routes_to_master(MediaTrack* t, int depth = 0) {
    if (!t || depth > 16)
        return false;
    if (GetMediaTrackInfo_Value(t, "B_MAINSEND") != 0.0)
        return true;
    int n = GetTrackNumSends(t, 0);
    for (int s = 0; s < n; s++) {
        if (auto* dst = static_cast<MediaTrack*>(
                    GetSetTrackSendInfo(t, 0, s, "P_DESTTRACK", nullptr)))
            if (routes_to_master(dst, depth + 1))
                return true;
    }
    return false;
}

// Invariants shared by any edit to a track. `verb` describes the edit so the
// message reads as a consequence (e.g. "This FX", "This item").
void track_invariants(nlohmann::json& h, MediaTrack* t, int idx, const char* subject) {
    std::string s(subject);
    std::string ti = "track " + std::to_string(idx);

    if (track_muted(t))
        add(h, "muted_track", "warn", s + " is on " + ti + ", which is muted — you won't hear it.");

    if (AnyTrackSolo(nullptr) && GetMediaTrackInfo_Value(t, "I_SOLO") == 0.0)
        add(h,
            "solo_elsewhere",
            "warn",
            s + " is on " + ti +
                    ", but another track is soloed — this track is silenced until you clear "
                    "the solo.");

    double db = track_vol_db(t);
    if (db <= -60.0)
        add(h,
            "near_silent_fader",
            "warn",
            ti + " fader is at " + std::to_string(static_cast<int>(db)) +
                    " dB — effectively silent.");

    if (!routes_to_master(t))
        add(h,
            "routes_nowhere",
            "warn",
            ti +
                    " does not route to the master (parent send off and no send chain reaches "
                    "it) — its audio dead-ends.");

    if (GetMediaTrackInfo_Value(t, "B_PHASE") != 0.0)
        add(h, "phase_inverted", "info", ti + " has its phase inverted.");
}

}  // namespace

nlohmann::json for_track(MediaTrack* track, int track_index) {
    nlohmann::json h = nlohmann::json::array();
    if (!track)
        return h;
    track_invariants(h, track, track_index, "This track");

    // Record-arm with no input is a track-level consequence worth flagging.
    if (GetMediaTrackInfo_Value(track, "I_RECARM") != 0.0 &&
        GetMediaTrackInfo_Value(track, "I_RECINPUT") < 0.0)
        add(h,
            "recarm_no_input",
            "warn",
            "track " + std::to_string(track_index) +
                    " is record-armed but has no record input assigned — it will record "
                    "nothing.");
    return h;
}

nlohmann::json for_fx(MediaTrack* track, int track_index, int fx_slot) {
    nlohmann::json h = nlohmann::json::array();
    if (!track)
        return h;
    track_invariants(h, track, track_index, "This FX");

    if (fx_slot >= 0 && fx_slot < TrackFX_GetCount(track)) {
        if (TrackFX_GetOffline(track, fx_slot))
            add(h, "fx_offline", "warn", "This FX is offline — it will not process audio.");
        else if (!TrackFX_GetEnabled(track, fx_slot))
            add(h, "fx_bypassed", "info", "This FX is bypassed (disabled).");
    }
    return h;
}

nlohmann::json for_send(MediaTrack* src, int src_index, MediaTrack* dest) {
    nlohmann::json h = nlohmann::json::array();
    if (!src)
        return h;
    track_invariants(h, src, src_index, "This send's source track");

    if (dest) {
        if (!routes_to_master(dest))
            add(h,
                "send_dest_routes_nowhere",
                "warn",
                "The send destination does not route to the master — audio sent there "
                "dead-ends.");
        if (track_muted(dest))
            add(h, "send_dest_muted", "warn", "The send destination track is muted.");
    }
    return h;
}

nlohmann::json for_item(MediaItem* item, int item_index) {
    (void)item_index;
    nlohmann::json h = nlohmann::json::array();
    if (!item)
        return h;
    MediaTrack* t = GetMediaItem_Track(item);
    int tidx = t ? static_cast<int>(GetMediaTrackInfo_Value(t, "IP_TRACKNUMBER")) - 1 : -1;
    if (t)
        track_invariants(h, t, tidx, "This item");

    MediaItem_Take* take = GetActiveTake(item);
    if (!take) {
        add(h,
            "empty_item",
            "info",
            "This item has no take/source — it is a silent placeholder until you add media.");
    } else if (TakeIsMIDI(take) && t && TrackFX_GetInstrument(t) < 0) {
        add(h,
            "midi_no_instrument",
            "warn",
            "This is a MIDI item on a track with no virtual instrument — it will be silent "
            "until you add an instrument FX.");
    }
    return h;
}

}  // namespace ReaClaw::Hints

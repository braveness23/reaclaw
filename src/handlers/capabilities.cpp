#include "handlers/capabilities.h"

#include "app.h"
#include "handlers/common.h"

#include <httplib.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include <reaper_plugin_functions.h>

#include <json.hpp>

namespace ReaClaw::Handlers {

namespace {

// Cheap PATH scan — no subprocess. Used for optional-tool feature detection so a
// /capabilities call stays fork-free.
bool binary_on_path(const char* name) {
    const char* path = std::getenv("PATH");
    if (!path)
        return false;
    std::string p(path);
    size_t start = 0;
    while (start <= p.size()) {
        size_t sep = p.find(':', start);
        std::string dir = p.substr(start,
                                   sep == std::string::npos ? std::string::npos : sep - start);
        if (!dir.empty()) {
            std::error_code ec;
            if (std::filesystem::exists(std::filesystem::path(dir) / name, ec))
                return true;
        }
        if (sep == std::string::npos)
            break;
        start = sep + 1;
    }
    return false;
}

}  // namespace

// GET /capabilities
//
// Tells an agent, at a glance, what ReaClaw supports directly (structured verbs)
// versus what it must reach through the action catalog or a generated Lua script.
// Reflects the tiered coverage decision (see ReaClaw_TECH_DECISIONS.md): typed
// verbs for the common objects, action-runner + Lua escape hatch for the rest.
void handle_capabilities(const httplib::Request& req, httplib::Response& res) {
    (void)req;

    nlohmann::json direct = {
            {"tracks",
             {{"create",
               "POST /state/tracks {create:[{name,color,folder_depth,volume_db,pan,"
               "muted,soloed,armed}]}"},
              {"update",
               "POST /state/tracks/{index}  (same fields) or batch via "
               "POST /state/tracks {update:[{index,...}]}"},
              {"delete", "DELETE /state/tracks/{index}"},
              {"writable_fields",
               nlohmann::json::array({"name",
                                      "color",
                                      "folder_depth",
                                      "volume_db",
                                      "pan",
                                      "muted",
                                      "soloed",
                                      "armed",
                                      "phase",
                                      "n_channels",
                                      "pan_mode",
                                      "dual_pan_l",
                                      "dual_pan_r",
                                      "rec_input",
                                      "midi_hw_out",
                                      "main_send",
                                      "icon"})},
              {"icon",
               "relative name (e.g. 'bass.png') resolved against Data/track_icons, "
               "or an absolute path. null/\"\" clears it. "
               "GET /state/track-icons lists available names"}}},
            {"items",
             {{"read", "GET /state/items  |  GET /state/items/{index}"},
              {"create",
               "POST /state/items {create:[{track,position,length?,file?}]}  "
               "(file loads an audio/MIDI source; length defaults to the source length)"},
              {"update",
               "POST /state/items/{index}  or batch POST /state/items {update:[{index,...}]}; "
               "fields: position,length,track(move),selected,muted,volume_db,fade_in,fade_out,"
               "take:{name,volume_db,pan,pitch,playrate,preserve_pitch}"},
              {"split", "POST /state/items/{index}/split {position}"},
              {"delete", "DELETE /state/items/{index}"},
              {"source",
               "reads expose source{file,type,length,sample_rate,num_channels} for the active "
               "take"}}},
            {"midi",
             {{"read",
               "GET /state/items/{index}/midi — notes[] + cc[] from the active MIDI take; "
               "each note has pitch,note(name),channel,velocity,start_ppq,end_ppq,"
               "start_time,end_time,selected,muted; each cc has number,value,channel,"
               "chanmsg,ppq,time,selected,muted"},
              {"write",
               "POST /state/items/{index}/midi {notes:[{pitch,channel?,velocity?,"
               "start_ppq,end_ppq}|{...start_time,end_time}], "
               "cc:[{number,value,channel?,ppq}|{...time}], replace:false} — "
               "append (default) or replace all MIDI content; wrapped in undo block"},
              {"note",
               "requires the item's active take to be a MIDI source; "
               "non-MIDI items return 400 BAD_REQUEST"}}},
            {"fx",
             {{"add", "POST /state/tracks/{index}/fx {name,enabled,params}"},
              {"read", "GET /state/tracks/{index}/fx/{slot}"},
              {"set",
               "POST /state/tracks/{index}/fx/{slot} "
               "{enabled,offline,params:[{index|name,value}]}"},
              {"delete", "DELETE /state/tracks/{index}/fx/{slot}"},
              {"copy",
               "POST /state/tracks/{index}/fx/{slot}/copy {to_track,to_slot:-1,move:false}"},
              {"preset", "GET/POST /state/tracks/{index}/fx/{slot}/preset {name|navigate:-1|1}"},
              {"params",
               "normalized 0..1 by param index or name; reads also expose real-unit "
               "min/max/mid/raw and offline state"}}},
            {"routing",
             {{"add_send", "POST /state/tracks/{index}/sends {to_track,volume_db,pan}"},
              {"set_send",
               "POST /state/tracks/{index}/sends/{send} "
               "{volume_db,pan,muted,phase,mono,mode}"},
              {"delete_send", "DELETE /state/tracks/{index}/sends/{send}"},
              {"read", "sends[] (incl. muted/phase/mono/mode) in GET /state/tracks"}}},
            {"automation",
             {{"read", "GET /state/automation  (selected track)"},
              {"write",
               "POST /state/tracks/{index}/automation "
               "{envelope,points:[{time,value,shape,tension}],clear_range:[s,e]}"}}},
            {"markers",
             {{"read", "GET /state/markers"},
              {"add", "POST /state/markers {position,name,is_region,region_end,color,id}"},
              {"delete", "DELETE /state/markers/{id}?is_region=true|false"}}},
            {"tempo",
             {{"read", "GET /state/tempo"},
              {"add", "POST /state/tempo {time,bpm,timesig_num,timesig_denom,linear}"}}},
            {"time", {{"convert", "GET /time?time=SEC  or  GET /time?beats=B[&measure=M]"}}},
            {"selection",
             {{"set",
               "POST /state/selection {tracks:[i,...]|\"all\"|\"none\", "
               "items:[j,...]|\"all\"|\"none\"}"}}},
            {"perception",
             {{"analyze_item", "GET /analysis/item/{index}?measures=loudness,spectral&start=&end="},
              {"analyze_file", "GET /analysis/file?path=ABS&measures=&start=&end="},
              {"measures",
               "loudness: lufs_i/rms_i/peak_db/true_peak_db (exact offline analysis) + "
               "clipping; spectral: low/mid/high band energy + centroid_hz (estimated DSP)"},
              {"meters", "GET /state/meters — live per-track + master peak/peak-hold (dBFS)"},
              {"visualize",
               "GET /analysis/item/{index}/visualize and /analysis/file/visualize"
               "?type=spectrum|waveform|loudness&width=&height=&image=png|none — "
               "returns a machine-readable digest + base64 PNG"},
              {"probe",
               "GET /analysis/item/{index}/probe and /analysis/file/probe"
               "?probes=key,pitch,tempo — musical-attribute probes. pitch/key are "
               "built-in DSP (estimated); tempo is exact project introspection plus an "
               "optional external detector (graceful when absent)"},
              {"screenshot",
               "GET /screenshot?target=screen|arrange|mixer|fxchain|midi|routing|master|"
               "transport|explorer & window=<title> & region=x,y,w,h & width= — on-demand "
               "PNG capture of a named surface (Linux/X11; needs ffmpeg, xdotool for "
               "framing). Fallback for GUI-only state; prefer structured /state reads"},
              {"tagging",
               "every measure carries method (offline_analysis|estimated_dsp|introspection|"
               "derived) + confidence so the agent knows how much to trust it"},
              {"hints",
               "mutating responses to track/FX/send/item edits carry a hints[] array of "
               "consequence-aware warnings ({code,severity,message})"}}},
            {"snapshot",
             {{"capture", "POST /snapshot {label?} — store a canonical project-state snapshot"},
              {"list", "GET /snapshot"},
              {"get", "GET /snapshot/{id}"},
              {"diff",
               "GET /snapshot/diff?from=<id>&to=<id|current> — flat list of {path,op,from,to} "
               "changes (to defaults to a live capture). Backs the #19 A/B diff too"},
              {"delete", "DELETE /snapshot/{id}"}}},
            {"learning",
             {{"suggestions",
               "GET /suggestions?after=&agent=&limit= — learned 'after X, agents usually do Y' "
               "suggestions tagged method:learned + confidence"},
              {"stats", "GET /learn/stats — events/patterns accumulated locally"},
              {"note",
               "local-first and OPT-IN: off unless learning.enabled=true; mines only this "
               "machine's edit history, never phones home"}}},
            {"render",
             {{"render",
               "POST /render {output, format?, bit_depth?, srate?, channels?, bounds?, "
               "start?, end?, mp3_bitrate?, flac_compression?} — offline render to file. "
               "Formats: wav (default), flac, mp3, ogg. Bounds: project (default), "
               "time_selection, all_regions, custom (requires start+end). "
               "Returns output_path, render_seconds, project_length, offline_ratio. "
               "Timeout: 300 s (covers projects up to ~100 min at 20× offline speed)."},
              {"note",
               "Render settings are saved and restored after each call so agent renders "
               "do not permanently change the project's render configuration."}}},
            {"project",
             {{"read", "GET /project  (dirty, length, notes)"},
              {"set_notes", "POST /project/notes {notes}"},
              {"ext_state",
               "GET/POST/DELETE /project/extstate {section,key,value} — persistent per-project "
               "scratchpad stored in the .rpp (survives close/reopen)"},
              {"new",
               "POST /project/new {discard_changes?:false} — open a blank project from the "
               "default template; returns 409 if unsaved changes exist and discard_changes is "
               "not true"},
              {"open",
               "POST /project/open {path, discard_changes?:false} — replace current project "
               "with a .rpp file; returns 409 on dirty project (without discard_changes:true) "
               "and 400 if the file does not exist. Tab mode is not supported."},
              {"save",
               "POST /project/save {path?} — save to path (sets new project filename) or "
               "in-place if path is omitted (400 if project has never been saved)"},
              {"reset",
               "POST /project/reset {discard_changes?:false} — blank the current project "
               "in-place: deletes all tracks/items/envelopes, markers, extra tempo markers "
               "(resets to 120 BPM 4/4), clears time selection/loop, cursor to 0, clears "
               "project notes. Returns 409 on unsaved changes without discard_changes:true. "
               "Deterministic on a headless display — no GUI modal."}}},
            {"undo",
             {{"state", "GET /undo  (can_undo, can_redo descriptions)"},
              {"undo", "POST /undo"},
              {"redo", "POST /redo"},
              {"note",
               "all structured mutations are wrapped in undo blocks — they land as "
               "single, user-undoable steps"}}},
            {"read",
             nlohmann::json::array({"GET /state",
                                    "GET /state/tracks",
                                    "GET /state/items",
                                    "GET /state/selection",
                                    "GET /state/automation",
                                    "GET /state/markers",
                                    "GET /state/tempo",
                                    "GET /state/meters",
                                    "GET /state/track-icons",
                                    "GET /project"})},
            {"actions",
             {{"run", "POST /execute/action {id}"},
              {"sequence", "POST /execute/sequence {steps:[...]}"},
              {"search", "GET /catalog/search?q=  (add &section=midi_editor for MIDI actions)"},
              {"note", "search the catalog before generating a script"}}},
            {"chunk",
             {{"read", "GET /state/chunk?target=track|item|envelope&index=N[&envelope=M]"},
              {"write", "POST /state/chunk {target,index,envelope?,chunk}"},
              {"note",
               "universal backstop: full RPP state of any track/item/envelope, readable and "
               "writable even with no dedicated verb. Writes are undo-wrapped"}}},
            {"transport",
             {{"action",
               "POST /transport {action: play|stop|pause|record} — "
               "backed by CSurf_OnPlay/Stop/Pause/Record; returns transport state after dispatch"},
              {"cursor",
               "POST /transport/cursor {position, moveview?:false, seekplay?:false} — "
               "moves edit cursor to position (seconds); returns actual cursor position"},
              {"loop",
               "POST /transport/loop {start?, end?, enabled?} — "
               "set loop range and/or toggle repeat; all fields optional; "
               "returns {start, end, enabled}"}}},
            {"scripts",
             {{"register", "POST /scripts/register  (Lua escape hatch for anything not above)"}}}};

    // Things that have no direct verb yet — reach them via an action ID or a
    // generated Lua script. Kept honest so the agent doesn't probe blindly.
    nlohmann::json via_script_or_action = nlohmann::json::array(
            {"MIDI notes/events", "freezing tracks"});

    // Coverage matrix — every REST-relevant REAPER domain and how it is reached, so an
    // agent (and a human) can see the whole map and know nothing is hidden. Statuses:
    //   structured  — first-class typed verb(s)
    //   chunk       — reachable via the universal /state/chunk backstop (issue #48)
    //   action      — reachable via POST /execute/action only (no typed verb yet)
    //   lua         — reachable via the /scripts/register Lua escape hatch only
    //   out_of_scope— deliberately not exposed (see note)
    // Verdicts mirror ReaClaw_COVERAGE_REPORT.md §4.
    auto dom = [](const char* status, const char* note) {
        return nlohmann::json{{"status", status}, {"note", note}};
    };
    nlohmann::json coverage = {
            {"tracks", dom("structured", "create/update/delete + 17 writable fields")},
            {"items_takes", dom("structured", "item + take CRUD, sources; take-FX pending #50")},
            {"fx", dom("structured", "track FX full; take-FX pending #50")},
            {"routing", dom("structured", "sends add/set/delete; HW-out (cat -1) via chunk/lua")},
            {"automation", dom("structured", "envelope read/write; automation items via chunk")},
            {"markers_regions", dom("structured", "read/add/delete")},
            {"tempo_time", dom("structured", "tempo/time-sig map + beat<->sec conversion")},
            {"selection", dom("structured", "tracks/items set incl. all/none")},
            {"project", dom("structured", "read/notes/ext-state + lifecycle: new/open/save/reset")},
            {"undo", dom("structured", "state/undo/redo; all mutations are undo-wrapped")},
            {"perception",
             dom("structured", "loudness/spectral/meters/visualize/probe/screenshot")},
            {"snapshot", dom("structured", "capture/list/get/diff/delete")},
            {"learning", dom("structured", "suggestions/stats (local-first, opt-in)")},
            {"render", dom("structured", "offline render; async jobs pending #35")},
            {"transport",
             dom("structured",
                 "POST /transport {action:play|stop|pause|record}, "
                 "POST /transport/cursor {position}, POST /transport/loop {start,end,enabled}")},
            {"config_vars", dom("action", "reaper.ini/config vars; typed endpoint pending #44")},
            {"midi", dom("structured", "notes + CC read/write via GET/POST /state/items/{i}/midi")},
            {"object_state_chunk",
             dom("structured",
                 "GET/POST /state/chunk — full RPP state of any track/item/envelope "
                 "(the universal reachability backstop)")},
            {"control_surface",
             dom("out_of_scope", "inverted model (REAPER calls you); not REST-mappable")},
            {"pcm_vst_interfaces",
             dom("out_of_scope", "C++ interfaces you implement; rendering covered by /render")}};

    // Honest SDK summary (reproducible — see ReaClaw_COVERAGE_REPORT.md §1).
    nlohmann::json sdk = {
            {"functions_total", 865},
            {"functions_called", 131},
            {"raw_pct", 15.1},
            {"reachable", "100% via verbs + actions (~6700) + Lua + chunk backstop"},
            {"note",
             "raw % understates coverage: ~16 of ~20 REST-relevant domains have typed verbs; "
             "most uncalled functions are out-of-scope, redundant variants, or "
             "action/Lua-reachable"}};

    // Optional-dependency / feature detection (folded from #37) so agents branch instead of
    // probe-and-fail. SWS via plugin_getapi; tools via a cheap PATH scan.
    bool sws = plugin_getapi && plugin_getapi("CF_GetSWSVersion") != nullptr;
    nlohmann::json features = {{"sws", sws},
                               {"sws_r128_loudness", sws},  // R128 API ships with SWS
                               {"ffmpeg", binary_on_path("ffmpeg")},
                               {"xdotool", binary_on_path("xdotool")},
                               {"key_tempo_detector", binary_on_path("bpm-tag")}};

    json_ok(res,
            {{"coverage_model",
              "tiered: structured verbs for common objects, "
              "action-runner + Lua escape hatch for the long tail"},
             {"version", REACLAW_VERSION},
             {"direct", direct},
             {"via_script_or_action", via_script_or_action},
             {"coverage", coverage},
             {"sdk", sdk},
             {"features", features}});
}

}  // namespace ReaClaw::Handlers

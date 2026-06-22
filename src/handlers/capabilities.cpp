#include "handlers/capabilities.h"

#include "app.h"
#include "handlers/common.h"

#include <httplib.h>

#include <json.hpp>

namespace ReaClaw::Handlers {

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
                                      "main_send"})}}},
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
             {{"analyze_item",
               "GET /analysis/item/{index}?measures=loudness,spectral&start=&end="},
              {"analyze_file", "GET /analysis/file?path=ABS&measures=&start=&end="},
              {"measures",
               "loudness: lufs_i/rms_i/peak_db/true_peak_db (exact offline analysis) + "
               "clipping; spectral: low/mid/high band energy + centroid_hz (estimated DSP)"},
              {"meters", "GET /state/meters — live per-track + master peak/peak-hold (dBFS)"},
              {"tagging",
               "every measure carries method (offline_analysis|estimated_dsp|introspection|"
               "derived) + confidence so the agent knows how much to trust it"},
              {"hints",
               "mutating responses to track/FX/send/item edits carry a hints[] array of "
               "consequence-aware warnings ({code,severity,message})"}}},
            {"project",
             {{"read", "GET /project  (dirty, length, notes)"},
              {"set_notes", "POST /project/notes {notes}"},
              {"ext_state",
               "GET/POST/DELETE /project/extstate {section,key,value} — persistent per-project "
               "scratchpad stored in the .rpp (survives close/reopen)"}}},
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
                                    "GET /project"})},
            {"actions",
             {{"run", "POST /execute/action {id}"},
              {"sequence", "POST /execute/sequence {steps:[...]}"},
              {"search", "GET /catalog/search?q=  (add &section=midi_editor for MIDI actions)"},
              {"note", "search the catalog before generating a script"}}},
            {"scripts",
             {{"register", "POST /scripts/register  (Lua escape hatch for anything not above)"}}}};

    // Things that have no direct verb yet — reach them via an action ID or a
    // generated Lua script. Kept honest so the agent doesn't probe blindly.
    nlohmann::json via_script_or_action = nlohmann::json::array(
            {"take FX chains (TakeFX_*)",
             "MIDI notes/events",
             "rendering / freezing",
             "project open / save"});

    json_ok(res,
            {{"coverage_model",
              "tiered: structured verbs for common objects, "
              "action-runner + Lua escape hatch for the long tail"},
             {"version", REACLAW_VERSION},
             {"direct", direct},
             {"via_script_or_action", via_script_or_action}});
}

}  // namespace ReaClaw::Handlers

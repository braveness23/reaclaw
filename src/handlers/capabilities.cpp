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
             {{"create", "POST /state/tracks {create:[{name,color,folder_depth,volume_db,pan,"
                         "muted,soloed,armed}]}"},
              {"update", "POST /state/tracks/{index}  (same fields) or batch via "
                         "POST /state/tracks {update:[{index,...}]}"},
              {"delete", "DELETE /state/tracks/{index}"},
              {"writable_fields",
               nlohmann::json::array({"name", "color", "folder_depth", "volume_db", "pan", "muted",
                                      "soloed", "armed"})}}},
            {"fx",
             {{"add", "POST /state/tracks/{index}/fx {name,enabled,params}"},
              {"read", "GET /state/tracks/{index}/fx/{slot}"},
              {"set", "POST /state/tracks/{index}/fx/{slot} {enabled,params:[{index|name,value}]}"},
              {"delete", "DELETE /state/tracks/{index}/fx/{slot}"},
              {"params", "normalized 0..1 by param index or name"}}},
            {"routing",
             {{"add_send", "POST /state/tracks/{index}/sends {to_track,volume_db,pan}"},
              {"delete_send", "DELETE /state/tracks/{index}/sends/{send}"},
              {"read", "sends[] in GET /state/tracks"}}},
            {"selection", {{"set", "POST /state/selection {tracks:[i,...]|\"all\"|\"none\"}"}}},
            {"read",
             nlohmann::json::array({"GET /state", "GET /state/tracks", "GET /state/items",
                                    "GET /state/selection", "GET /state/automation"})},
            {"actions",
             {{"run", "POST /execute/action {id}"},
              {"sequence", "POST /execute/sequence {steps:[...]}"},
              {"search", "GET /catalog/search?q="},
              {"note", "search the catalog before generating a script"}}},
            {"scripts",
             {{"register", "POST /scripts/register  (Lua escape hatch for anything not above)"}}}};

    // Things that have no direct verb yet — reach them via an action ID or a
    // generated Lua script. Kept honest so the agent doesn't probe blindly.
    nlohmann::json via_script_or_action = nlohmann::json::array(
            {"media items / takes (create, move, trim, fades)", "MIDI notes/events",
             "markers & regions", "tempo / time-signature map edits", "envelope point writes",
             "rendering / freezing", "project open / save"});

    json_ok(res,
            {{"coverage_model", "tiered: structured verbs for common objects, "
                                "action-runner + Lua escape hatch for the long tail"},
             {"version", REACLAW_VERSION},
             {"direct", direct},
             {"via_script_or_action", via_script_or_action}});
}

}  // namespace ReaClaw::Handlers

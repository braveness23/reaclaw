#include "handlers/recipes.h"

#include "handlers/common.h"

#include <httplib.h>

#include <string>
#include <vector>

#include <json.hpp>

namespace ReaClaw::Handlers {

namespace {

struct Step {
    std::string description;
    std::string method;   // "GET" | "POST" | "DELETE" — empty for a shell step
    std::string path;     // e.g. "/state/tracks" — empty for a shell step
    std::string body;     // JSON body as a string literal, empty if none
    std::string command;  // shell command — set instead of method/path
};

struct Recipe {
    std::string id;
    std::string title;
    std::string description;
    std::vector<Step> steps;
    std::string notes;
};

nlohmann::json step_to_json(const Step& s) {
    nlohmann::json j = {{"description", s.description}};
    if (!s.command.empty()) {
        j["command"] = s.command;
    } else {
        j["method"] = s.method;
        j["path"] = s.path;
        if (!s.body.empty())
            j["body"] = nlohmann::json::parse(s.body, nullptr, false);
    }
    return j;
}

nlohmann::json recipe_to_json(const Recipe& r) {
    nlohmann::json steps = nlohmann::json::array();
    for (const auto& s : r.steps)
        steps.push_back(step_to_json(s));
    nlohmann::json j = {
            {"id", r.id}, {"title", r.title}, {"description", r.description}, {"steps", steps}};
    if (!r.notes.empty())
        j["notes"] = r.notes;
    return j;
}

// Curated set — mirrors skill/reaclaw/SKILL.md. Keep in sync by hand when
// either changes (same convention as catalog.cpp's DON'T-list mirror).
const std::vector<Recipe>& recipes() {
    static const std::vector<Recipe> r = {
            {"folder_group_session",
             "Build a colored folder group in one call",
             "Create a folder parent + children with names, colors, and a mix — one "
             "structured-verb call, no per-track round trips.",
             {{"Create the folder: parent (folder_depth=1), children, last child closes "
               "the folder (folder_depth=-1)",
               "POST",
               "/state/tracks",
               R"({"create":[
  {"name":"DRUMS","color":"#CC3333","folder_depth":1},
  {"name":"Kick","color":"#33AA55","volume_db":-3.0,"armed":true},
  {"name":"Snare","color":"#3366CC","folder_depth":-1,"pan":-0.2}
]})",
               ""}},
             "folder_depth: 1 = folder parent, 0 = normal, negative = closes that many "
             "levels (put it on the last child)."},

            {"search_then_run_action",
             "Search the action catalog, then run what you find",
             "For anything without a structured verb: search first (don't guess IDs), "
             "then execute. Prefer structured verbs when one exists — actions can depend "
             "on selection state.",
             {{"Search for a matching action", "GET", "/catalog/search?q=mute%20drums", "", ""},
              {"Run the action you picked from the results",
               "POST",
               "/execute/action",
               R"({"id":40702})",
               ""}},
             "Response includes action_name for confirmation. Try semantic=true if "
             "keyword search misses (needs semantic_search enabled in config.json)."},

            {"lua_register_then_run",
             "Register a Lua script, then run it by id",
             "For media items, MIDI notes, markers/regions, tempo map, envelopes, or "
             "render — anything neither a structured verb nor an action covers.",
             {{"Register the script (syntax-validated, not sandboxed — you're trusted)",
               "POST",
               "/scripts/register",
               R"({"name":"my_script","script":"-- Lua using reaper.* API"})",
               ""},
              {"Run it by the returned action id",
               "POST",
               "/execute/action",
               R"({"id":"_my_script_<hash>"})",
               ""}},
             "Register once, reuse by id — no need to re-register on every call."},

            {"add_fx_and_tune",
             "Add a named effect and set a parameter",
             "Add FX by name (resolves ReaEQ/ReaComp/ReaGate shorthand or a full VST "
             "name), then read the slot to learn param names before writing.",
             {{"Add the effect", "POST", "/state/tracks/0/fx", R"({"name":"ReaComp"})", ""},
              {"Read the slot to see param names + current values",
               "GET",
               "/state/tracks/0/fx/0",
               "",
               ""},
              {"Set a parameter (normalized 0..1, addressed by name or index)",
               "POST",
               "/state/tracks/0/fx/0",
               R"({"params":[{"name":"Threshold","value":0.4}]})",
               ""}},
             "If add returns 400 'FX not found', the plugin isn't installed on this "
             "machine."},

            {"add_send_routing",
             "Route one track's output to another",
             "Add a send from a source track to a destination track with its own "
             "volume/pan.",
             {{"Add the send",
               "POST",
               "/state/tracks/0/sends",
               R"({"to_track":2,"volume_db":-6.0,"pan":0.0})",
               ""}},
             ""},

            {"screenshot_verify",
             "Capture the live REAPER window and read it yourself",
             "Structure-first is the default (GET /state/tracks covers almost "
             "everything) — use this only for inherently visual verification (themes, "
             "layout) or when the user asks 'show me'.",
             {{"Grab the live X11 framebuffer (not xwd — it returns blank client areas "
               "for REAPER's SWELL windows)",
               "",
               "",
               "",
               "ffmpeg -y -f x11grab -i \"$DISPLAY\" -frames:v 1 /tmp/reaper.png"},
              {"Optionally crop to a region (mixer = bottom strip, arrange = top)",
               "",
               "",
               "",
               "ffmpeg -y -i /tmp/reaper.png -vf \"crop=1920:230:0:850\" /tmp/mixer.png"}},
             "Then Read the PNG yourself. Wayland: use grim instead of ffmpeg x11grab."},
    };
    return r;
}

}  // namespace

// GET /recipes
void handle_recipes_list(const httplib::Request&, httplib::Response& res) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : recipes())
        arr.push_back(recipe_to_json(r));
    json_ok(res, {{"recipes", arr}});
}

// GET /recipes/{id}
void handle_recipes_get(const httplib::Request& req, httplib::Response& res) {
    auto it = req.path_params.find("id");
    if (it == req.path_params.end()) {
        json_error(res, 400, "Missing recipe id", "BAD_REQUEST");
        return;
    }
    for (const auto& r : recipes()) {
        if (r.id == it->second) {
            json_ok(res, recipe_to_json(r));
            return;
        }
    }
    json_error(res, 404, "No recipe with id " + it->second, "NOT_FOUND");
}

}  // namespace ReaClaw::Handlers

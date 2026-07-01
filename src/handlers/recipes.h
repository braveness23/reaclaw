#pragma once

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib

namespace ReaClaw::Handlers {

// Issue #10 (magic wand, layer 3) — the vetted recipes curated in
// skill/reaclaw/SKILL.md, exposed as structured JSON so a plain REST/MCP
// client without the Skill loaded still gets them. A static, curated set —
// keep in sync with the Skill by hand when either changes (same convention
// catalog.cpp's modal-action DON'T list already uses for its own mirror of
// the Skill).
//
//   GET /recipes       -> { recipes: [ {id, title, ...}, ... ] }
//   GET /recipes/{id}  -> { id, title, ... }
void handle_recipes_list(const httplib::Request& req, httplib::Response& res);
void handle_recipes_get(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

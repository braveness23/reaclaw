#pragma once

namespace httplib { struct Request; struct Response; }

namespace ReaClaw::Handlers {

// Phase 1 — not implemented in Phase 0.
void handle_scripts_register(const httplib::Request& req, httplib::Response& res);
void handle_scripts_cache(const httplib::Request& req, httplib::Response& res);
void handle_scripts_get(const httplib::Request& req, httplib::Response& res);
void handle_scripts_delete(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

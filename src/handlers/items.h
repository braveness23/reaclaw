#pragma once

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib

namespace ReaClaw::Handlers {

// Epic #17 — Tier-B content manipulation: media items, takes, sources.
// Items are addressed by project-wide index (the order GET /state/items
// returns). Mutating verbs that change the item set (create/split/delete) can
// shift those indices, so callers should re-read after a structural change.
void handle_items_get(const httplib::Request& req, httplib::Response& res);
void handle_item_get(const httplib::Request& req, httplib::Response& res);
void handle_items_post(const httplib::Request& req, httplib::Response& res);
void handle_item_update(const httplib::Request& req, httplib::Response& res);
void handle_item_split(const httplib::Request& req, httplib::Response& res);
void handle_item_delete(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

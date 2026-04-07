#pragma once

namespace httplib { struct Request; struct Response; }

namespace ReaClaw::Handlers {

void handle_catalog_list(const httplib::Request& req, httplib::Response& res);
void handle_catalog_search(const httplib::Request& req, httplib::Response& res);
void handle_catalog_by_id(const httplib::Request& req, httplib::Response& res);
void handle_catalog_categories(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

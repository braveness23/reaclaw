#pragma once

namespace httplib { struct Request; struct Response; }
namespace ReaClaw { struct Config; }

namespace ReaClaw::Auth {

// Returns true if the request satisfies the auth policy in cfg.
bool check(const Config& cfg, const httplib::Request& req);

// Set res to 401 Unauthorized with a standard JSON error body.
void reject(httplib::Response& res);

}  // namespace ReaClaw::Auth

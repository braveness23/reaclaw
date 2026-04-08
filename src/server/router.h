#pragma once

namespace httplib {
class SSLServer;
}
namespace ReaClaw {
struct Config;
}

namespace ReaClaw::Router {

// Register all API routes on the given server.
// Auth middleware is applied here via per-route wrappers.
void register_routes(httplib::SSLServer& svr, const Config& cfg);

}  // namespace ReaClaw::Router

#pragma once

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib
namespace ReaClaw {
struct Config;
}

namespace ReaClaw::Auth {

// Returns true if the request satisfies the auth policy in cfg.
bool check(const Config& cfg, const httplib::Request& req);

// Same policy as check(), plus a `?token=<key>` query-param fallback — used
// only by GET /stream/video and GET /stream/audio (router.cpp's
// auth_wrap_stream), since browsers/media players opening a stream URL
// directly (<img>, <audio> tags) can't set a custom Authorization header.
// Narrow and explicit rather than widening check() itself: see
// ReaClaw_TECH_DECISIONS.md for why this isn't applied to the other ~80
// routes (stream URLs carrying the token end up in proxy logs/browser
// history — rotate auth_key if one ever leaks).
bool check_stream(const Config& cfg, const httplib::Request& req);

// Set res to 401 Unauthorized with a standard JSON error body.
void reject(httplib::Response& res);

}  // namespace ReaClaw::Auth

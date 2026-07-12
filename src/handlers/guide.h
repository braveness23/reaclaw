#pragma once

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib

namespace ReaClaw::Handlers {

// GET /agent/guide — the vendor-neutral agent onboarding manual
// (docs/AGENT_GUIDE.md), embedded at build time so the served copy always
// matches the running binary. Any AI harness can fetch it once and
// self-configure (connection, latency contract, sync protocol, cheat sheet,
// traps). `?format=json` wraps it with the server version and entry-point
// links for callers that want structure instead of raw markdown.
void handle_agent_guide(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

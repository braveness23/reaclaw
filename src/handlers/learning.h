#pragma once

#include <string>

#include <json.hpp>

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib

namespace ReaClaw::Learning {

// Epic #20 — learned suggestions: the compounding moat.
//
// Locally and opt-in, observe the sequence of structured edits the agent makes
// and mine "after X, agents usually do Y" associations from that history. Surface
// them as suggestions tagged `method:"learned"` with a `confidence`. This is
// distinct from the hand-authored hints of Epic #18 though it shares the
// suggestion channel: hints are fixed invariants, these are *learned* from use.
//
// **Local-first, opt-in, never phones home.** All state lives in the same local
// SQLite DB; there is no network egress. Disabled by default
// (`learning.enabled`); when off, note() is a no-op and nothing is recorded.

// Is learning enabled in config?
bool enabled();

// Record one structured edit by `agent` (e.g. "track.create", "track.set:armed",
// "fx.add", "send.add"). No-op when learning is disabled. When the previous edit
// by the same agent falls within the correction window, the transition
// (prev -> action) is counted as an observed pattern.
void note(const std::string& agent, const std::string& action);

// Convenience: note one event per writable field present in a track-edit body
// (so "after creating a track, agents set its color" etc. is learnable).
void note_track_fields(const std::string& agent, const nlohmann::json& body);

// Learned suggestions for what usually follows `after` (or the agent's most
// recent edit when `after` is empty). Returns a JSON array of
// {after, suggest, support, confidence, method:"learned"}; empty when there is
// no confident pattern. Caller decides how to surface them.
nlohmann::json suggestions(const std::string& agent, const std::string& after, int limit);

// Observability: how much has been learned so far (events, patterns, agents).
nlohmann::json stats();

// GET /suggestions?after=&agent=&limit=  — opt-in learned suggestions.
void handle_suggestions(const httplib::Request& req, httplib::Response& res);
// GET /learn/stats — what the learner has accumulated locally.
void handle_learn_stats(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Learning

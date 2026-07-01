#pragma once

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib

namespace ReaClaw::Handlers {

// Epic #32 / issue #33 — first-class offline render to audio file.
//
// POST /render  {output, format, bit_depth, srate, channels, bounds,
//                start, end, normalize?, async?}
//
// Default (no `async`, or `async: false`) is unchanged: blocks the calling
// HTTP thread until the render finishes (up to 300s).
//
// Issue #35 — async render-job model. `async: true` returns
// {job_id, status: "queued"} immediately instead of blocking; the render
// itself still runs via the same Executor::post path, just awaited from a
// detached worker thread instead of the HTTP thread. See
// ReaClaw_TECH_DECISIONS.md for why this does NOT make other API calls stay
// responsive during the render (REAPER's main thread has no message pump
// during an offline render — confirmed live).
void handle_render(const httplib::Request& req, httplib::Response& res);

// GET /render/jobs/{id} — poll a single job's status/result.
void handle_render_job_get(const httplib::Request& req, httplib::Response& res);

// GET /render/jobs — list all tracked jobs (bounded, oldest terminal jobs evicted).
void handle_render_jobs_list(const httplib::Request& req, httplib::Response& res);

// DELETE /render/jobs/{id} — cancel a job. Only a job that hasn't started
// running yet can be cancelled cleanly; a job already mid-render returns 409
// (no safe SDK abort exists for an in-flight offline render).
void handle_render_job_cancel(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

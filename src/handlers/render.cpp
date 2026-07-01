#include "handlers/render.h"

#include "handlers/common.h"
#include "reaper/executor.h"
#include "util/image.h"
#include "util/logging.h"

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <reaper_plugin_functions.h>

#include <json.hpp>

namespace ReaClaw::Handlers {

namespace {

// ---------------------------------------------------------------------------
// RENDER_FORMAT blob builder
//
// REAPER stores codec settings for the render output as a base64-encoded
// binary blob under the "RENDER_FORMAT" project info key. The blob is
// codec-specific and starts with the sink's FourCC tag (e.g. "evaw" for WAV),
// NOT a simple numeric discriminator.
//
// For WAV: we return an empty string so REAPER keeps/uses its own default WAV
// format (from [wave sink defaults] in reaper.ini). Attempting to hand-craft
// the WAV blob with a 0x00000000 discriminator causes an "Invalid render
// format" error because REAPER's PCM sink expects the "evaw" FourCC prefix.
//
// For FLAC / MP3 / OGG: the blob layout below has been observed to work for
// those codecs which DO use a numeric discriminator in the upper half of the
// first 4 bytes.
// ---------------------------------------------------------------------------

static std::string wav_render_format(int /*bit_depth*/) {
    // Return empty: let REAPER use its built-in default WAV format.
    // The project's RENDER_FORMAT is NOT overridden when this is empty.
    return "";
}

static std::string flac_render_format(int bit_depth, int compression) {
    std::vector<uint8_t> blob(8, 0);
    // [0..3]: type = 0x00010000 (little-endian)
    blob[2] = 0x01;
    blob[4] = static_cast<uint8_t>(bit_depth);
    blob[5] = static_cast<uint8_t>(compression);
    return Image::base64(blob);
}

static std::string mp3_render_format(int bitrate_kbps) {
    std::vector<uint8_t> blob(8, 0);
    // [0..3]: type = 0x00020000 (little-endian)
    blob[2] = 0x02;
    // [4]: mode = 0 (CBR)
    // [5..6]: bitrate as uint16_le
    blob[5] = static_cast<uint8_t>(bitrate_kbps & 0xFF);
    blob[6] = static_cast<uint8_t>((bitrate_kbps >> 8) & 0xFF);
    return Image::base64(blob);
}

static std::string ogg_render_format(int quality_x10) {
    std::vector<uint8_t> blob(8, 0);
    // [0..3]: type = 0x00030000 (little-endian)
    blob[2] = 0x03;
    // [4]: quality 0-10
    blob[4] = static_cast<uint8_t>(quality_x10);
    return Image::base64(blob);
}

// ---------------------------------------------------------------------------
// RENDER_BOUNDSFLAG values (REAPER API):
//   0 = custom time range
//   1 = entire project
//   2 = time selection
//   3 = all project regions/markers
// ---------------------------------------------------------------------------
static int bounds_flag(const std::string& bounds) {
    if (bounds == "time_selection")
        return 2;
    if (bounds == "all_regions")
        return 3;
    return 1;  // "project" or fallback → entire project
}

// Shared executor error → HTTP mapping.
static bool exec_error(httplib::Response& res, const nlohmann::json& result) {
    if (result.contains("_timeout")) {
        json_error(res, 408, "Render timed out on main thread", "TIMEOUT");
        return true;
    }
    if (result.contains("_error")) {
        json_error(res, 500, result["_error"].get<std::string>(), "INTERNAL_ERROR");
        return true;
    }
    if (result.contains("_bad_request")) {
        json_error(res, 400, result.value("_message", "Bad request"), "BAD_REQUEST");
        return true;
    }
    return false;
}

// Copy a std::string into a mutable char buffer for REAPER write API calls.
// GetSetProjectInfo_String takes char* (used bidirectionally: read buffer on get,
// source value on set). Use this to avoid casting away const.
static std::vector<char> to_mutable(const std::string& s) {
    std::vector<char> buf(s.begin(), s.end());
    buf.push_back('\0');
    return buf;
}

struct RenderParams {
    std::string render_dir;
    std::string render_pat;
    std::string fmt_blob;
    int bounds_flag_val;
    int srate;
    int channels;
    bool custom_bounds;
    double start_sec;
    double end_sec;
};

// The actual render, run on the main thread (call only from inside an
// Executor::post lambda). Shared by both the synchronous and async-job paths
// so there's exactly one place that knows how to save/apply/restore REAPER's
// render settings around action 41824.
nlohmann::json do_render(const RenderParams& params) {
    if (!GetSetProjectInfo || !GetSetProjectInfo_String || !Main_OnCommand || !GetProjectLength) {
        return {{"_error",
                 "Required REAPER API functions unavailable "
                 "(GetSetProjectInfo / Main_OnCommand / GetProjectLength)"}};
    }

    // Save current render settings so they can be restored afterwards.
    // Use large-enough fixed buffers — RENDER_FORMAT can be a long blob.
    std::vector<char> old_file(4096, 0);
    std::vector<char> old_pattern(1024, 0);
    std::vector<char> old_format(8192, 0);
    double old_boundsflag = 0, old_srate = 0, old_channels = 0;
    double old_loop_start = 0, old_loop_end = 0;

    GetSetProjectInfo_String(nullptr, "RENDER_FILE", old_file.data(), false);
    GetSetProjectInfo_String(nullptr, "RENDER_PATTERN", old_pattern.data(), false);
    GetSetProjectInfo_String(nullptr, "RENDER_FORMAT", old_format.data(), false);
    old_boundsflag = GetSetProjectInfo(nullptr, "RENDER_BOUNDSFLAG", 0, false);
    old_srate = GetSetProjectInfo(nullptr, "RENDER_SRATE", 0, false);
    old_channels = GetSetProjectInfo(nullptr, "RENDER_CHANNELS", 0, false);

    if (params.custom_bounds && GetSet_LoopTimeRange2) {
        GetSet_LoopTimeRange2(nullptr, false, false, &old_loop_start, &old_loop_end, false);
    }

    // Apply render settings.
    auto dir_buf = to_mutable(params.render_dir);
    auto pat_buf = to_mutable(params.render_pat);

    GetSetProjectInfo_String(nullptr, "RENDER_FILE", dir_buf.data(), true);
    GetSetProjectInfo_String(nullptr, "RENDER_PATTERN", pat_buf.data(), true);
    // Only override RENDER_FORMAT when we have a specific codec blob.
    // For WAV (empty blob) we leave the project's current format intact
    // so REAPER uses its built-in default rather than showing an error.
    if (!params.fmt_blob.empty()) {
        auto fmt_buf = to_mutable(params.fmt_blob);
        GetSetProjectInfo_String(nullptr, "RENDER_FORMAT", fmt_buf.data(), true);
    }
    GetSetProjectInfo(
            nullptr, "RENDER_BOUNDSFLAG", static_cast<double>(params.bounds_flag_val), true);
    GetSetProjectInfo(nullptr, "RENDER_SRATE", static_cast<double>(params.srate), true);
    GetSetProjectInfo(nullptr, "RENDER_CHANNELS", static_cast<double>(params.channels), true);

    // For custom bounds: set the project time selection and render
    // with RENDER_BOUNDSFLAG=2 (time selection in REAPER's API).
    if (params.custom_bounds && GetSet_LoopTimeRange2) {
        double s = params.start_sec;
        double e = params.end_sec;
        GetSet_LoopTimeRange2(nullptr, true, false, &s, &e, false);
        GetSetProjectInfo(nullptr, "RENDER_BOUNDSFLAG", 2.0, true);
    }

    double project_length = GetProjectLength(nullptr);

    // Action 41824 = "File: Render project, using the most recent
    // render settings". Runs synchronously on the main thread and blocks
    // until the render file is fully written. Confirmed live (issue #35):
    // this call pumps no message loop at all — REAPER's main thread is fully
    // unresponsive to everything else (including ReaClaw's own "timer"
    // plugin hook that drives Executor::tick()) for the render's entire
    // duration, regardless of which thread triggered it.
    Main_OnCommand(41824, 0);

    // Restore previous render settings.
    GetSetProjectInfo_String(nullptr, "RENDER_FILE", old_file.data(), true);
    GetSetProjectInfo_String(nullptr, "RENDER_PATTERN", old_pattern.data(), true);
    if (!params.fmt_blob.empty()) {
        GetSetProjectInfo_String(nullptr, "RENDER_FORMAT", old_format.data(), true);
    }
    GetSetProjectInfo(nullptr, "RENDER_BOUNDSFLAG", old_boundsflag, true);
    GetSetProjectInfo(nullptr, "RENDER_SRATE", old_srate, true);
    GetSetProjectInfo(nullptr, "RENDER_CHANNELS", old_channels, true);

    if (params.custom_bounds && GetSet_LoopTimeRange2) {
        GetSet_LoopTimeRange2(nullptr, true, false, &old_loop_start, &old_loop_end, false);
    }

    return {{"ok", true}, {"project_length", project_length}};
}

// ---------------------------------------------------------------------------
// Issue #35 — async render-job registry.
//
// A single global mutex guards a small bounded vector of jobs (matches
// Executor's own single-mutex style — job volume is low, single-user tool,
// no need for finer-grained locking). Jobs are in-memory only; they don't
// need to survive a ReaClaw/REAPER restart any more than Executor's own
// pending-command queue does.
// ---------------------------------------------------------------------------

struct RenderJob {
    std::string id;
    std::string status = "queued";  // queued | running | done | error | cancelled
    std::string output_path;
    std::string format;
    int srate = 0;
    int channels = 0;
    nlohmann::json warnings = nlohmann::json::array();
    double render_seconds = 0.0;
    double project_length = 0.0;
    double offline_ratio = 0.0;
    std::string error;
    std::string created_at;
    std::string started_at;
    std::string finished_at;
    std::chrono::steady_clock::time_point start_tp{};
    std::string agent_id;
};

std::mutex s_jobs_mutex;
std::vector<std::shared_ptr<RenderJob>> s_jobs;  // creation order
std::atomic<uint64_t> s_next_job_id{1};

constexpr size_t kMaxJobs = 200;

// Caller must hold s_jobs_mutex. Drops the oldest terminal (done/error/
// cancelled) jobs once the registry exceeds kMaxJobs — queued/running jobs
// are never evicted.
void evict_old_jobs_locked() {
    if (s_jobs.size() <= kMaxJobs)
        return;
    for (auto it = s_jobs.begin(); it != s_jobs.end() && s_jobs.size() > kMaxJobs;) {
        const auto& st = (*it)->status;
        if (st == "done" || st == "error" || st == "cancelled")
            it = s_jobs.erase(it);
        else
            ++it;
    }
}

// Caller must hold s_jobs_mutex.
std::shared_ptr<RenderJob> find_job_locked(const std::string& id) {
    for (auto& j : s_jobs) {
        if (j->id == id)
            return j;
    }
    return nullptr;
}

nlohmann::json job_to_json(const std::shared_ptr<RenderJob>& job) {
    nlohmann::json j = {{"job_id", job->id},
                        {"status", job->status},
                        {"output_path", job->output_path},
                        {"format", job->format},
                        {"srate", job->srate},
                        {"channels", job->channels},
                        {"created_at", job->created_at}};
    if (!job->warnings.empty())
        j["warnings"] = job->warnings;
    if (!job->started_at.empty())
        j["started_at"] = job->started_at;
    if (!job->finished_at.empty())
        j["finished_at"] = job->finished_at;
    if (job->status == "running") {
        j["elapsed_seconds"] = std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                             job->start_tp)
                                       .count();
    }
    if (job->status == "done") {
        j["render_seconds"] = job->render_seconds;
        j["project_length"] = job->project_length;
        j["offline_ratio"] = job->offline_ratio;
    }
    if (job->status == "error")
        j["error"] = job->error;
    return j;
}

// Runs the render on a detached worker thread so the HTTP thread returns
// immediately. The render itself still goes through the normal
// Executor::post path — this only moves the *waiting* off the HTTP thread;
// see do_render's comment for why that does not make REAPER responsive to
// other calls during the render.
//
// A job's status stays "queued" for as long as it sits in Executor's FIFO
// queue — even if several async renders are queued back-to-back and their
// worker threads have all already started — and only flips to "running"
// once Executor::tick() actually dequeues it and starts do_render on
// REAPER's main thread. That's the point where cancellation genuinely stops
// being possible, so it's also where DELETE's "queued vs. running" check
// needs to line up with reality.
void run_render_job_async(std::shared_ptr<RenderJob> job, RenderParams params) {
    std::thread([job, params]() {
        auto t0 = std::chrono::steady_clock::now();
        // Long timeout — async jobs exist specifically so a caller doesn't
        // need to guess a project's render time up front.
        auto result = Executor::post(
                [job, params]() -> nlohmann::json {
                    {
                        std::lock_guard<std::mutex> lk(s_jobs_mutex);
                        if (job->status == "cancelled")
                            return {{"_cancelled", true}};
                        job->status = "running";
                        job->started_at = now_iso();
                        job->start_tp = std::chrono::steady_clock::now();
                    }
                    return do_render(params);
                },
                3600);
        auto t1 = std::chrono::steady_clock::now();
        double render_secs = std::chrono::duration<double>(t1 - t0).count();

        std::lock_guard<std::mutex> lk(s_jobs_mutex);
        if (job->status == "cancelled")
            return;  // cancelled while queued; the lambda skipped the render
        job->finished_at = now_iso();
        if (result.contains("_timeout")) {
            job->status = "error";
            job->error = "Render timed out on main thread";
        } else if (result.contains("_error")) {
            job->status = "error";
            job->error = result["_error"].get<std::string>();
        } else {
            job->status = "done";
            job->render_seconds = render_secs;
            job->project_length = result.value("project_length", 0.0);
            job->offline_ratio = (render_secs > 0.001 && job->project_length > 0.0)
                                         ? (job->project_length / render_secs)
                                         : 0.0;
        }
    }).detach();
}

}  // namespace

// ---------------------------------------------------------------------------
// POST /render
//
// Body (all fields optional except output):
//   output          string  Full path for the rendered file (dir must exist).
//   format          string  "wav" | "flac" | "mp3" | "ogg" — default "wav".
//   bit_depth       int     16, 24, or 32 — default 24. (WAV / FLAC only.)
//   srate           int     Sample rate in Hz — default 44100.
//   channels        int     1 or 2 (or more) — default 2.
//   bounds          string  "project" | "time_selection" | "all_regions" | "custom"
//                           default "project".
//   start           float   Render start (seconds). Used when bounds="custom".
//   end             float   Render end (seconds). Used when bounds="custom".
//   mp3_bitrate     int     CBR bitrate kbps (mp3 only) — default 192.
//   flac_compression int    Compression 1-8 (flac only) — default 5.
//   normalize       object  {target_lufs} — accepted but not yet implemented.
//
// Response:
//   output_path     string  Full path to the rendered file.
//   format          string  Format used.
//   srate           int     Sample rate used.
//   channels        int     Channel count used.
//   render_seconds  float   Wall-clock seconds the render took.
//   project_length  float   Project length at render time (seconds).
//   offline_ratio   float   project_length / render_seconds (speed multiple).
//   rendered_at     string  ISO-8601 timestamp.
//   warnings        array   Non-fatal notices (e.g. unsupported fields).
// ---------------------------------------------------------------------------
void handle_render(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (...) {
        json_error(res, 400, "Invalid JSON body", "BAD_REQUEST");
        return;
    }

    if (!body.contains("output") || !body["output"].is_string()) {
        json_error(res,
                   400,
                   "Missing required field: output",
                   "BAD_REQUEST",
                   {{"schema",
                     {{"required", {"output"}},
                      {"optional",
                       {{"format", "string: wav|flac|mp3|ogg, default wav"},
                        {"srate", "integer, default 44100"},
                        {"channels", "integer 1-64, default 2"},
                        {"bit_depth", "integer: 16|24|32, default 24"},
                        {"bounds",
                         "string: project|time_selection|all_regions|custom, default project"},
                        {"start", "number (seconds), required when bounds=custom"},
                        {"end", "number (seconds), required when bounds=custom"}}}}}});
        return;
    }

    std::string output = body["output"].get<std::string>();
    std::string format = body.value("format", "wav");
    int bit_depth = body.value("bit_depth", 24);
    int srate = body.value("srate", 44100);
    int channels = body.value("channels", 2);
    std::string bounds_str = body.value("bounds", "project");
    double start_sec = body.value("start", 0.0);
    double end_sec = body.value("end", 0.0);
    int mp3_bitrate = body.value("mp3_bitrate", 192);
    int flac_comp = body.value("flac_compression", 5);

    // Validate
    if (output.empty()) {
        json_error(res, 400, "output path must not be empty", "BAD_REQUEST");
        return;
    }
    if (format != "wav" && format != "flac" && format != "mp3" && format != "ogg") {
        json_error(res,
                   400,
                   "Unsupported format '" + format + "'. Supported: wav, flac, mp3, ogg",
                   "BAD_REQUEST");
        return;
    }
    if (bit_depth != 16 && bit_depth != 24 && bit_depth != 32) {
        json_error(res, 400, "bit_depth must be 16, 24, or 32", "BAD_REQUEST");
        return;
    }
    if (srate < 8000 || srate > 192000) {
        json_error(res, 400, "srate must be between 8000 and 192000", "BAD_REQUEST");
        return;
    }
    if (channels < 1 || channels > 64) {
        json_error(res, 400, "channels must be between 1 and 64", "BAD_REQUEST");
        return;
    }
    if (bounds_str != "project" && bounds_str != "time_selection" && bounds_str != "all_regions" &&
        bounds_str != "custom") {
        json_error(res,
                   400,
                   "bounds must be 'project', 'time_selection', 'all_regions', or 'custom'",
                   "BAD_REQUEST");
        return;
    }
    if (bounds_str == "custom" && end_sec <= start_sec) {
        json_error(res, 400, "end must be greater than start for bounds='custom'", "BAD_REQUEST");
        return;
    }

    nlohmann::json warnings = nlohmann::json::array();
    if (body.contains("normalize"))
        warnings.push_back("normalize is not yet implemented and was ignored");
    if (body.value("stems", false))
        warnings.push_back("stem rendering is not yet implemented; rendering to single file");

    // Build RENDER_FORMAT blob (base64-encoded binary).
    // WAV: uses bit_depth param.
    // FLAC: clamps 32-bit to 24-bit (FLAC doesn't support float PCM via this path).
    // MP3 / OGG: use respective quality/bitrate params.
    std::string fmt_blob;
    if (format == "wav") {
        fmt_blob = wav_render_format(bit_depth);
    } else if (format == "flac") {
        int bd = (bit_depth == 32) ? 24 : bit_depth;
        if (bit_depth == 32)
            warnings.push_back("FLAC does not support 32-bit float; using 24-bit");
        fmt_blob = flac_render_format(bd, flac_comp);
    } else if (format == "mp3") {
        fmt_blob = mp3_render_format(mp3_bitrate);
    } else {
        fmt_blob = ogg_render_format(6);  // Vorbis quality ~0.6 ≈ 192 kbps
    }

    // Split output path into directory (RENDER_FILE) and filename (RENDER_PATTERN).
    std::string render_dir;
    std::string render_pat;
    {
        auto sep = output.find_last_of("/\\");
        if (sep != std::string::npos) {
            render_dir = output.substr(0, sep + 1);
            render_pat = output.substr(sep + 1);
        } else {
            render_dir = "";
            render_pat = output;
        }
    }

    Log::info("Render: " + output + " fmt=" + format + " srate=" + std::to_string(srate) +
              " ch=" + std::to_string(channels) + " bounds=" + bounds_str);

    RenderParams params;
    params.render_dir = render_dir;
    params.render_pat = render_pat;
    params.fmt_blob = fmt_blob;
    params.bounds_flag_val = bounds_flag(bounds_str);
    params.srate = srate;
    params.channels = channels;
    params.custom_bounds = (bounds_str == "custom");
    params.start_sec = start_sec;
    params.end_sec = end_sec;

    // Issue #35 — async job path: return a job handle immediately instead of
    // blocking the HTTP thread. Mirrors the `async` field already used by
    // POST /execute/action.
    if (body.value("async", false)) {
        auto job = std::make_shared<RenderJob>();
        job->id = "job_" + std::to_string(s_next_job_id.fetch_add(1));
        job->output_path = output;
        job->format = format;
        job->srate = srate;
        job->channels = channels;
        job->warnings = warnings;
        job->created_at = now_iso();
        job->agent_id = agent_id(req);

        {
            std::lock_guard<std::mutex> lk(s_jobs_mutex);
            s_jobs.push_back(job);
            evict_old_jobs_locked();
        }

        run_render_job_async(job, params);

        json_ok(res, {{"job_id", job->id}, {"status", "queued"}});
        return;
    }

    auto t0 = std::chrono::steady_clock::now();

    // Render must run on the main thread. Use a long timeout (300 s) because
    // offline renders can exceed the default 5 s for large projects.
    auto result = Executor::post(
            [params]() -> nlohmann::json {
                return do_render(params);
            },
            300);

    auto t1 = std::chrono::steady_clock::now();
    double render_secs = std::chrono::duration<double>(t1 - t0).count();

    if (exec_error(res, result))
        return;

    double project_length = result.value("project_length", 0.0);
    double offline_ratio = (render_secs > 0.001 && project_length > 0.0)
                                   ? (project_length / render_secs)
                                   : 0.0;

    nlohmann::json resp = {{"output_path", output},
                           {"format", format},
                           {"srate", srate},
                           {"channels", channels},
                           {"render_seconds", render_secs},
                           {"project_length", project_length},
                           {"offline_ratio", offline_ratio},
                           {"rendered_at", now_iso()}};
    if (!warnings.empty())
        resp["warnings"] = warnings;

    json_ok(res, resp);
}

// ---------------------------------------------------------------------------
// GET /render/jobs/{id}
// ---------------------------------------------------------------------------
void handle_render_job_get(const httplib::Request& req, httplib::Response& res) {
    auto it = req.path_params.find("id");
    if (it == req.path_params.end()) {
        json_error(res, 400, "Missing job id", "BAD_REQUEST");
        return;
    }

    std::lock_guard<std::mutex> lk(s_jobs_mutex);
    auto job = find_job_locked(it->second);
    if (!job) {
        json_error(res, 404, "Render job not found: " + it->second, "NOT_FOUND");
        return;
    }
    json_ok(res, job_to_json(job));
}

// ---------------------------------------------------------------------------
// GET /render/jobs
// ---------------------------------------------------------------------------
void handle_render_jobs_list(const httplib::Request&, httplib::Response& res) {
    nlohmann::json jobs = nlohmann::json::array();
    std::lock_guard<std::mutex> lk(s_jobs_mutex);
    for (const auto& job : s_jobs)
        jobs.push_back(job_to_json(job));
    json_ok(res, {{"jobs", jobs}});
}

// ---------------------------------------------------------------------------
// DELETE /render/jobs/{id}
// ---------------------------------------------------------------------------
void handle_render_job_cancel(const httplib::Request& req, httplib::Response& res) {
    auto it = req.path_params.find("id");
    if (it == req.path_params.end()) {
        json_error(res, 400, "Missing job id", "BAD_REQUEST");
        return;
    }

    std::lock_guard<std::mutex> lk(s_jobs_mutex);
    auto job = find_job_locked(it->second);
    if (!job) {
        json_error(res, 404, "Render job not found: " + it->second, "NOT_FOUND");
        return;
    }
    if (job->status == "queued") {
        job->status = "cancelled";
        job->finished_at = now_iso();
        json_ok(res, job_to_json(job));
        return;
    }
    if (job->status == "running") {
        json_error(res,
                   409,
                   "Render job is already in progress; REAPER's offline render has no safe "
                   "abort — wait for it to finish",
                   "CONFLICT");
        return;
    }
    json_error(res, 409, "Render job already finished (status: " + job->status + ")", "CONFLICT");
}

}  // namespace ReaClaw::Handlers

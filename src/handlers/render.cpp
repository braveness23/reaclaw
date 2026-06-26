#include "handlers/render.h"

#include "handlers/common.h"
#include "reaper/executor.h"
#include "util/image.h"
#include "util/logging.h"

#include <httplib.h>

#include <json.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <reaper_plugin_functions.h>

namespace ReaClaw::Handlers {

namespace {

// ---------------------------------------------------------------------------
// RENDER_FORMAT blob builder
//
// REAPER stores codec settings for the render output as a base64-encoded
// binary blob under the "RENDER_FORMAT" project info key. The blob is
// codec-specific; leaving it empty (or as-is) uses whatever the project
// currently has, which defaults to WAV 24-bit in a fresh REAPER project.
//
// Blob layout (integers little-endian):
//   [0..3]  uint32  codec type discriminator
//   [4..]   codec-specific parameters
//
// Type discriminators (from REAPER internal codec registry):
//   0x00000000  PCM / WAV
//   0x00010000  FLAC
//   0x00020000  LAME MP3
//   0x00030000  OGG Vorbis
// ---------------------------------------------------------------------------

static std::string wav_render_format(int bit_depth) {
    std::vector<uint8_t> blob(8, 0);
    // [0..3]: type = 0 (PCM) — already zero
    blob[4] = static_cast<uint8_t>(bit_depth);  // 16, 24, or 32
    // [5..7]: dither=0, flags=0, reserved=0
    return Image::base64(blob);
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
//   0 = entire project
//   1 = time selection
//   2 = all project regions/markers
//   3 = selected markers/regions
// ---------------------------------------------------------------------------
static int bounds_flag(const std::string& bounds) {
    if (bounds == "time_selection")
        return 1;
    if (bounds == "all_regions")
        return 2;
    return 0;  // "project" or fallback → entire project
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
        json_error(res, 400, "Missing required field: output", "BAD_REQUEST");
        return;
    }

    std::string output      = body["output"].get<std::string>();
    std::string format      = body.value("format", "wav");
    int         bit_depth   = body.value("bit_depth", 24);
    int         srate       = body.value("srate", 44100);
    int         channels    = body.value("channels", 2);
    std::string bounds_str  = body.value("bounds", "project");
    double      start_sec   = body.value("start", 0.0);
    double      end_sec     = body.value("end", 0.0);
    int         mp3_bitrate = body.value("mp3_bitrate", 192);
    int         flac_comp   = body.value("flac_compression", 5);

    // Validate
    if (output.empty()) {
        json_error(res, 400, "output path must not be empty", "BAD_REQUEST");
        return;
    }
    if (format != "wav" && format != "flac" && format != "mp3" && format != "ogg") {
        json_error(res, 400,
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
    if (bounds_str != "project" && bounds_str != "time_selection" &&
        bounds_str != "all_regions" && bounds_str != "custom") {
        json_error(res, 400,
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

    Log::info("Render: " + output + " fmt=" + format +
              " srate=" + std::to_string(srate) + " ch=" + std::to_string(channels) +
              " bounds=" + bounds_str);

    // Capture everything needed inside the lambda by value.
    struct RenderParams {
        std::string render_dir;
        std::string render_pat;
        std::string fmt_blob;
        int         bounds_flag_val;
        int         srate;
        int         channels;
        bool        custom_bounds;
        double      start_sec;
        double      end_sec;
    };

    RenderParams params;
    params.render_dir      = render_dir;
    params.render_pat      = render_pat;
    params.fmt_blob        = fmt_blob;
    params.bounds_flag_val = bounds_flag(bounds_str);
    params.srate           = srate;
    params.channels        = channels;
    params.custom_bounds   = (bounds_str == "custom");
    params.start_sec       = start_sec;
    params.end_sec         = end_sec;

    auto t0 = std::chrono::steady_clock::now();

    // Render must run on the main thread. Use a long timeout (300 s) because
    // offline renders can exceed the default 5 s for large projects. The async
    // job model (issue #35) will handle very long renders with a job handle.
    auto result = Executor::post(
            [params]() -> nlohmann::json {
                if (!GetSetProjectInfo || !GetSetProjectInfo_String || !Main_OnCommand ||
                    !GetProjectLength) {
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

                GetSetProjectInfo_String(nullptr, "RENDER_FILE",
                                         old_file.data(), static_cast<int>(old_file.size()),
                                         false);
                GetSetProjectInfo_String(nullptr, "RENDER_PATTERN",
                                         old_pattern.data(),
                                         static_cast<int>(old_pattern.size()), false);
                GetSetProjectInfo_String(nullptr, "RENDER_FORMAT",
                                         old_format.data(),
                                         static_cast<int>(old_format.size()), false);
                old_boundsflag = GetSetProjectInfo(nullptr, "RENDER_BOUNDSFLAG", 0, false);
                old_srate      = GetSetProjectInfo(nullptr, "RENDER_SRATE", 0, false);
                old_channels   = GetSetProjectInfo(nullptr, "RENDER_CHANNELS", 0, false);

                if (params.custom_bounds && GetSet_LoopTimeRange2) {
                    GetSet_LoopTimeRange2(nullptr, false, false, &old_loop_start, &old_loop_end,
                                          false);
                }

                // Apply render settings.
                auto dir_buf = to_mutable(params.render_dir);
                auto pat_buf = to_mutable(params.render_pat);
                auto fmt_buf = to_mutable(params.fmt_blob);

                GetSetProjectInfo_String(nullptr, "RENDER_FILE", dir_buf.data(), 0, true);
                GetSetProjectInfo_String(nullptr, "RENDER_PATTERN", pat_buf.data(), 0, true);
                GetSetProjectInfo_String(nullptr, "RENDER_FORMAT", fmt_buf.data(), 0, true);
                GetSetProjectInfo(nullptr, "RENDER_BOUNDSFLAG",
                                   static_cast<double>(params.bounds_flag_val), true);
                GetSetProjectInfo(nullptr, "RENDER_SRATE",
                                   static_cast<double>(params.srate), true);
                GetSetProjectInfo(nullptr, "RENDER_CHANNELS",
                                   static_cast<double>(params.channels), true);

                // For custom bounds: set the project time selection and render
                // with RENDER_BOUNDSFLAG=1 (time selection).
                if (params.custom_bounds && GetSet_LoopTimeRange2) {
                    double s = params.start_sec;
                    double e = params.end_sec;
                    GetSet_LoopTimeRange2(nullptr, true, false, &s, &e, false);
                    GetSetProjectInfo(nullptr, "RENDER_BOUNDSFLAG", 1.0, true);
                }

                double project_length = GetProjectLength(nullptr);

                // Action 41824 = "File: Render project, using the most recent
                // render settings". Runs synchronously on the main thread and blocks
                // until the render file is fully written.
                Main_OnCommand(41824, 0);

                // Restore previous render settings.
                GetSetProjectInfo_String(nullptr, "RENDER_FILE",
                                         old_file.data(), 0, true);
                GetSetProjectInfo_String(nullptr, "RENDER_PATTERN",
                                         old_pattern.data(), 0, true);
                GetSetProjectInfo_String(nullptr, "RENDER_FORMAT",
                                         old_format.data(), 0, true);
                GetSetProjectInfo(nullptr, "RENDER_BOUNDSFLAG", old_boundsflag, true);
                GetSetProjectInfo(nullptr, "RENDER_SRATE", old_srate, true);
                GetSetProjectInfo(nullptr, "RENDER_CHANNELS", old_channels, true);

                if (params.custom_bounds && GetSet_LoopTimeRange2) {
                    GetSet_LoopTimeRange2(nullptr, true, false, &old_loop_start, &old_loop_end,
                                          false);
                }

                return {{"ok", true}, {"project_length", project_length}};
            },
            300);

    auto t1            = std::chrono::steady_clock::now();
    double render_secs = std::chrono::duration<double>(t1 - t0).count();

    if (exec_error(res, result))
        return;

    double project_length = result.value("project_length", 0.0);
    double offline_ratio  = (render_secs > 0.001 && project_length > 0.0)
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

}  // namespace ReaClaw::Handlers

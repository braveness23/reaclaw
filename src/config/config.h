#pragma once
#include <string>

namespace ReaClaw {

struct Config {
    // server
    std::string host = "0.0.0.0";
    int port = 9091;
    int thread_pool_size = 4;

    // tls
    bool tls_enabled = true;
    bool tls_generate_if_missing = true;
    std::string tls_cert_file;
    std::string tls_key_file;

    // auth
    std::string auth_type = "api_key";  // "none" or "api_key"
    std::string auth_key = "sk_change_me";

    // database
    std::string db_path;  // defaults to {resource_dir}/reaclawdb.sqlite

    // script_security
    bool validate_syntax = true;
    bool log_all_executions = true;
    int max_script_size_kb = 512;

    // logging
    std::string log_level = "info";   // debug, info, warn, error
    std::string log_file;             // empty = REAPER console only
    std::string log_format = "text";  // "text" or "json"

    // learning (Epic #20) — local-first, opt-in correction mining. Off by
    // default: nothing is recorded unless the user turns it on, and nothing ever
    // leaves the machine.
    bool learning_enabled = false;
    int learning_window_seconds = 180;     // max gap for one edit to "follow" another
    int learning_min_support = 3;          // min observations before a pattern surfaces
    double learning_min_confidence = 0.3;  // min P(consequent | antecedent) to surface

    // semantic_search (issue #10) — opt-in, localhost-only embedding-based
    // catalog search via a local Ollama instance. Off by default: zero network
    // egress out of the box. See ReaClaw_TECH_DECISIONS.md §25 for why this is
    // a narrow, explicit carve-out of §11's no-LLM-client stance (an embedding
    // model, not a generative one, and this project already ships the same
    // call from the MCP client — this just makes it available server-side).
    bool semantic_search_enabled = false;
    std::string semantic_search_ollama_url = "http://127.0.0.1:11434";
    std::string semantic_search_model = "nomic-embed-text";

    // streaming — live video/audio-out over HTTP (handlers/stream_video.cpp,
    // handlers/stream_audio.cpp). Linux/X11+PulseAudio only; see
    // ReaClaw_TECH_DECISIONS.md for the query-token auth carve-out this
    // requires and the one-process-per-connection concurrency model.
    std::string streaming_ffmpeg_path = "ffmpeg";  // resolved via PATH, like /screenshot
    int streaming_video_fps = 10;
    int streaming_video_quality = 5;             // ffmpeg -q:v (2=best .. 31=worst)
    std::string streaming_audio_monitor_source;  // PulseAudio monitor name; no
                                                 // reliable auto-detect — see docs/API.md
    int streaming_audio_bitrate_kbps = 128;
    int streaming_max_duration_minutes = 10;  // mirrors events.cpp's kMaxStreamDuration

    // Derived paths (filled in by load())
    std::string resource_dir;  // {GetResourcePath()}/reaclaw/
    std::string certs_dir;     // {resource_dir}/certs/
    std::string scripts_dir;   // {resource_dir}/scripts/

    // Resolved TLS paths (filled in by load(); may be derived from resource_dir)
    std::string resolved_cert_path;
    std::string resolved_key_path;

    // Load config from {resource_path}/reaclaw/config.json.
    // Writes defaults when the file is missing.
    // Returns false only on fatal error (cannot create directory or parse JSON).
    static bool load(Config& out, const std::string& resource_path);

    // Persist current values back to config.json.
    bool save() const;
};

}  // namespace ReaClaw

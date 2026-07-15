#pragma once

#include <httplib.h>

namespace ReaClaw::Handlers {

// GET /stream/audio?bitrate=&token=<key>
//
// Continuous MP3-over-HTTP stream of REAPER's audio output — open the URL
// directly in a browser <audio> tag or a phone's stock player, no extra
// software required. Formalizes the PulseAudio-null-sink capture pattern
// already used ad hoc in demos/scripts/record.sh (see
// ReaClaw_TECH_DECISIONS.md §19) into a permanent endpoint: a persistent
// `ffmpeg -f pulse -i <monitor> -f mp3` subprocess (util/subprocess.h),
// relayed as a raw MP3 byte stream — the same shape internet radio has
// always used, no container/moov-box concerns. One ffmpeg process per HTTP
// connection, same lifecycle as GET /stream/video. Requires
// config streaming.audio_monitor_source to be set (no reliable auto-detect
// exists); see GET /stream/audio/devices to discover the right name.
// `?token=` auth (Auth::check_stream) exists because a browser/media-player
// tag can't set a custom Authorization header. Linux/PulseAudio only.
void handle_stream_audio(const httplib::Request& req, httplib::Response& res);

// GET /stream/audio/devices — lists PulseAudio sources (`pactl list short
// sources`) so a caller can find the right monitor name without leaving the
// API.
void handle_stream_audio_devices(const httplib::Request& req, httplib::Response& res);

}  // namespace ReaClaw::Handlers

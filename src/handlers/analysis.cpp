#include "handlers/analysis.h"

#include "handlers/common.h"
#include "reaper/executor.h"
#include "util/dsp.h"
#include "util/logging.h"

#include <httplib.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <reaper_plugin_functions.h>  // also pulls in reaper_plugin.h (PCM_source)

#include <json.hpp>

namespace ReaClaw::Handlers {

namespace {

constexpr double kSilenceDb = -150.0;  // reported when a measure is effectively silent

bool exec_error(httplib::Response& res, const nlohmann::json& result) {
    if (result.contains("_timeout")) {
        json_error(res,
                   408,
                   "Analysis timed out — the source may be very long; try a shorter "
                   "start/end window",
                   "TIMEOUT");
        return true;
    }
    if (result.contains("_error")) {
        json_error(res, 500, result["_error"].get<std::string>(), "INTERNAL_ERROR");
        return true;
    }
    if (result.contains("_not_found")) {
        json_error(res, 404, result.value("_message", "Not found"), "NOT_FOUND");
        return true;
    }
    if (result.contains("_bad_request")) {
        json_error(res, 400, result.value("_message", "Bad request"), "BAD_REQUEST");
        return true;
    }
    return false;
}

// Linear gain → dB.
double lin2db(double g) {
    if (!(g > 0.0))
        return kSilenceDb;
    return 20.0 * std::log10(g);
}

// One exact loudness/level measure of a source via REAPER's offline analyser.
// CalculateNormalization returns the linear gain needed to bring the source to
// `target`; with target 0 the measured value is simply -gain_in_dB.
//   mode: 0=LUFS-I, 1=RMS-I, 2=peak, 3=true-peak, 4=LUFS-M max, 5=LUFS-S max.
double measure_db(PCM_source* src, int mode, double start, double end) {
    double g = CalculateNormalization(src, mode, 0.0, start, end);
    if (!(g > 0.0))
        return kSilenceDb;
    return -lin2db(g);
}

// ---------------------------------------------------------------------------
// Rough spectral balance: decode the source to mono and run a small radix-2
// FFT (ReaClaw::dsp::fft), then fold magnitude² into three bands. Estimated
// DSP, not exact.
// ---------------------------------------------------------------------------

struct Spectral {
    bool ok = false;
    double low = 0, mid = 0, high = 0;  // fractions of total energy, sum ~1
    double centroid_hz = 0;
    int frames = 0;
};

// Decode up to a bounded number of frames and accumulate band energy. Bands:
// low < 250 Hz, mid 250–4000 Hz, high > 4000 Hz.
Spectral analyze_spectral(PCM_source* src, double start, double max_dur) {
    constexpr int SR = 44100;
    constexpr int N = 4096;
    constexpr int MAX_FRAMES = 240;  // ~22s of audio at 4096/44100 per frame

    std::vector<ReaSample> buf(N);
    std::vector<double> re(N), im(N), window(N);
    for (int i = 0; i < N; i++)
        window[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (N - 1));  // Hann

    double low = 0, mid = 0, high = 0, total = 0, centroid_num = 0;
    int frames = 0;
    double t = start;
    const double frame_dur = static_cast<double>(N) / SR;

    while (frames < MAX_FRAMES && t < start + max_dur) {
        PCM_source_transfer_t blk;
        memset(&blk, 0, sizeof(blk));
        blk.time_s = t;
        blk.samplerate = SR;
        blk.nch = 1;  // ask REAPER to downmix to mono
        blk.length = N;
        blk.samples = buf.data();
        blk.samples_out = 0;
        src->GetSamples(&blk);
        int got = blk.samples_out;
        if (got <= 0)
            break;

        for (int i = 0; i < N; i++) {
            re[i] = (i < got ? static_cast<double>(buf[i]) : 0.0) * window[i];
            im[i] = 0.0;
        }
        dsp::fft(re, im, N);
        for (int k = 1; k < N / 2; k++) {
            double p = re[k] * re[k] + im[k] * im[k];
            double f = static_cast<double>(k) * SR / N;
            if (f < 250.0)
                low += p;
            else if (f < 4000.0)
                mid += p;
            else
                high += p;
            total += p;
            centroid_num += f * p;
        }
        frames++;
        t += frame_dur;
        if (got < N)
            break;  // hit end of source
    }

    Spectral s;
    if (total <= 0.0 || frames == 0)
        return s;
    s.ok = true;
    s.low = low / total;
    s.mid = mid / total;
    s.high = high / total;
    s.centroid_hz = centroid_num / total;
    s.frames = frames;
    return s;
}

nlohmann::json source_meta(PCM_source* src) {
    char file[4096] = {};
    GetMediaSourceFileName(src, file, sizeof(file));
    char type[64] = {};
    GetMediaSourceType(src, type, sizeof(type));
    bool qn = false;
    return {{"file", file},
            {"type", type},
            {"length", GetMediaSourceLength(src, &qn)},
            {"sample_rate", GetMediaSourceSampleRate(src)},
            {"num_channels", GetMediaSourceNumChannels(src)}};
}

// Build the analysis payload for an already-resolved source. `which` selects
// which measure groups to compute (loudness and/or spectral).
nlohmann::json
analyze_source(PCM_source* src, double start, double end, bool want_loudness, bool want_spectral) {
    nlohmann::json out;
    out["source"] = source_meta(src);
    out["window"] = {{"start", start},
                     {"end", end > 0.0 ? nlohmann::json(end) : nlohmann::json(nullptr)}};

    if (want_loudness) {
        double lufs = measure_db(src, 0, start, end);
        double rms = measure_db(src, 1, start, end);
        double peak = measure_db(src, 2, start, end);
        double tpeak = measure_db(src, 3, start, end);
        out["loudness"] = {{"lufs_i", lufs},
                           {"rms_i", rms},
                           {"peak_db", peak},
                           {"true_peak_db", tpeak},
                           {"method", "offline_analysis"},
                           {"confidence", 1.0}};
        // Clipping derived from peak / true-peak. Digital clip at/above 0 dBFS;
        // inter-sample (true-peak) overs above 0 dBTP even when samples don't clip.
        out["clipping"] = {{"digital", peak >= 0.0},
                           {"inter_sample", tpeak > 0.0},
                           {"true_peak_db", tpeak},
                           {"method", "derived"},
                           {"confidence", 1.0}};
    }

    if (want_spectral) {
        bool qn = false;
        double len = GetMediaSourceLength(src, &qn);
        double dur = end > 0.0 ? (end - start) : (len - start);
        if (dur <= 0.0)
            dur = len;
        Spectral sp = analyze_spectral(src, start, dur);
        if (sp.ok) {
            const char* dom = sp.low >= sp.mid && sp.low >= sp.high
                                      ? "low"
                                      : (sp.mid >= sp.high ? "mid" : "high");
            out["spectral"] = {{"low", sp.low},
                               {"mid", sp.mid},
                               {"high", sp.high},
                               {"centroid_hz", sp.centroid_hz},
                               {"dominant_band", dom},
                               {"frames_analyzed", sp.frames},
                               {"bands_hz",
                                {{"low", "<250"}, {"mid", "250-4000"}, {"high", ">4000"}}},
                               {"method", "estimated_dsp"},
                               {"confidence", 0.6}};
        } else {
            out["spectral"] = {{"method", "estimated_dsp"},
                               {"confidence", 0.0},
                               {"note", "no decodable audio in window"}};
        }
    }
    return out;
}

// Parse the ?measures= filter; defaults to both groups.
void parse_measures(const httplib::Request& req, bool& loudness, bool& spectral) {
    auto it = req.params.find("measures");
    if (it == req.params.end() || it->second.empty()) {
        loudness = spectral = true;
        return;
    }
    loudness = spectral = false;
    std::string m = it->second;
    auto has = [&](const char* k) {
        return m.find(k) != std::string::npos;
    };
    if (has("loud") || has("lufs") || has("rms") || has("peak") || has("clip"))
        loudness = true;
    if (has("spectr") || has("band") || has("eq"))
        spectral = true;
    if (!loudness && !spectral)
        loudness = spectral = true;  // unrecognized → give everything
}

double query_double(const httplib::Request& req, const char* key, double dflt) {
    auto it = req.params.find(key);
    if (it == req.params.end())
        return dflt;
    try {
        return std::stod(it->second);
    } catch (...) {
        return dflt;
    }
}

}  // namespace

// GET /analysis/item/{index}?measures=loudness,spectral&start=&end=
void handle_analysis_item(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    try {
        index = std::stoi(req.path_params.at("index"));
    } catch (...) {
        json_error(res, 400, "index must be a numeric integer", "BAD_REQUEST");
        return;
    }
    bool want_loud = true, want_spec = true;
    parse_measures(req, want_loud, want_spec);
    double start = query_double(req, "start", 0.0);
    double end = query_double(req, "end", 0.0);

    auto result = Executor::post(
            [index, start, end, want_loud, want_spec]() -> nlohmann::json {
                if (index < 0 || index >= CountMediaItems(nullptr))
                    return {{"_not_found", true}, {"_message", "Item index out of range"}};
                MediaItem* it = GetMediaItem(nullptr, index);
                if (!it)
                    return {{"_not_found", true}, {"_message", "Item index out of range"}};
                MediaItem_Take* take = GetActiveTake(it);
                PCM_source* src = take ? GetMediaItemTake_Source(take) : nullptr;
                if (!src)
                    return {{"_bad_request", true},
                            {"_message", "Item has no audio source (empty item or MIDI take)"}};
                nlohmann::json a = analyze_source(src, start, end, want_loud, want_spec);
                a["item_index"] = index;
                return a;
            },
            30);
    if (exec_error(res, result))
        return;
    json_ok(res, result);
}

// GET /analysis/file?path=/abs/file.wav&measures=&start=&end=
// Analyses an arbitrary audio file — e.g. a freshly rendered stem/mix.
void handle_analysis_file(const httplib::Request& req, httplib::Response& res) {
    auto pit = req.params.find("path");
    if (pit == req.params.end() || pit->second.empty()) {
        json_error(res, 400, "Missing required query param: path", "BAD_REQUEST");
        return;
    }
    std::string path = pit->second;
    bool want_loud = true, want_spec = true;
    parse_measures(req, want_loud, want_spec);
    double start = query_double(req, "start", 0.0);
    double end = query_double(req, "end", 0.0);

    auto result = Executor::post(
            [path, start, end, want_loud, want_spec]() -> nlohmann::json {
                PCM_source* src = PCM_Source_CreateFromFile(path.c_str());
                if (!src)
                    return {{"_not_found", true},
                            {"_message", "Could not open audio file: " + path}};
                nlohmann::json a;
                try {
                    a = analyze_source(src, start, end, want_loud, want_spec);
                } catch (...) {
                    PCM_Source_Destroy(src);
                    throw;
                }
                PCM_Source_Destroy(src);
                return a;
            },
            30);
    if (exec_error(res, result))
        return;
    json_ok(res, result);
}

// GET /state/meters — live per-track + master peak metering.
// These are REAPER's own meter readings (introspection); they are only
// meaningful while audio is running (playback/record), and read instantaneous
// peak plus the running peak-hold.
void handle_meters(const httplib::Request& req, httplib::Response& res) {
    (void)req;
    auto result = Executor::post([]() -> nlohmann::json {
        auto meter = [](MediaTrack* t) -> nlohmann::json {
            nlohmann::json peak = nlohmann::json::array();
            nlohmann::json hold = nlohmann::json::array();
            for (int ch = 0; ch < 2; ch++) {
                peak.push_back(lin2db(Track_GetPeakInfo(t, ch)));
                hold.push_back(Track_GetPeakHoldDB(t, ch, false));
            }
            return {{"peak_db", peak}, {"peak_hold_db", hold}};
        };
        int n = CountTracks(nullptr);
        nlohmann::json tracks = nlohmann::json::array();
        for (int i = 0; i < n; i++) {
            MediaTrack* t = GetTrack(nullptr, i);
            nlohmann::json m = meter(t);
            char name[256] = {};
            GetTrackName(t, name, sizeof(name));
            m["index"] = i;
            m["name"] = name;
            tracks.push_back(m);
        }
        nlohmann::json master = meter(GetMasterTrack(nullptr));
        return {{"audio_running", Audio_IsRunning() != 0},
                {"tracks", tracks},
                {"master", master},
                {"method", "introspection"},
                {"confidence", 1.0},
                {"note",
                 "live meter values in dBFS; meaningful only while audio is running "
                 "(play/record). -150 = silence/no signal."}};
    });
    if (exec_error(res, result))
        return;
    json_ok(res, result);
}

}  // namespace ReaClaw::Handlers

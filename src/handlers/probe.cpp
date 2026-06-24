#include "handlers/probe.h"

#include "handlers/common.h"
#include "reaper/executor.h"
#include "util/dsp.h"
#include "util/music.h"

#include <httplib.h>

#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <reaper_plugin_functions.h>  // also pulls in reaper_plugin.h (PCM_source)

#include <json.hpp>

#ifndef _WIN32
#include <cstdlib>

#include <fcntl.h>
#include <unistd.h>

#include <sys/wait.h>
#endif

namespace ReaClaw::Handlers {

namespace {

constexpr int kSR = 44100;
constexpr int kN = 4096;          // FFT frame (power of two)
constexpr int kMaxFrames = 240;   // ~22 s of audio; bounds decode cost
constexpr double kFundLo = 55.0;  // pitch search range (A1 … ~B6)
constexpr double kFundHi = 2000.0;

bool exec_error(httplib::Response& res, const nlohmann::json& result) {
    if (result.contains("_timeout")) {
        json_error(res, 408, "Probe timed out — try a shorter start/end window", "TIMEOUT");
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

// Which probes the caller asked for. Default: all.
struct Want {
    bool pitch = true, key = true, tempo = true;
};
Want parse_probes(const httplib::Request& req) {
    auto it = req.params.find("probes");
    if (it == req.params.end() || it->second.empty())
        return {};
    const std::string& p = it->second;
    auto has = [&](const char* k) {
        return p.find(k) != std::string::npos;
    };
    Want w{has("pitch"), has("key"), has("tempo")};
    if (!w.pitch && !w.key && !w.tempo)
        return {};  // unrecognised → give everything
    return w;
}

// ---- decode pass: averaged magnitude spectrum over the window ----------------

struct Spectrum {
    std::vector<double> mag;  // averaged |X[k]|, k in [0, kN/2)
    int frames = 0;
    bool ok = false;
};

Spectrum decode_spectrum(PCM_source* src, double start, double dur) {
    Spectrum s;
    s.mag.assign(kN / 2, 0.0);
    if (dur <= 0.0)
        return s;

    std::vector<ReaSample> buf(kN);
    std::vector<double> re(kN), im(kN), window(kN);
    for (int i = 0; i < kN; i++)
        window[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (kN - 1));  // Hann

    double t = start;
    const double frame_dur = static_cast<double>(kN) / kSR;
    int frames = 0;
    while (frames < kMaxFrames && t < start + dur) {
        PCM_source_transfer_t blk;
        memset(&blk, 0, sizeof(blk));
        blk.time_s = t;
        blk.samplerate = kSR;
        blk.nch = 1;  // downmix to mono
        blk.length = kN;
        blk.samples = buf.data();
        blk.samples_out = 0;
        src->GetSamples(&blk);
        int got = blk.samples_out;
        if (got <= 0)
            break;
        for (int i = 0; i < kN; i++) {
            re[i] = (i < got ? static_cast<double>(buf[i]) : 0.0) * window[i];
            im[i] = 0.0;
        }
        dsp::fft(re, im, kN);
        for (int k = 1; k < kN / 2; k++)
            s.mag[k] += std::sqrt(re[k] * re[k] + im[k] * im[k]);
        frames++;
        t += frame_dur;
        if (got < kN)
            break;
    }
    if (frames == 0)
        return s;
    for (auto& m : s.mag)
        m /= frames;
    s.frames = frames;
    s.ok = true;
    return s;
}

// Dominant fundamental: take the strongest spectral peak in the fundamental
// range, then correct for the octave-too-high case by preferring a sub-octave
// (f/2) only when it carries real energy. Plain peak-picking (not a harmonic
// product spectrum) so a pure tone — no energy at 2f/3f — still resolves to its
// own fundamental. Refined with parabolic interpolation. 0 Hz if nothing usable.
nlohmann::json estimate_pitch(const Spectrum& s) {
    const int half = kN / 2;
    int klo = std::max(1, static_cast<int>(kFundLo * kN / kSR));
    int khi = std::min(half - 1, static_cast<int>(kFundHi * kN / kSR));

    int kbest = 0;
    double mbest = 0.0;
    for (int k = klo; k <= khi; k++) {
        if (s.mag[k] > mbest) {
            mbest = s.mag[k];
            kbest = k;
        }
    }
    // Octave correction: if half the found frequency also has strong energy, the
    // peak was likely a harmonic — drop to the sub-octave fundamental.
    if (kbest >= 2 * klo) {
        int ksub = kbest / 2;
        if (ksub >= klo && s.mag[ksub] > 0.5 * mbest) {
            kbest = ksub;
            mbest = s.mag[ksub];
        }
    }
    if (kbest <= 0 || mbest <= 0.0)
        return {{"method", "estimated_dsp"},
                {"confidence", 0.0},
                {"note", "no pitched signal found"}};

    // Parabolic interpolation on the magnitude spectrum around the peak bin.
    double a = s.mag[kbest - 1], b = s.mag[kbest], c = s.mag[kbest + 1];
    double denom = a - 2.0 * b + c;
    double delta = denom != 0.0 ? 0.5 * (a - c) / denom : 0.0;
    double f0 = (kbest + delta) * kSR / static_cast<double>(kN);

    // Confidence from peak prominence: fundamental magnitude vs. spectral mean.
    double mean = 0.0;
    for (int k = 1; k < half; k++)
        mean += s.mag[k];
    mean /= (half - 1);
    double prominence = mean > 0.0 ? s.mag[kbest] / mean : 0.0;
    double conf = std::max(0.0, std::min(0.95, (prominence - 1.5) / 12.0));

    music::Note n = music::hz_to_note(f0);
    return {{"note", n.name + std::to_string(n.octave)},
            {"name", n.name},
            {"octave", n.octave},
            {"frequency_hz", f0},
            {"cents", n.cents},
            {"midi", n.midi},
            {"method", "estimated_dsp"},
            {"confidence", conf}};
}

nlohmann::json estimate_key(const Spectrum& s) {
    const int half = kN / 2;
    int klo = std::max(1, static_cast<int>(55.0 * kN / kSR));
    int khi = std::min(half - 1, static_cast<int>(5000.0 * kN / kSR));
    std::array<double, 12> chroma{};
    for (int k = klo; k <= khi; k++) {
        double f = static_cast<double>(k) * kSR / kN;
        int pc = static_cast<int>(std::lround(12.0 * std::log2(f / 440.0) + 69.0));
        pc = ((pc % 12) + 12) % 12;
        chroma[pc] += s.mag[k];
    }
    music::Key key = music::estimate_key(chroma);
    if (!key.ok)
        return {{"method", "estimated_dsp"}, {"confidence", 0.0}, {"note", "no tonal content"}};

    nlohmann::json chroma_out = nlohmann::json::array();
    for (double v : chroma)
        chroma_out.push_back(v);
    return {{"key", key.tonic + " " + key.mode},
            {"tonic", key.tonic},
            {"mode", key.mode},
            {"correlation", key.correlation},
            {"chroma", chroma_out},
            {"method", "estimated_dsp"},
            {"confidence", key.confidence}};
}

#ifndef _WIN32
// Run argv with no shell, capturing stdout+stderr into `out`. True on exit 0.
bool run_capture(const std::vector<std::string>& argv, std::string* out) {
    int pipefd[2];
    if (pipe(pipefd) != 0)
        return false;
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }
    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        std::vector<char*> cargv;
        for (const auto& a : argv)
            cargv.push_back(const_cast<char*>(a.c_str()));
        cargv.push_back(nullptr);
        execvp(cargv[0], cargv.data());
        _exit(127);
    }
    close(pipefd[1]);
    if (out) {
        char buf[4096];
        ssize_t n;
        while ((n = read(pipefd[0], buf, sizeof(buf))) > 0)
            out->append(buf, static_cast<size_t>(n));
    }
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool have_binary(const char* name) {
    return run_capture({"which", name}, nullptr);
}
#endif

// Optional advanced tempo detection from audio, via an external analyser
// (bpm-tools' `bpm-tag`). When the tool is absent the probe degrades gracefully:
// it reports `available:false` rather than failing.
nlohmann::json detect_tempo_external(const std::string& file) {
#ifndef _WIN32
    if (have_binary("bpm-tag")) {
        std::string out;
        if (run_capture({"bpm-tag", "-n", file}, &out)) {
            // bpm-tag prints e.g. "file.wav: 128.000 BPM".
            double bpm = 0.0;
            auto p = out.find("BPM");
            if (p != std::string::npos) {
                size_t b = out.rfind(' ', p > 0 ? p - 2 : 0);
                try {
                    bpm = std::stod(out.substr(b == std::string::npos ? 0 : b));
                } catch (...) {
                }
            }
            if (bpm > 0.0)
                return {{"available", true},
                        {"bpm", bpm},
                        {"tool", "bpm-tag"},
                        {"method", "estimated_dsp"},
                        {"confidence", 0.5}};
        }
    }
#endif
    (void)file;
    return {{"available", false},
            {"method", "estimated_dsp"},
            {"note",
             "tempo-from-audio needs an optional external analyser (e.g. bpm-tools' "
             "`bpm-tag`) on PATH; not found. Built-in introspection still reports the "
             "project tempo."}};
}

std::string source_file(PCM_source* src) {
    char file[4096] = {};
    GetMediaSourceFileName(src, file, sizeof(file));
    return file;
}

nlohmann::json source_meta(PCM_source* src) {
    char type[64] = {};
    GetMediaSourceType(src, type, sizeof(type));
    bool qn = false;
    return {{"file", source_file(src)},
            {"type", type},
            {"length", GetMediaSourceLength(src, &qn)},
            {"sample_rate", GetMediaSourceSampleRate(src)},
            {"num_channels", GetMediaSourceNumChannels(src)}};
}

double window_dur(PCM_source* src, double start, double end) {
    bool qn = false;
    double len = GetMediaSourceLength(src, &qn);
    double dur = end > 0.0 ? (end - start) : (len - start);
    return dur > 0.0 ? dur : len;
}

}  // namespace

// GET /analysis/item/{index}/probe?probes=pitch,key,tempo&start=&end=
void handle_probe_item(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    try {
        index = std::stoi(req.path_params.at("index"));
    } catch (...) {
        json_error(res, 400, "index must be a numeric integer", "BAD_REQUEST");
        return;
    }
    Want want = parse_probes(req);
    double start = query_double(req, "start", 0.0);
    double end = query_double(req, "end", 0.0);

    auto result = Executor::post(
            [index, want, start, end]() -> nlohmann::json {
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

                nlohmann::json out;
                out["item_index"] = index;
                out["source"] = source_meta(src);

                if (want.pitch || want.key) {
                    Spectrum s = decode_spectrum(src, start, window_dur(src, start, end));
                    if (s.ok) {
                        if (want.pitch)
                            out["pitch"] = estimate_pitch(s);
                        if (want.key)
                            out["key"] = estimate_key(s);
                    } else {
                        if (want.pitch)
                            out["pitch"] = {{"method", "estimated_dsp"},
                                            {"confidence", 0.0},
                                            {"note", "no decodable audio in window"}};
                        if (want.key)
                            out["key"] = {{"method", "estimated_dsp"},
                                          {"confidence", 0.0},
                                          {"note", "no decodable audio in window"}};
                    }
                }

                if (want.tempo) {
                    double pos = GetMediaItemInfo_Value(it, "D_POSITION");
                    int num = 4, den = 4;
                    double bpm = 0.0;
                    TimeMap_GetTimeSigAtTime(nullptr, pos, &num, &den, &bpm);
                    if (!(bpm > 0.0))
                        bpm = Master_GetTempo();
                    out["tempo"] = {{"project",
                                     {{"bpm", bpm},
                                      {"timesig_num", num},
                                      {"timesig_denom", den},
                                      {"method", "introspection"},
                                      {"confidence", 1.0},
                                      {"note",
                                       "the project tempo at this item's position — exact, "
                                       "not detected from audio"}}},
                                    {"detected", detect_tempo_external(source_file(src))}};
                }
                return out;
            },
            30);
    if (exec_error(res, result))
        return;
    json_ok(res, result);
}

// GET /analysis/file/probe?path=/abs/file.wav&probes=&start=&end=
void handle_probe_file(const httplib::Request& req, httplib::Response& res) {
    auto pit = req.params.find("path");
    if (pit == req.params.end() || pit->second.empty()) {
        json_error(res, 400, "Missing required query param: path", "BAD_REQUEST");
        return;
    }
    std::string path = pit->second;
    Want want = parse_probes(req);
    double start = query_double(req, "start", 0.0);
    double end = query_double(req, "end", 0.0);

    auto result = Executor::post(
            [path, want, start, end]() -> nlohmann::json {
                PCM_source* src = PCM_Source_CreateFromFile(path.c_str());
                if (!src)
                    return {{"_not_found", true},
                            {"_message", "Could not open audio file: " + path}};
                nlohmann::json out;
                try {
                    out["source"] = source_meta(src);
                    if (want.pitch || want.key) {
                        Spectrum s = decode_spectrum(src, start, window_dur(src, start, end));
                        if (s.ok) {
                            if (want.pitch)
                                out["pitch"] = estimate_pitch(s);
                            if (want.key)
                                out["key"] = estimate_key(s);
                        } else {
                            if (want.pitch)
                                out["pitch"] = {{"method", "estimated_dsp"},
                                                {"confidence", 0.0},
                                                {"note", "no decodable audio in window"}};
                            if (want.key)
                                out["key"] = {{"method", "estimated_dsp"},
                                              {"confidence", 0.0},
                                              {"note", "no decodable audio in window"}};
                        }
                    }
                    if (want.tempo) {
                        // A loose file has no project timebase, so only the
                        // estimated-from-audio source applies here.
                        out["tempo"] = {{"detected", detect_tempo_external(path)},
                                        {"note",
                                         "a loose file has no project tempo; introspection "
                                         "applies only to items placed in the project"}};
                    }
                } catch (...) {
                    PCM_Source_Destroy(src);
                    throw;
                }
                PCM_Source_Destroy(src);
                return out;
            },
            30);
    if (exec_error(res, result))
        return;
    json_ok(res, result);
}

}  // namespace ReaClaw::Handlers

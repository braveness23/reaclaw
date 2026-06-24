#include "handlers/visualize.h"

#include "handlers/common.h"
#include "reaper/executor.h"
#include "util/dsp.h"
#include "util/image.h"

#include <httplib.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <reaper_plugin_functions.h>

#include <json.hpp>

namespace ReaClaw::Handlers {

namespace {

constexpr int kSR = 44100;        // decode/analysis sample rate (mono downmix)
constexpr int kFrame = 4096;      // FFT frame size (power of two)
constexpr int kMaxSeconds = 120;  // cap decode length; longer windows truncate
constexpr int kBands = 32;        // log-spaced spectrum bands
constexpr double kSilenceDb = -150.0;
constexpr double kFloorDb = -80.0;  // visual floor for spectrum/loudness plots

// ---- small shared-style helpers (kept local to bound blast radius) ----------

bool exec_error(httplib::Response& res, const nlohmann::json& result) {
    if (result.contains("_timeout")) {
        json_error(res, 408, "Visualization timed out — try a shorter start/end window", "TIMEOUT");
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

int query_int(const httplib::Request& req, const char* key, int dflt, int lo, int hi) {
    auto it = req.params.find(key);
    if (it == req.params.end())
        return dflt;
    try {
        return std::clamp(std::stoi(it->second), lo, hi);
    } catch (...) {
        return dflt;
    }
}

double lin2db(double g) {
    return g > 0.0 ? 20.0 * std::log10(g) : kSilenceDb;
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

// ---- decode pass ------------------------------------------------------------

// One streaming decode of [wstart, wend) downmixed to mono. Aggregates, in a
// single pass: per-x-column min/max/RMS (for waveform + loudness) and per-band
// FFT power (for the spectrum). Column count == image width so each pixel column
// maps to one bucket.
struct Decoded {
    int width = 0;
    std::vector<float> col_min, col_max;  // peak envelope per column, [-1,1]
    std::vector<double> col_rms;          // RMS per column, linear
    std::vector<double> band_pow;         // accumulated power per log band
    double peak_lin = 0.0;                // overall peak |sample|
    double rms_lin = 0.0;                 // overall RMS
    long samples = 0;
    double analyzed_dur = 0.0;
    bool truncated = false;
    bool ok = false;
};

Decoded decode_window(PCM_source* src, double wstart, double wend, int width) {
    Decoded d;
    d.width = width;
    d.col_min.assign(width, 0.0f);
    d.col_max.assign(width, 0.0f);
    std::vector<double> col_sumsq(width, 0.0);
    std::vector<long> col_n(width, 0);
    d.band_pow.assign(kBands, 0.0);

    double dur = wend - wstart;
    if (dur <= 0.0)
        return d;

    std::vector<ReaSample> buf(kFrame);
    std::vector<double> re(kFrame), im(kFrame), window(kFrame);
    for (int i = 0; i < kFrame; i++)
        window[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (kFrame - 1));  // Hann

    const double fmin = 20.0, fmax = kSR / 2.0;
    const double logspan = std::log(fmax / fmin);

    double t = wstart;
    long total = 0;
    double sumsq_all = 0.0;
    const long max_samples = static_cast<long>(kSR) * kMaxSeconds;

    while (t < wend && total < max_samples) {
        PCM_source_transfer_t blk;
        memset(&blk, 0, sizeof(blk));
        blk.time_s = t;
        blk.samplerate = kSR;
        blk.nch = 1;
        blk.length = kFrame;
        blk.samples = buf.data();
        blk.samples_out = 0;
        src->GetSamples(&blk);
        int got = blk.samples_out;
        if (got <= 0)
            break;

        // Per-sample column aggregation (waveform + loudness + overall stats).
        for (int i = 0; i < got; i++) {
            double tt = t + static_cast<double>(i) / kSR;
            if (tt >= wend)
                break;
            float v = static_cast<float>(buf[i]);
            int col = static_cast<int>((tt - wstart) / dur * width);
            col = std::clamp(col, 0, width - 1);
            if (col_n[col] == 0) {
                d.col_min[col] = v;
                d.col_max[col] = v;
            } else {
                d.col_min[col] = std::min(d.col_min[col], v);
                d.col_max[col] = std::max(d.col_max[col], v);
            }
            col_sumsq[col] += static_cast<double>(v) * v;
            col_n[col]++;
            double av = std::fabs(v);
            if (av > d.peak_lin)
                d.peak_lin = av;
            sumsq_all += static_cast<double>(v) * v;
            total++;
        }

        // Per-frame FFT into log-spaced bands (spectrum). Only on full frames.
        if (got >= kFrame) {
            for (int i = 0; i < kFrame; i++) {
                re[i] = static_cast<double>(buf[i]) * window[i];
                im[i] = 0.0;
            }
            dsp::fft(re, im, kFrame);
            for (int k = 1; k < kFrame / 2; k++) {
                double f = static_cast<double>(k) * kSR / kFrame;
                if (f < fmin || f >= fmax)
                    continue;
                double p = re[k] * re[k] + im[k] * im[k];
                int b = static_cast<int>(kBands * std::log(f / fmin) / logspan);
                b = std::clamp(b, 0, kBands - 1);
                d.band_pow[b] += p;
            }
        }

        t += static_cast<double>(got) / kSR;
        if (got < kFrame)
            break;  // end of source
    }

    if (total == 0)
        return d;
    for (int x = 0; x < width; x++)
        d.col_rms.push_back(col_n[x] > 0 ? std::sqrt(col_sumsq[x] / col_n[x]) : 0.0);
    d.rms_lin = std::sqrt(sumsq_all / total);
    d.samples = total;
    d.analyzed_dur = std::min(dur, static_cast<double>(total) / kSR);
    d.truncated = (total >= max_samples) && (d.analyzed_dur < dur - 1e-6);
    d.ok = true;
    return d;
}

// ---- band frequency edges (for the spectrum digest) -------------------------

double band_lo_hz(int b) {
    const double fmin = 20.0, fmax = kSR / 2.0;
    return fmin * std::pow(fmax / fmin, static_cast<double>(b) / kBands);
}

// ---- renderers: each returns a digest and (optionally) draws a Canvas -------

// Theme: dark bg, gray grid, teal signal, light-gray labels.
const uint8_t BG[3] = {20, 23, 28};
const uint8_t GRID[3] = {44, 49, 58};
const uint8_t MID[3] = {70, 78, 92};
const uint8_t SIG[3] = {70, 196, 160};
const uint8_t SIGDIM[3] = {34, 86, 72};
const uint8_t LBL[3] = {150, 160, 175};

// Inner plot rectangle (axes/labels live in the surrounding margins).
struct Plot {
    int x0, y0, x1, y1;
    int w() const {
        return x1 - x0;
    }
    int h() const {
        return y1 - y0;
    }
};

// Format a time in seconds for an axis tick: "0", "1.5s", "12s".
std::string time_label(double s) {
    char buf[24];
    if (s < 10.0)
        snprintf(buf, sizeof(buf), "%.1fs", s);
    else
        snprintf(buf, sizeof(buf), "%.0fs", s);
    return buf;
}

// Format a frequency for an axis tick: "100", "1k", "10k".
std::string hz_label(double hz) {
    char buf[24];
    if (hz >= 1000.0)
        snprintf(buf, sizeof(buf), "%gk", hz / 1000.0);
    else
        snprintf(buf, sizeof(buf), "%g", hz);
    return buf;
}

void draw_frame(Image::Canvas* c, const Plot& p) {
    c->hline(p.x0, p.x1, p.y0, GRID[0], GRID[1], GRID[2]);
    c->hline(p.x0, p.x1, p.y1, GRID[0], GRID[1], GRID[2]);
    c->vline(p.x0, p.y0, p.y1, GRID[0], GRID[1], GRID[2]);
    c->vline(p.x1, p.y0, p.y1, GRID[0], GRID[1], GRID[2]);
}

// dB gridlines + left-margin labels for the level-based plots (spectrum/loudness).
void draw_db_axis(Image::Canvas* c, const Plot& p, std::initializer_list<double> ticks) {
    auto y_of = [&](double db) {
        double f = std::clamp((db - kFloorDb) / (0.0 - kFloorDb), 0.0, 1.0);
        return p.y1 - static_cast<int>(f * p.h());
    };
    for (double g : ticks) {
        int y = y_of(g);
        c->hline(p.x0, p.x1, y, GRID[0], GRID[1], GRID[2]);
        c->draw_text(1, y - 3, std::to_string(static_cast<int>(g)), 1, LBL[0], LBL[1], LBL[2]);
    }
}

// Time ticks along the bottom margin (waveform/loudness).
void draw_time_axis(Image::Canvas* c, const Plot& p, double wstart, double wend) {
    double dur = wend - wstart;
    for (int i = 0; i <= 4; i++) {
        double frac = i / 4.0;
        int x = p.x0 + static_cast<int>(frac * p.w());
        c->vline(x, p.y0, p.y1, GRID[0], GRID[1], GRID[2]);
        std::string lab = time_label(wstart + frac * dur);
        int lx = std::clamp(x - Image::text_width(lab, 1) / 2, 0, c->w - Image::text_width(lab, 1));
        c->draw_text(lx, p.y1 + 3, lab, 1, LBL[0], LBL[1], LBL[2]);
    }
}

nlohmann::json
render_waveform(const Decoded& d, Image::Canvas* c, const Plot& p, double ws, double we) {
    int w = d.width;
    if (c) {
        draw_frame(c, p);
        draw_time_axis(c, p, ws, we);
        int mid = (p.y0 + p.y1) / 2;
        c->hline(p.x0, p.x1, mid, MID[0], MID[1], MID[2]);
        double amp = (p.h() / 2) * 0.95;
        for (int x = 0; x < std::min(w, p.w()); x++) {
            int ytop = mid - static_cast<int>(d.col_max[x] * amp);
            int ybot = mid - static_cast<int>(d.col_min[x] * amp);
            c->vline(p.x0 + x, ytop, ybot, SIG[0], SIG[1], SIG[2]);
        }
    }
    // Digest: overall peak/RMS/clip + a compact per-segment peak envelope.
    int segs = std::min(32, w);
    nlohmann::json env = nlohmann::json::array();
    for (int s = 0; s < segs; s++) {
        double pk = 0.0;
        int x0 = s * w / segs, x1 = (s + 1) * w / segs;
        for (int x = x0; x < x1; x++)
            pk = std::max({pk, std::fabs((double)d.col_min[x]), std::fabs((double)d.col_max[x])});
        env.push_back(lin2db(pk));
    }
    return {{"peak_db", lin2db(d.peak_lin)},
            {"rms_db", lin2db(d.rms_lin)},
            {"clipping", d.peak_lin >= 0.999},
            {"envelope_db", env},
            {"method", "estimated_dsp"},
            {"confidence", 0.9}};
}

nlohmann::json
render_loudness(const Decoded& d, Image::Canvas* c, const Plot& p, double ws, double we) {
    int w = d.width;
    auto y_of = [&](double db) {
        double f = std::clamp((db - kFloorDb) / (0.0 - kFloorDb), 0.0, 1.0);
        return p.y1 - static_cast<int>(f * p.h());
    };
    if (c) {
        draw_frame(c, p);
        draw_db_axis(c, p, {0.0, -12.0, -24.0, -48.0});
        draw_time_axis(c, p, ws, we);
        int prevx = -1, prevy = 0;
        for (int x = 0; x < std::min(w, p.w()); x++) {
            int y = y_of(lin2db(d.col_rms[x]));
            if (prevx >= 0)
                c->line_w(prevx, prevy, p.x0 + x, y, 2, SIG[0], SIG[1], SIG[2]);
            prevx = p.x0 + x;
            prevy = y;
        }
    }
    int segs = std::min(48, w);
    nlohmann::json contour = nlohmann::json::array();
    double mn = 1e9, mx = -1e9;
    for (int s = 0; s < segs; s++) {
        double ss = 0;
        int cnt = 0, x0 = s * w / segs, x1 = (s + 1) * w / segs;
        for (int x = x0; x < x1; x++) {
            ss += d.col_rms[x] * d.col_rms[x];
            cnt++;
        }
        double db = lin2db(cnt > 0 ? std::sqrt(ss / cnt) : 0.0);
        contour.push_back(db);
        if (db > kSilenceDb) {
            mn = std::min(mn, db);
            mx = std::max(mx, db);
        }
    }
    return {{"rms_contour_db", contour},
            {"min_db", mn > 1e8 ? kSilenceDb : mn},
            {"max_db", mx < -1e8 ? kSilenceDb : mx},
            {"mean_db", lin2db(d.rms_lin)},
            {"method", "estimated_dsp"},
            {"confidence", 0.85}};
}

nlohmann::json render_spectrum(const Decoded& d, Image::Canvas* c, const Plot& p) {
    // Convert band power to dB relative to the loudest band (curve shape).
    double maxp = 0.0;
    for (double pw : d.band_pow)
        maxp = std::max(maxp, pw);
    std::vector<double> db(kBands, kFloorDb);
    for (int b = 0; b < kBands; b++)
        if (d.band_pow[b] > 0.0 && maxp > 0.0)
            db[b] = std::clamp(10.0 * std::log10(d.band_pow[b] / maxp), kFloorDb, 0.0);

    if (c) {
        draw_frame(c, p);
        draw_db_axis(c, p, {0.0, -12.0, -24.0, -48.0});
        // Frequency gridlines + labels at decade marks (log x-axis).
        const double fmin = 20.0, fmax = kSR / 2.0, logspan = std::log(fmax / fmin);
        auto x_of_hz = [&](double hz) {
            return p.x0 + static_cast<int>(std::log(hz / fmin) / logspan * p.w());
        };
        for (double f : {100.0, 1000.0, 10000.0}) {
            int x = x_of_hz(f);
            c->vline(x, p.y0, p.y1, GRID[0], GRID[1], GRID[2]);
            std::string lab = hz_label(f);
            c->draw_text(std::clamp(x - Image::text_width(lab, 1) / 2, p.x0, p.x1),
                         p.y1 + 3,
                         lab,
                         1,
                         LBL[0],
                         LBL[1],
                         LBL[2]);
        }
        auto y_of = [&](double v) {
            double f = std::clamp((v - kFloorDb) / (0.0 - kFloorDb), 0.0, 1.0);
            return p.y1 - static_cast<int>(f * p.h());
        };
        // EQ-style curve: faint fill under, bright 2px line through band centers.
        int prevx = -1, prevy = 0;
        for (int b = 0; b < kBands; b++) {
            int xc = p.x0 + static_cast<int>((b + 0.5) * p.w() / kBands);
            int y = y_of(db[b]);
            c->vline(xc, y, p.y1 - 1, SIGDIM[0], SIGDIM[1], SIGDIM[2]);
            if (prevx >= 0)
                c->line_w(prevx, prevy, xc, y, 2, SIG[0], SIG[1], SIG[2]);
            prevx = xc;
            prevy = y;
        }
    }

    // Low/mid/high summary + centroid from the band powers.
    double low = 0, mid = 0, high = 0, tot = 0, cnum = 0;
    nlohmann::json bands = nlohmann::json::array();
    int peak_band = 0;
    double peak_db = kFloorDb;
    for (int b = 0; b < kBands; b++) {
        double lo = band_lo_hz(b), hi = band_lo_hz(b + 1);
        double fc = std::sqrt(lo * hi);
        bands.push_back({{"hz_lo", lo}, {"hz_hi", hi}, {"db", db[b]}});
        double pw = d.band_pow[b];
        if (fc < 250)
            low += pw;
        else if (fc < 4000)
            mid += pw;
        else
            high += pw;
        tot += pw;
        cnum += fc * pw;
        if (db[b] > peak_db) {
            peak_db = db[b];
            peak_band = b;
        }
    }
    const char* dom = (low >= mid && low >= high) ? "low" : (mid >= high ? "mid" : "high");
    return {{"bands", bands},
            {"peak_band_hz", std::sqrt(band_lo_hz(peak_band) * band_lo_hz(peak_band + 1))},
            {"centroid_hz", tot > 0 ? cnum / tot : 0.0},
            {"low", tot > 0 ? low / tot : 0.0},
            {"mid", tot > 0 ? mid / tot : 0.0},
            {"high", tot > 0 ? high / tot : 0.0},
            {"dominant_band", dom},
            {"reference", "db relative to loudest band (curve shape)"},
            {"method", "estimated_dsp"},
            {"confidence", 0.6}};
}

// ---- shared driver ----------------------------------------------------------

// type: 0=spectrum, 1=waveform, 2=loudness. Returns -1 on unknown.
int parse_type(const httplib::Request& req) {
    auto it = req.params.find("type");
    std::string t = it != req.params.end() ? it->second : "spectrum";
    if (t == "spectrum" || t == "eq" || t == "spectrum_curve")
        return 0;
    if (t == "waveform" || t == "wave")
        return 1;
    if (t == "loudness" || t == "contour")
        return 2;
    return -1;
}

nlohmann::json build_visual(PCM_source* src,
                            int type,
                            double start,
                            double end,
                            int width,
                            int height,
                            bool image) {
    bool qn = false;
    double len = GetMediaSourceLength(src, &qn);
    double wstart = std::max(0.0, start);
    double wend = end > 0.0 ? std::min(end, len) : len;
    if (wend <= wstart) {
        wstart = 0.0;
        wend = len;
    }

    // Inner plot rect: leave margins for axis labels (left=dB, bottom=time/Hz).
    Plot plot{22, 4, width - 5, height - 12};
    int plot_w = std::max(1, plot.w());

    Decoded d = decode_window(src, wstart, wend, plot_w);
    if (!d.ok)
        return {{"_bad_request", true}, {"_message", "no decodable audio in window"}};

    Image::Canvas canvas(width, height, BG[0], BG[1], BG[2]);
    Image::Canvas* cp = image ? &canvas : nullptr;

    nlohmann::json digest;
    const char* tname = "spectrum";
    if (type == 0) {
        digest = render_spectrum(d, cp, plot);
        tname = "spectrum";
    } else if (type == 1) {
        digest = render_waveform(d, cp, plot, wstart, wend);
        tname = "waveform";
    } else {
        digest = render_loudness(d, cp, plot, wstart, wend);
        tname = "loudness";
    }

    nlohmann::json out;
    out["type"] = tname;
    out["source"] = source_meta(src);
    out["window"] = {{"start", wstart},
                     {"end", wend},
                     {"analyzed_seconds", d.analyzed_dur},
                     {"truncated", d.truncated}};
    out["digest"] = digest;
    if (image) {
        std::vector<uint8_t> png = Image::encode_png(canvas);
        out["image"] = {{"format", "png"},
                        {"width", width},
                        {"height", height},
                        {"base64", Image::base64(png)}};
    }
    return out;
}

void parse_common(const httplib::Request& req,
                  int& type,
                  double& start,
                  double& end,
                  int& width,
                  int& height,
                  bool& image) {
    type = parse_type(req);
    start = query_double(req, "start", 0.0);
    end = query_double(req, "end", 0.0);
    width = query_int(req, "width", 640, 160, 1024);
    height = query_int(req, "height", 200, 80, 512);
    auto it = req.params.find("image");
    image = !(it != req.params.end() && it->second == "none");
}

}  // namespace

// GET /analysis/item/{index}/visualize
void handle_visualize_item(const httplib::Request& req, httplib::Response& res) {
    int index = 0;
    try {
        index = std::stoi(req.path_params.at("index"));
    } catch (...) {
        json_error(res, 400, "index must be a numeric integer", "BAD_REQUEST");
        return;
    }
    int type, width, height;
    double start, end;
    bool image;
    parse_common(req, type, start, end, width, height, image);
    if (type < 0) {
        json_error(res, 400, "type must be spectrum, waveform, or loudness", "BAD_REQUEST");
        return;
    }

    auto result = Executor::post(
            [index, type, start, end, width, height, image]() -> nlohmann::json {
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
                nlohmann::json a = build_visual(src, type, start, end, width, height, image);
                a["item_index"] = index;
                return a;
            },
            30);
    if (exec_error(res, result))
        return;
    json_ok(res, result);
}

// GET /analysis/file/visualize?path=/abs/file.wav
void handle_visualize_file(const httplib::Request& req, httplib::Response& res) {
    auto pit = req.params.find("path");
    if (pit == req.params.end() || pit->second.empty()) {
        json_error(res, 400, "Missing required query param: path", "BAD_REQUEST");
        return;
    }
    std::string path = pit->second;
    int type, width, height;
    double start, end;
    bool image;
    parse_common(req, type, start, end, width, height, image);
    if (type < 0) {
        json_error(res, 400, "type must be spectrum, waveform, or loudness", "BAD_REQUEST");
        return;
    }

    auto result = Executor::post(
            [path, type, start, end, width, height, image]() -> nlohmann::json {
                PCM_source* src = PCM_Source_CreateFromFile(path.c_str());
                if (!src)
                    return {{"_not_found", true},
                            {"_message", "Could not open audio file: " + path}};
                nlohmann::json a;
                try {
                    a = build_visual(src, type, start, end, width, height, image);
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

}  // namespace ReaClaw::Handlers

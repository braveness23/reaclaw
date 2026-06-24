#pragma once

#include <array>
#include <cmath>
#include <string>

namespace ReaClaw::music {

// Pure, REAPER-free music-theory helpers shared by the musical-probe handler
// (key / pitch). Header-only so they unit-test on their own, like util/dsp.h.

inline const char* kNoteNames[12] =
        {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

struct Note {
    int midi = 0;        // nearest MIDI note number
    std::string name;    // e.g. "A"
    int octave = 0;      // scientific pitch (A4 = MIDI 69)
    double cents = 0.0;  // signed deviation from equal temperament, [-50, +50]
    double hz = 0.0;     // the frequency that was classified
};

// Map a frequency (Hz) to the nearest equal-tempered note, A4 = 440 Hz.
inline Note hz_to_note(double hz) {
    Note n;
    n.hz = hz;
    if (!(hz > 0.0))
        return n;
    double midi_f = 69.0 + 12.0 * std::log2(hz / 440.0);
    int midi = static_cast<int>(std::lround(midi_f));
    n.midi = midi;
    n.cents = (midi_f - midi) * 100.0;
    int pc = ((midi % 12) + 12) % 12;
    n.name = kNoteNames[pc];
    n.octave = midi / 12 - 1;
    return n;
}

// Pearson correlation of two 12-vectors.
inline double pearson12(const std::array<double, 12>& a, const std::array<double, 12>& b) {
    double ma = 0, mb = 0;
    for (int i = 0; i < 12; i++) {
        ma += a[i];
        mb += b[i];
    }
    ma /= 12.0;
    mb /= 12.0;
    double num = 0, da = 0, db = 0;
    for (int i = 0; i < 12; i++) {
        double xa = a[i] - ma, xb = b[i] - mb;
        num += xa * xb;
        da += xa * xa;
        db += xb * xb;
    }
    double den = std::sqrt(da * db);
    return den > 0.0 ? num / den : 0.0;
}

struct Key {
    std::string tonic;       // e.g. "A"
    std::string mode;        // "major" | "minor"
    double correlation = 0;  // best Krumhansl–Schmuckler correlation
    double confidence = 0;   // margin of best over runner-up, clamped [0,1]
    bool ok = false;
};

// Krumhansl–Schmuckler key estimation from a 12-bin chromagram (index 0 = C).
// Correlates the chroma against the major and minor tonal-hierarchy profiles in
// all 12 rotations and returns the best fit, with a confidence taken from how
// far the winner beats the runner-up.
inline Key estimate_key(const std::array<double, 12>& chroma) {
    // Krumhansl & Kessler (1982) profiles.
    static const std::array<double, 12> kMajor = {
            6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
    static const std::array<double, 12> kMinor = {
            6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
    Key key;
    double total = 0;
    for (double v : chroma)
        total += v;
    if (!(total > 0.0))
        return key;

    double best = -2.0, second = -2.0;
    for (int mode = 0; mode < 2; mode++) {
        const auto& prof = mode == 0 ? kMajor : kMinor;
        for (int tonic = 0; tonic < 12; tonic++) {
            std::array<double, 12> rot;
            for (int i = 0; i < 12; i++)
                rot[i] = prof[((i - tonic) % 12 + 12) % 12];
            double c = pearson12(chroma, rot);
            if (c > best) {
                second = best;
                best = c;
                key.tonic = kNoteNames[tonic];
                key.mode = mode == 0 ? "major" : "minor";
                key.correlation = c;
            } else if (c > second) {
                second = c;
            }
        }
    }
    key.ok = true;
    double margin = best - (second > -2.0 ? second : 0.0);
    key.confidence = std::max(0.0, std::min(1.0, margin * 2.5));
    return key;
}

}  // namespace ReaClaw::music

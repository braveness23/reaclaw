#include "util/dsp.h"

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

using ReaClaw::dsp::fft;

namespace {

// Naive O(n^2) DFT for cross-checking the radix-2 FFT.
void naive_dft(const std::vector<double>& in, std::vector<double>& re, std::vector<double>& im) {
    int n = static_cast<int>(in.size());
    re.assign(n, 0.0);
    im.assign(n, 0.0);
    for (int k = 0; k < n; k++)
        for (int t = 0; t < n; t++) {
            double a = -2.0 * M_PI * k * t / n;
            re[k] += in[t] * std::cos(a);
            im[k] += in[t] * std::sin(a);
        }
}

}  // namespace

TEST(Dsp, MatchesNaiveDftRandom) {
    const int n = 64;
    std::vector<double> in(n);
    unsigned seed = 12345;
    for (int i = 0; i < n; i++) {
        seed = seed * 1103515245u + 12345u;
        in[i] = static_cast<double>((seed >> 16) & 0x7FFF) / 16384.0 - 1.0;
    }
    std::vector<double> nre, nim;
    naive_dft(in, nre, nim);

    std::vector<double> re = in, im(n, 0.0);
    fft(re, im, n);

    for (int k = 0; k < n; k++) {
        EXPECT_NEAR(re[k], nre[k], 1e-6) << "bin " << k;
        EXPECT_NEAR(im[k], nim[k], 1e-6) << "bin " << k;
    }
}

TEST(Dsp, PureTonePeaksAtItsBin) {
    const int n = 128;
    const int k0 = 9;
    std::vector<double> re(n), im(n, 0.0);
    for (int i = 0; i < n; i++)
        re[i] = std::cos(2.0 * M_PI * k0 * i / n);
    fft(re, im, n);

    auto mag = [&](int k) {
        return std::sqrt(re[k] * re[k] + im[k] * im[k]);
    };
    double peak = mag(k0);
    for (int k = 1; k < n / 2; k++)
        if (k != k0)
            EXPECT_LT(mag(k), peak * 0.01) << "bin " << k << " leaked";
    EXPECT_NEAR(peak, n / 2.0, 1e-6);  // cosine energy splits into ±k0
}

TEST(Dsp, DcImpulseIsFlat) {
    const int n = 16;
    std::vector<double> re(n, 0.0), im(n, 0.0);
    re[0] = 1.0;  // unit impulse → flat magnitude spectrum
    fft(re, im, n);
    for (int k = 0; k < n; k++) {
        EXPECT_NEAR(re[k], 1.0, 1e-9);
        EXPECT_NEAR(im[k], 0.0, 1e-9);
    }
}

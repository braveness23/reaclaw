#pragma once

#include <cmath>
#include <utility>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ReaClaw::dsp {

// In-place iterative radix-2 Cooley–Tukey FFT (n must be a power of two).
// Shared by the audio-analysis (spectral digest) and visualization (spectrum
// image) handlers so there is one FFT in the tree. Header-only and free of any
// REAPER dependency, so it is unit-testable on its own.
inline void fft(std::vector<double>& re, std::vector<double>& im, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * M_PI / len;
        double wlen_re = std::cos(ang), wlen_im = std::sin(ang);
        for (int i = 0; i < n; i += len) {
            double w_re = 1.0, w_im = 0.0;
            for (int k = 0; k < len / 2; k++) {
                double u_re = re[i + k], u_im = im[i + k];
                double v_re = re[i + k + len / 2] * w_re - im[i + k + len / 2] * w_im;
                double v_im = re[i + k + len / 2] * w_im + im[i + k + len / 2] * w_re;
                re[i + k] = u_re + v_re;
                im[i + k] = u_im + v_im;
                re[i + k + len / 2] = u_re - v_re;
                im[i + k + len / 2] = u_im - v_im;
                double nw_re = w_re * wlen_re - w_im * wlen_im;
                w_im = w_re * wlen_im + w_im * wlen_re;
                w_re = nw_re;
            }
        }
    }
}

}  // namespace ReaClaw::dsp

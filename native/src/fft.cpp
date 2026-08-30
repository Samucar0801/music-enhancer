// SPDX-License-Identifier: Apache-2.0
#include "me/fft.h"

namespace me {

void fftRadix2(std::vector<double>& re, std::vector<double>& im, bool inverse) {
    const size_t n = re.size();
    if (n < 2 || (n & (n - 1)) != 0) return;
    // reordenacao bit-reverse
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = 2 * kPi / (double)len * (inverse ? 1.0 : -1.0);
        double wr = std::cos(ang), wi = std::sin(ang);
        for (size_t i = 0; i < n; i += len) {
            double cr = 1.0, ci = 0.0;
            for (size_t k = 0; k < len / 2; ++k) {
                double ur = re[i + k], ui = im[i + k];
                double vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
                double vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                re[i + k] = ur + vr; im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
                double nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
            }
        }
    }
    if (inverse) for (size_t i = 0; i < n; ++i) { re[i] /= (double)n; im[i] /= (double)n; }
}

void hannWindow(std::vector<double>& w) {
    const size_t n = w.size();
    for (size_t i = 0; i < n; ++i) w[i] = 0.5 * (1.0 - std::cos(2 * kPi * (double)i / (double)(n - 1)));
}

} // namespace me

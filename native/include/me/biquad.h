// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "me/common.h"

namespace me {

// Biquad Transposed Direct Form II (estavel em float, baixo ruido).
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    inline double process(double x) {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
    // |H(e^jw)| em dB - usado para prever a resposta pretendida da cadeia.
    double magnitudeDb(double fs, double f) const {
        double w = 2.0 * kPi * f / fs, c1 = std::cos(w), s1 = std::sin(w);
        double c2 = std::cos(2 * w), s2 = std::sin(2 * w);
        double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        double dr = 1.0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        double n = std::sqrt(nr * nr + ni * ni), d = std::sqrt(dr * dr + di * di);
        return 20.0 * std::log10(std::max(n, 1e-12) / std::max(d, 1e-12));
    }

    static Biquad peaking(double fs, double f0, double q, double gainDb);
    static Biquad lowShelf(double fs, double f0, double q, double gainDb);
    static Biquad highShelf(double fs, double f0, double q, double gainDb);
    static Biquad highPass(double fs, double f0, double q);
    static Biquad lowPass(double fs, double f0, double q);
    static Biquad bandPass(double fs, double f0, double q);
    // Redigitaliza um biquad projetado em fs0 para fs1 (bilinear inversa).
    static Biquad redesign(const Biquad& src, double fs0, double fs1);
};

// Linkwitz-Riley 4a ordem = dois Butterworth 2a ordem em cascata.
struct LR4 {
    Biquad a, b;
    inline double process(double x) { return b.process(a.process(x)); }
    void reset() { a.reset(); b.reset(); }
    static LR4 lowPass(double fs, double f0);
    static LR4 highPass(double fs, double f0);
};

} // namespace me

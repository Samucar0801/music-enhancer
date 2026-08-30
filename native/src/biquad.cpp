// SPDX-License-Identifier: Apache-2.0
#include "me/biquad.h"

namespace me {

static Biquad norm(double b0, double b1, double b2, double a0, double a1, double a2) {
    Biquad q; q.b0 = b0 / a0; q.b1 = b1 / a0; q.b2 = b2 / a0; q.a1 = a1 / a0; q.a2 = a2 / a0;
    return q;
}

Biquad Biquad::peaking(double fs, double f0, double q, double gainDb) {
    if (f0 <= 0 || f0 >= fs * 0.49) { Biquad b; return b; }
    double A = std::pow(10.0, gainDb / 40.0);
    double w0 = 2 * kPi * f0 / fs, c = std::cos(w0), s = std::sin(w0);
    double al = s / (2 * q);
    return norm(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
}

Biquad Biquad::lowShelf(double fs, double f0, double q, double gainDb) {
    if (f0 <= 0 || f0 >= fs * 0.49) { Biquad b; return b; }
    double A = std::pow(10.0, gainDb / 40.0), sa = std::sqrt(A);
    double w0 = 2 * kPi * f0 / fs, c = std::cos(w0), al = std::sin(w0) / (2 * q);
    return norm(A * ((A + 1) - (A - 1) * c + 2 * sa * al),
                2 * A * ((A - 1) - (A + 1) * c),
                A * ((A + 1) - (A - 1) * c - 2 * sa * al),
                (A + 1) + (A - 1) * c + 2 * sa * al,
                -2 * ((A - 1) + (A + 1) * c),
                (A + 1) + (A - 1) * c - 2 * sa * al);
}

Biquad Biquad::highShelf(double fs, double f0, double q, double gainDb) {
    if (f0 <= 0 || f0 >= fs * 0.49) { Biquad b; return b; }
    double A = std::pow(10.0, gainDb / 40.0), sa = std::sqrt(A);
    double w0 = 2 * kPi * f0 / fs, c = std::cos(w0), al = std::sin(w0) / (2 * q);
    return norm(A * ((A + 1) + (A - 1) * c + 2 * sa * al),
                -2 * A * ((A - 1) + (A + 1) * c),
                A * ((A + 1) + (A - 1) * c - 2 * sa * al),
                (A + 1) - (A - 1) * c + 2 * sa * al,
                2 * ((A - 1) - (A + 1) * c),
                (A + 1) - (A - 1) * c - 2 * sa * al);
}

Biquad Biquad::highPass(double fs, double f0, double q) {
    if (f0 <= 0 || f0 >= fs * 0.49) { Biquad b; return b; }
    double w0 = 2 * kPi * f0 / fs, c = std::cos(w0), al = std::sin(w0) / (2 * q);
    return norm((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
}

Biquad Biquad::lowPass(double fs, double f0, double q) {
    if (f0 <= 0 || f0 >= fs * 0.49) { Biquad b; return b; }
    double w0 = 2 * kPi * f0 / fs, c = std::cos(w0), al = std::sin(w0) / (2 * q);
    return norm((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
}

Biquad Biquad::bandPass(double fs, double f0, double q) {
    if (f0 <= 0 || f0 >= fs * 0.49) { Biquad b; return b; }
    double w0 = 2 * kPi * f0 / fs, c = std::cos(w0), al = std::sin(w0) / (2 * q);
    return norm(al, 0, -al, 1 + al, -2 * c, 1 - al);
}

// Bilinear inversa (fs0 -> plano s) seguida de bilinear direta (s -> fs1).
// Exato por construcao quando fs1 == fs0.
Biquad Biquad::redesign(const Biquad& s, double fs0, double fs1) {
    double K0 = 2.0 * fs0;
    double n2 = s.b0 - s.b1 + s.b2;
    double n1 = 2 * K0 * (s.b0 - s.b2);
    double n0 = K0 * K0 * (s.b0 + s.b1 + s.b2);
    double d2 = 1.0 - s.a1 + s.a2;
    double d1 = 2 * K0 * (1.0 - s.a2);
    double d0 = K0 * K0 * (1.0 + s.a1 + s.a2);
    double K1 = 2.0 * fs1, K1s = K1 * K1;
    double B0 = n2 * K1s + n1 * K1 + n0;
    double B1 = -2 * n2 * K1s + 2 * n0;
    double B2 = n2 * K1s - n1 * K1 + n0;
    double A0 = d2 * K1s + d1 * K1 + d0;
    double A1 = -2 * d2 * K1s + 2 * d0;
    double A2 = d2 * K1s - d1 * K1 + d0;
    return norm(B0, B1, B2, A0, A1, A2);
}

LR4 LR4::lowPass(double fs, double f0) {
    LR4 l; l.a = Biquad::lowPass(fs, f0, 0.70710678118654752);
    l.b = Biquad::lowPass(fs, f0, 0.70710678118654752); return l;
}
LR4 LR4::highPass(double fs, double f0) {
    LR4 l; l.a = Biquad::highPass(fs, f0, 0.70710678118654752);
    l.b = Biquad::highPass(fs, f0, 0.70710678118654752); return l;
}

} // namespace me

// SPDX-License-Identifier: Apache-2.0
#include "me/loudness.h"
#include <algorithm>

namespace me {

// Coeficientes normativos BS.1770-4 @ 48 kHz.
static Biquad kPre48() { Biquad b; b.b0=1.53512485958697; b.b1=-2.69169618940638;
    b.b2=1.19839281085285; b.a1=-1.69065929318241; b.a2=0.73248077421585; return b; }
static Biquad kRlb48() { Biquad b; b.b0=1.0; b.b1=-2.0; b.b2=1.0;
    b.a1=-1.99004745483398; b.a2=0.99007225036621; return b; }

LoudnessMeter::LoudnessMeter(int sampleRate, int channels)
    : fs_(sampleRate), ch_(std::max(1, channels)) {
    Biquad pre = (fs_ == 48000) ? kPre48() : Biquad::redesign(kPre48(), 48000.0, (double)fs_);
    Biquad rlb = (fs_ == 48000) ? kRlb48() : Biquad::redesign(kRlb48(), 48000.0, (double)fs_);
    pre_.assign(ch_, pre); rlb_.assign(ch_, rlb);
    stepSamples_  = (size_t)std::llround(fs_ * 0.1);   // 100 ms
    blockSamples_ = stepSamples_ * 4;                  // 400 ms, 75% de sobreposicao
    ring_.assign(4, 0.0);
    reset();
}

void LoudnessMeter::reset() {
    for (auto& f : pre_) f.reset();
    for (auto& f : rlb_) f.reset();
    std::fill(ring_.begin(), ring_.end(), 0.0);
    ringPos_ = 0; subAcc_ = 0; subCount_ = 0; filled_ = 0;
    blockLoud_.clear();
}

void LoudnessMeter::push(const float* x, size_t frames) {
    for (size_t i = 0; i < frames; ++i) {
        double sum = 0;
        for (int c = 0; c < ch_; ++c) {
            double v = (double)x[i * ch_ + c];
            v = pre_[c].process(v);
            v = rlb_[c].process(v);
            sum += v * v;                 // G = 1.0 para L e R
        }
        subAcc_ += sum;
        if (++subCount_ >= stepSamples_) {
            ring_[ringPos_] = subAcc_;
            ringPos_ = (ringPos_ + 1) % 4;
            subAcc_ = 0; subCount_ = 0;
            if (++filled_ >= 4) {
                double tot = ring_[0] + ring_[1] + ring_[2] + ring_[3];
                double ms = tot / (double)blockSamples_;
                blockLoud_.push_back(ms > 0 ? (-0.691 + 10.0 * std::log10(ms)) : -200.0);
            }
        }
    }
}

double LoudnessMeter::integratedLufs() const {
    if (blockLoud_.empty()) return -70.0;
    // gate absoluto -70 LUFS
    std::vector<double> pass;
    for (double l : blockLoud_) if (l > -70.0) pass.push_back(l);
    if (pass.empty()) return -70.0;
    double s = 0;
    for (double l : pass) s += std::pow(10.0, (l + 0.691) / 10.0);
    double meanMs = s / (double)pass.size();
    double rel = -0.691 + 10.0 * std::log10(meanMs) - 10.0;   // gate relativo -10 LU
    std::vector<double> pass2;
    for (double l : pass) if (l > rel) pass2.push_back(l);
    if (pass2.empty()) return -70.0;
    double s2 = 0;
    for (double l : pass2) s2 += std::pow(10.0, (l + 0.691) / 10.0);
    return -0.691 + 10.0 * std::log10(s2 / (double)pass2.size());
}

double LoudnessMeter::loudnessRangeLu() const {
    if (blockLoud_.size() < 10) return 0.0;
    std::vector<double> v;
    for (double l : blockLoud_) if (l > -70.0) v.push_back(l);
    if (v.size() < 10) return 0.0;
    double s = 0;
    for (double l : v) s += std::pow(10.0, (l + 0.691) / 10.0);
    double rel = -0.691 + 10.0 * std::log10(s / (double)v.size()) - 20.0;
    std::vector<double> g;
    for (double l : v) if (l > rel) g.push_back(l);
    if (g.size() < 5) return 0.0;
    std::sort(g.begin(), g.end());
    auto pct = [&](double p) { return g[(size_t)clampd(p * (g.size() - 1), 0, (double)g.size() - 1)]; };
    return pct(0.95) - pct(0.10);
}

double LoudnessMeter::shortTermMaxLufs() const {
    double m = -200; for (double l : blockLoud_) m = std::max(m, l); return m;
}

// -------------------------------------------------------------- true peak
// FIR sinc janelado (Blackman), 4 fases x 12 taps = 48 taps.
static const int kPhases = 4, kTaps = 12;
static std::vector<double> buildPolyphase() {
    std::vector<double> h(kPhases * kTaps);
    const int L = kPhases * kTaps;
    for (int n = 0; n < L; ++n) {
        double t = (double)n - (double)(L - 1) / 2.0;
        double x = t / (double)kPhases;
        double sinc = (std::fabs(x) < 1e-9) ? 1.0 : std::sin(kPi * x) / (kPi * x);
        double w = 0.42 - 0.5 * std::cos(2 * kPi * n / (L - 1)) + 0.08 * std::cos(4 * kPi * n / (L - 1));
        h[n] = sinc * w;
    }
    // normaliza cada fase para ganho DC unitario
    for (int p = 0; p < kPhases; ++p) {
        double s = 0;
        for (int k = 0; k < kTaps; ++k) s += h[k * kPhases + p];
        if (std::fabs(s) > 1e-12) for (int k = 0; k < kTaps; ++k) h[k * kPhases + p] /= s;
    }
    return h;
}
static const std::vector<double>& poly() { static std::vector<double> h = buildPolyphase(); return h; }

TruePeakMeter::TruePeakMeter(int channels) : ch_(std::max(1, channels)) { reset(); }

void TruePeakMeter::reset() {
    hist_.assign(ch_, std::vector<double>(kTaps, 0.0));
    pos_.assign(ch_, 0); peak_ = 0;
}

void TruePeakMeter::push(const float* x, size_t frames) {
    const std::vector<double>& h = poly();
    for (size_t i = 0; i < frames; ++i) {
        for (int c = 0; c < ch_; ++c) {
            std::vector<double>& hb = hist_[c];
            hb[pos_[c]] = (double)x[i * ch_ + c];
            pos_[c] = (pos_[c] + 1) % kTaps;
            peak_ = std::max(peak_, std::fabs((double)x[i * ch_ + c]));
            for (int p = 0; p < kPhases; ++p) {
                double acc = 0;
                for (int k = 0; k < kTaps; ++k) {
                    size_t idx = (pos_[c] + k) % kTaps;   // mais antigo primeiro
                    acc += hb[idx] * h[(kTaps - 1 - k) * kPhases + p];
                }
                peak_ = std::max(peak_, std::fabs(acc));
            }
        }
    }
}

double gainForTargetLufs(double currentLufs, double targetLufs) {
    if (currentLufs <= -70.0) return 0.0;
    return targetLufs - currentLufs;
}

} // namespace me

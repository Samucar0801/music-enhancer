// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "me/common.h"
#include "me/biquad.h"

namespace me {

// Cadeia DSP com estado. Suporta streaming (blocos) e uso offline.
// Latencia = lookahead do limiter (reportada em latencyFrames()).
class Chain {
public:
    void configure(const ChainParams& p, int sampleRate, int channels);
    void reset();
    // in/out podem ser o mesmo ponteiro. Interleaved float.
    void process(const float* in, float* out, size_t frames);
    size_t latencyFrames() const { return lookahead_; }
    const ChainParams& params() const { return p_; }
    // Reescala a intensidade sem reconfigurar tudo (usado pelo Quality Guard).
    void setIntensity(double k);

private:
    ChainParams p_, base_;
    int fs_ = 48000, ch_ = 2;

    std::vector<std::vector<Biquad>> eq_;      // [canal][banda]
    std::vector<Biquad> vocalLift_;
    std::vector<Biquad> deEssBp_, bassBp_, bassHp_;
    std::vector<LR4> xLow_, xMidLo_, xMidHi_, xHigh_;
    std::vector<double> deEssEnv_, transFast_, transSlow_;
    std::vector<double> cLowEnv_, cMidEnv_, cHighEnv_;
    Biquad monoLp_, monoHpL_, monoHpR_;

    // limiter
    size_t lookahead_ = 0;
    std::vector<float> delay_;                 // circular, interleaved
    size_t delayPos_ = 0;
    std::vector<double> gainRing_;
    double limGain_ = 1.0, limRelCoef_ = 0.0;
    double ceilingLin_ = 1.0;

    double preGain_ = 1.0, outGain_ = 1.0;

    double compressBand(double x, double& env, const CompBand& c, double atkCo, double relCo);
    double atkLow_ = 0, relLow_ = 0, atkMid_ = 0, relMid_ = 0, atkHigh_ = 0, relHigh_ = 0;
    double transAtk_ = 0, transRel_ = 0, deEssAtk_ = 0, deEssRel_ = 0;
};

} // namespace me

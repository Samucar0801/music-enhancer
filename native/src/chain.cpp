// SPDX-License-Identifier: Apache-2.0
#include "me/chain.h"
#include <algorithm>
#include <cstring>

namespace me {

static inline double coefFromMs(double ms, double fs) {
    if (ms <= 0) return 0.0;
    return std::exp(-1.0 / (ms * 0.001 * fs));
}

void Chain::configure(const ChainParams& p, int sampleRate, int channels) {
    base_ = p; p_ = p;
    fs_ = sampleRate; ch_ = std::max(1, channels);

    eq_.assign(ch_, {});
    for (int c = 0; c < ch_; ++c) {
        for (const auto& b : p_.eq) {
            switch (b.type) {
                case 1: eq_[c].push_back(Biquad::lowShelf(fs_, b.freq, b.q, b.gainDb)); break;
                case 2: eq_[c].push_back(Biquad::highShelf(fs_, b.freq, b.q, b.gainDb)); break;
                case 3: eq_[c].push_back(Biquad::highPass(fs_, b.freq, b.q)); break;
                default: eq_[c].push_back(Biquad::peaking(fs_, b.freq, b.q, b.gainDb)); break;
            }
        }
    }

    vocalLift_.assign(ch_, Biquad::peaking(fs_, 2400, 1.1, p_.vocalLiftOn ? p_.vocalLiftDb : 0.0));
    deEssBp_.assign(ch_, Biquad::bandPass(fs_, 7000, 1.2));
    bassBp_.assign(ch_, Biquad::bandPass(fs_, p_.bassEnhancerFreq, 1.0));
    bassHp_.assign(ch_, Biquad::highPass(fs_, p_.bassEnhancerFreq * 1.6, 0.707));

    xLow_.assign(ch_, LR4::lowPass(fs_, p_.crossoverLowHz));
    xMidLo_.assign(ch_, LR4::highPass(fs_, p_.crossoverLowHz));
    xMidHi_.assign(ch_, LR4::lowPass(fs_, p_.crossoverHighHz));
    xHigh_.assign(ch_, LR4::highPass(fs_, p_.crossoverHighHz));

    deEssEnv_.assign(ch_, 0.0);
    transFast_.assign(ch_, 0.0); transSlow_.assign(ch_, 0.0);
    cLowEnv_.assign(ch_, 0.0); cMidEnv_.assign(ch_, 0.0); cHighEnv_.assign(ch_, 0.0);

    monoLp_ = Biquad::lowPass(fs_, p_.bassMonoHz, 0.707);
    monoHpL_ = Biquad::highPass(fs_, p_.bassMonoHz, 0.707);
    monoHpR_ = Biquad::highPass(fs_, p_.bassMonoHz, 0.707);

    lookahead_ = (size_t)std::max(1.0, p_.limiterLookaheadMs * 0.001 * fs_);
    delay_.assign(lookahead_ * ch_, 0.0f);
    gainRing_.assign(lookahead_, 1.0);
    delayPos_ = 0; limGain_ = 1.0;
    limRelCoef_ = coefFromMs(p_.limiterReleaseMs, fs_);
    ceilingLin_ = db2lin(p_.ceilingDbtp - std::max(0.0, p_.ispMarginDb));

    atkLow_ = coefFromMs(p_.low.attackMs, fs_);   relLow_ = coefFromMs(p_.low.releaseMs, fs_);
    atkMid_ = coefFromMs(p_.midB.attackMs, fs_);  relMid_ = coefFromMs(p_.midB.releaseMs, fs_);
    atkHigh_ = coefFromMs(p_.high.attackMs, fs_); relHigh_ = coefFromMs(p_.high.releaseMs, fs_);
    transAtk_ = coefFromMs(1.5, fs_); transRel_ = coefFromMs(60.0, fs_);
    deEssAtk_ = coefFromMs(1.0, fs_); deEssRel_ = coefFromMs(40.0, fs_);

    preGain_ = db2lin(p_.preGainDb);
    outGain_ = db2lin(p_.outputGainDb);
}

void Chain::setIntensity(double k) {
    k = clampd(k, 0.0, 1.0);
    ChainParams s = base_;
    s.intensity = k;
    for (auto& b : s.eq) b.gainDb *= k;
    s.vocalLiftDb *= k;
    s.bassEnhancerAmount *= k;
    s.transientAmount *= k;
    s.deEsserAmount *= k;
    s.stereoWidth = 1.0 + (base_.stereoWidth - 1.0) * k;
    s.preGainDb *= k;
    // ratio interpola em direcao a 1:1 (sem compressao)
    s.low.ratio  = 1.0 + (base_.low.ratio - 1.0) * k;
    s.midB.ratio = 1.0 + (base_.midB.ratio - 1.0) * k;
    s.high.ratio = 1.0 + (base_.high.ratio - 1.0) * k;
    // ganho de saida e limiter NAO sao reduzidos: loudness alvo e o teto
    // continuam sendo requisitos, nao "intensidade de efeito".
    ChainParams keep = base_;
    configure(s, fs_, ch_);
    base_ = keep;
    p_.intensity = k;
}

void Chain::reset() {
    for (auto& v : eq_) for (auto& b : v) b.reset();
    for (auto& b : vocalLift_) b.reset();
    for (auto& b : deEssBp_) b.reset();
    for (auto& b : bassBp_) b.reset();
    for (auto& b : bassHp_) b.reset();
    for (auto& f : xLow_) f.reset();
    for (auto& f : xMidLo_) f.reset();
    for (auto& f : xMidHi_) f.reset();
    for (auto& f : xHigh_) f.reset();
    std::fill(deEssEnv_.begin(), deEssEnv_.end(), 0.0);
    std::fill(transFast_.begin(), transFast_.end(), 0.0);
    std::fill(transSlow_.begin(), transSlow_.end(), 0.0);
    std::fill(cLowEnv_.begin(), cLowEnv_.end(), 0.0);
    std::fill(cMidEnv_.begin(), cMidEnv_.end(), 0.0);
    std::fill(cHighEnv_.begin(), cHighEnv_.end(), 0.0);
    monoLp_.reset(); monoHpL_.reset(); monoHpR_.reset();
    std::fill(delay_.begin(), delay_.end(), 0.0f);
    std::fill(gainRing_.begin(), gainRing_.end(), 1.0);
    delayPos_ = 0; limGain_ = 1.0;
}

// Compressor de banda com joelho suave, detector de pico com suavizacao.
double Chain::compressBand(double x, double& env, const CompBand& c, double atkCo, double relCo) {
    double lvl = std::fabs(x);
    env = (lvl > env) ? (atkCo * env + (1 - atkCo) * lvl) : (relCo * env + (1 - relCo) * lvl);
    double envDb = lin2db(env);
    double over = envDb - c.thresholdDb;
    double grDb = 0.0;
    if (over > c.kneeDb * 0.5) {
        grDb = (over - over / c.ratio);
    } else if (over > -c.kneeDb * 0.5) {
        double t = over + c.kneeDb * 0.5;
        grDb = (1.0 - 1.0 / c.ratio) * t * t / (2.0 * c.kneeDb);
    }
    return x * db2lin(c.makeupDb - grDb);
}

void Chain::process(const float* in, float* out, size_t frames) {
    if (!p_.enabled) {
        if (in != out) std::memcpy(out, in, frames * ch_ * sizeof(float));
        return;
    }
    const bool stereo = (ch_ >= 2);
    std::vector<double> smp(ch_);

    for (size_t i = 0; i < frames; ++i) {
        for (int c = 0; c < ch_; ++c) smp[c] = (double)in[i * ch_ + c] * preGain_;

        // ---------------------------------------------------------- EQ
        for (int c = 0; c < ch_; ++c)
            for (auto& b : eq_[c]) smp[c] = b.process(smp[c]);

        // ------------------------------------------------- realce vocal
        if (p_.vocalLiftOn)
            for (int c = 0; c < ch_; ++c) smp[c] = vocalLift_[c].process(smp[c]);

        // ------------------------------------------- exciter de grave
        // Gera 2o/3o harmonico da fundamental e mistura acima do corte.
        // Da sensacao de grave em transdutores que nao reproduzem <150 Hz.
        if (p_.bassEnhancerOn && p_.bassEnhancerAmount > 0.001) {
            for (int c = 0; c < ch_; ++c) {
                double b = bassBp_[c].process(smp[c]);
                double h = std::tanh(b * 3.0);          // gera harmonicos impares
                h += 0.5 * (b * b * (b >= 0 ? 1.0 : -1.0)) * 2.0;  // componente par
                h = bassHp_[c].process(h);
                smp[c] += h * p_.bassEnhancerAmount * 0.35;
            }
        }

        // ------------------------------------------------- de-esser
        if (p_.deEsserOn && p_.deEsserAmount > 0.001) {
            for (int c = 0; c < ch_; ++c) {
                double s = deEssBp_[c].process(smp[c]);
                double lvl = std::fabs(s);
                deEssEnv_[c] = (lvl > deEssEnv_[c])
                    ? (deEssAtk_ * deEssEnv_[c] + (1 - deEssAtk_) * lvl)
                    : (deEssRel_ * deEssEnv_[c] + (1 - deEssRel_) * lvl);
                double over = lin2db(deEssEnv_[c]) - p_.deEsserThresholdDb;
                if (over > 0) {
                    double red = db2lin(-over * p_.deEsserAmount) - 1.0;
                    smp[c] += s * red;                  // reduz so a banda sibilante
                }
            }
        }

        // --------------------------------------- compressor multibanda
        if (p_.multibandOn) {
            for (int c = 0; c < ch_; ++c) {
                double lo = xLow_[c].process(smp[c]);
                double rest = xMidLo_[c].process(smp[c]);
                double mid = xMidHi_[c].process(rest);
                double hi = xHigh_[c].process(rest);
                lo  = compressBand(lo,  cLowEnv_[c],  p_.low,  atkLow_,  relLow_);
                mid = compressBand(mid, cMidEnv_[c],  p_.midB, atkMid_,  relMid_);
                hi  = compressBand(hi,  cHighEnv_[c], p_.high, atkHigh_, relHigh_);
                smp[c] = lo + mid + hi;
            }
        }

        // ---------------------------------------- realce de transientes
        if (p_.transientOn && std::fabs(p_.transientAmount) > 0.001) {
            for (int c = 0; c < ch_; ++c) {
                double lvl = std::fabs(smp[c]);
                transFast_[c] = (lvl > transFast_[c])
                    ? lvl : (transAtk_ * transFast_[c] + (1 - transAtk_) * lvl);
                transSlow_[c] = transRel_ * transSlow_[c] + (1 - transRel_) * lvl;
                double diff = transFast_[c] - transSlow_[c];
                double g = 1.0 + clampd(diff * 6.0, 0.0, 1.0) * p_.transientAmount;
                smp[c] *= g;
            }
        }

        // ------------------------------------------------ estereo M/S
        if (stereo) {
            double L = smp[0], R = smp[1];
            if (p_.bassMonoOn) {
                double sum = 0.5 * (L + R);
                double lowMono = monoLp_.process(sum);
                L = monoHpL_.process(L) + lowMono;
                R = monoHpR_.process(R) + lowMono;
            }
            if (std::fabs(p_.stereoWidth - 1.0) > 0.001) {
                double m = 0.5 * (L + R), s = 0.5 * (L - R) * p_.stereoWidth;
                L = m + s; R = m - s;
            }
            smp[0] = L; smp[1] = R;
        }

        // ------------------------------------------- ganho de saida
        for (int c = 0; c < ch_; ++c) smp[c] *= outGain_;

        // -------------------------------- limiter com lookahead (peak)
        if (p_.limiterOn) {
            double pk = 0;
            for (int c = 0; c < ch_; ++c) pk = std::max(pk, std::fabs(smp[c]));
            double need = (pk > ceilingLin_) ? (ceilingLin_ / pk) : 1.0;
            gainRing_[delayPos_] = need;

            // ganho aplicado = minimo da janela de lookahead (ataque antecipado)
            double target = 1.0;
            for (size_t k = 0; k < lookahead_; ++k) target = std::min(target, gainRing_[k]);
            limGain_ = (target < limGain_) ? target
                     : (limRelCoef_ * limGain_ + (1 - limRelCoef_) * target);

            for (int c = 0; c < ch_; ++c) {
                float delayed = delay_[delayPos_ * ch_ + c];
                delay_[delayPos_ * ch_ + c] = (float)smp[c];
                double y = (double)delayed * limGain_;
                // clipper suave final: rede de seguranca, nunca deve atuar
                if (y > ceilingLin_) y = ceilingLin_ * std::tanh(y / ceilingLin_);
                else if (y < -ceilingLin_) y = -ceilingLin_ * std::tanh(-y / ceilingLin_);
                out[i * ch_ + c] = (float)y;
            }
            delayPos_ = (delayPos_ + 1) % lookahead_;
        } else {
            for (int c = 0; c < ch_; ++c) out[i * ch_ + c] = (float)smp[c];
        }
    }
}

} // namespace me

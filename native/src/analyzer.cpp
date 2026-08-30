// SPDX-License-Identifier: Apache-2.0
#include "me/analyzer.h"
#include "me/loudness.h"
#include "me/fft.h"
#include "me/biquad.h"
#include <algorithm>
#include <cstring>

namespace me {

static const int kFft = 4096;
static const int kHop = 2048;

struct BandDef { double lo, hi; };
static const BandDef kBands[6] = {
    {20, 60}, {60, 120}, {120, 400}, {400, 2000}, {2000, 6000}, {6000, 16000}
};

Analysis analyze(const float* x, size_t frames, int fs, int ch) {
    Analysis a;
    if (!x || frames == 0 || fs <= 0 || ch <= 0) {
        a.notes.push_back("entrada invalida");
        return a;
    }
    a.valid = true; a.sampleRate = fs; a.channels = ch;
    a.durationSec = (double)frames / (double)fs;

    // ---------------------------------------------- loudness / peak / RMS
    LoudnessMeter lm(fs, ch);
    TruePeakMeter tp(ch);
    const size_t kChunk = 8192;
    for (size_t off = 0; off < frames; off += kChunk) {
        size_t n = std::min(kChunk, frames - off);
        lm.push(x + off * ch, n);
        tp.push(x + off * ch, n);
    }
    a.integratedLufs = lm.integratedLufs();
    a.loudnessRangeLu = lm.loudnessRangeLu();
    a.truePeakDbtp = tp.truePeakDbtp();

    double sumSq = 0, peak = 0, sumL = 0, sumR = 0, sumLR = 0, sumL2 = 0, sumR2 = 0;
    double sumMid2 = 0, sumSide2 = 0;
    int clipRun = 0, clipRuns = 0; size_t clipped = 0;
    const double kClipTh = 0.9921;   // ~ -0.07 dBFS
    for (size_t i = 0; i < frames; ++i) {
        double l = x[i * ch], r = (ch > 1) ? x[i * ch + 1] : l;
        double mono = 0.5 * (l + r);
        sumSq += mono * mono;
        double amax = std::max(std::fabs(l), std::fabs(r));
        peak = std::max(peak, amax);
        sumL += l * l; sumR += r * r;
        sumLR += l * r; sumL2 += l * l; sumR2 += r * r;
        double m = 0.5 * (l + r), s = 0.5 * (l - r);
        sumMid2 += m * m; sumSide2 += s * s;
        if (amax >= kClipTh) { clipped++; clipRun++; }
        else { if (clipRun >= 3) clipRuns++; clipRun = 0; }
    }
    if (clipRun >= 3) clipRuns++;
    a.samplePeakDbfs = lin2db(peak);
    a.rmsDbfs = lin2db(std::sqrt(sumSq / (double)frames));
    a.crestFactorDb = a.samplePeakDbfs - a.rmsDbfs;
    a.clippedRuns = clipRuns;
    a.clippedRatio = (double)clipped / (double)frames;
    if (ch > 1) {
        double den = std::sqrt(sumL2 * sumR2);
        a.stereoCorrelation = den > 1e-12 ? clampd(sumLR / den, -1.0, 1.0) : 1.0;
        a.sideMidRatioDb = 10.0 * std::log10(std::max(sumSide2, 1e-15) / std::max(sumMid2, 1e-15));
        a.channelBalanceDb = 10.0 * std::log10(std::max(sumR, 1e-15) / std::max(sumL, 1e-15));
    } else {
        a.stereoCorrelation = 1.0; a.sideMidRatioDb = -120; a.channelBalanceDb = 0;
    }

    // ------------------------------------------------------------ espectro
    std::vector<double> win(kFft); hannWindow(win);
    std::vector<double> avgMag(kFft / 2 + 1, 0.0);
    std::vector<double> re(kFft), im(kFft);
    std::vector<double> flux;               // fluxo espectral (transientes)
    std::vector<double> prevMag(kFft / 2 + 1, 0.0);
    std::vector<double> hfFlux;             // fluxo apenas em HF (percussao)
    size_t nFrames = 0;
    double winPow = 0; for (double w : win) winPow += w * w;
    winPow /= (double)kFft;

    for (size_t off = 0; off + kFft <= frames; off += kHop) {
        for (int i = 0; i < kFft; ++i) {
            double v = 0;
            for (int c = 0; c < ch; ++c) v += x[(off + i) * ch + c];
            v /= (double)ch;
            re[i] = v * win[i]; im[i] = 0;
        }
        fftRadix2(re, im, false);
        double f = 0, hf = 0;
        for (int k = 0; k <= kFft / 2; ++k) {
            double mag = std::sqrt(re[k] * re[k] + im[k] * im[k]) / ((double)kFft * 0.5);
            avgMag[k] += mag * mag;
            double d = mag - prevMag[k];
            if (d > 0) { f += d; if (k * (double)fs / kFft > 3000.0) hf += d; }
            prevMag[k] = mag;
        }
        flux.push_back(f); hfFlux.push_back(hf);
        nFrames++;
        if (nFrames > 20000) break;         // teto de seguranca
    }
    if (nFrames == 0) {
        a.notes.push_back("arquivo curto demais para analise espectral");
        return a;
    }
    for (auto& v : avgMag) v /= (double)nFrames * winPow;

    auto binHz = [&](int k) { return (double)k * fs / (double)kFft; };
    auto bandPower = [&](double lo, double hi) {
        double s = 0; int k0 = (int)std::ceil(lo * kFft / fs), k1 = (int)std::floor(hi * kFft / fs);
        k0 = std::max(k0, 1); k1 = std::min(k1, kFft / 2);
        for (int k = k0; k <= k1; ++k) s += avgMag[k];
        return s;
    };
    double* bp[6] = { &a.bands.sub, &a.bands.bass, &a.bands.lowMid,
                      &a.bands.mid, &a.bands.presence, &a.bands.brilliance };
    for (int b = 0; b < 6; ++b) *bp[b] = 10.0 * std::log10(std::max(bandPower(kBands[b].lo, kBands[b].hi), 1e-15));

    double total = 0, weighted = 0, geo = 0, arith = 0; int cnt = 0;
    for (int k = 1; k <= kFft / 2; ++k) {
        double p = avgMag[k];
        total += p; weighted += p * binHz(k);
        if (binHz(k) >= 50 && binHz(k) <= 16000) {
            geo += std::log(std::max(p, 1e-20)); arith += p; cnt++;
        }
    }
    a.spectralCentroidHz = total > 0 ? weighted / total : 0;
    a.spectralFlatness = (cnt > 0 && arith > 0)
        ? clampd(std::exp(geo / cnt) / (arith / cnt), 0.0, 1.0) : 0.0;
    double acc = 0;
    for (int k = 1; k <= kFft / 2; ++k) { acc += avgMag[k]; if (acc >= 0.85 * total) { a.spectralRolloff85Hz = binHz(k); break; } }

    // corte de banda (limite tipico de MP3): ultimo bin acima de pico-45 dB
    double pk = 0; for (int k = 1; k <= kFft / 2; ++k) pk = std::max(pk, avgMag[k]);
    double th = pk * std::pow(10.0, -45.0 / 10.0);
    a.hfCutoffHz = binHz(kFft / 2);
    for (int k = kFft / 2; k >= 1; --k) {
        double sm = 0; int n = 0;
        for (int j = std::max(1, k - 3); j <= std::min(kFft / 2, k + 3); ++j) { sm += avgMag[j]; n++; }
        if (n && sm / n > th) { a.hfCutoffHz = binHz(k); break; }
    }

    // ------------------------------------------------------ BPM (autocorr)
    if (flux.size() > 64) {
        double mean = 0; for (double v : flux) mean += v; mean /= flux.size();
        std::vector<double> env(flux.size());
        for (size_t i = 0; i < flux.size(); ++i) env[i] = std::max(0.0, flux[i] - mean);
        double hopSec = (double)kHop / fs;
        int lagMin = (int)std::floor(60.0 / 200.0 / hopSec);
        int lagMax = (int)std::ceil(60.0 / 60.0 / hopSec);
        lagMin = std::max(lagMin, 1); lagMax = std::min<int>(lagMax, (int)env.size() / 2);
        double best = 0; int bestLag = 0;
        for (int lag = lagMin; lag <= lagMax; ++lag) {
            double s = 0; for (size_t i = 0; i + lag < env.size(); ++i) s += env[i] * env[i + lag];
            s /= (double)(env.size() - lag);
            if (s > best) { best = s; bestLag = lag; }
        }
        if (bestLag > 0) {
            a.bpm = 60.0 / (bestLag * hopSec);
            double e0 = 0; for (double v : env) e0 += v * v; e0 /= env.size();
            a.bpmConfidence = e0 > 0 ? clampd(best / e0, 0.0, 1.0) : 0.0;
        }
    }

    // ------------------------------------------- heuristicas de conteudo
    // Voz: energia media (300-3500) forte, correlacionada ao mid, com
    // variabilidade espectral compativel com canto. Probabilistico.
    double pMid = bandPower(300, 3500), pAll = bandPower(20, 16000);
    double midRatio = pAll > 0 ? pMid / pAll : 0;
    double fluxVar = 0, fluxMean = 0;
    for (double v : flux) { fluxMean += v; }
    fluxMean /= flux.size();
    for (double v : flux) { fluxVar += (v - fluxMean) * (v - fluxMean); }
    fluxVar = std::sqrt(fluxVar / flux.size());
    double fluxCv = fluxMean > 0 ? fluxVar / fluxMean : 0;
    double centroidOk = (a.spectralCentroidHz > 600 && a.spectralCentroidHz < 4500) ? 1.0 : 0.4;
    a.vocalProbability = clampd(0.55 * clampd(midRatio * 2.2, 0, 1)
                              + 0.25 * centroidOk
                              + 0.20 * clampd(fluxCv, 0, 1), 0.0, 1.0);

    // Percussao: picos fortes de fluxo em HF + crest alto na banda 60-120
    double hfMean = 0; for (double v : hfFlux) hfMean += v; hfMean /= hfFlux.size();
    double hfPeak = 0; for (double v : hfFlux) hfPeak = std::max(hfPeak, v);
    double hfRatio = hfMean > 0 ? clampd(hfPeak / (hfMean * 8.0), 0.0, 1.0) : 0.0;
    a.percussionProbability = clampd(0.6 * hfRatio + 0.4 * clampd((a.crestFactorDb - 6.0) / 12.0, 0, 1), 0, 1);

    double pBass = bandPower(30, 160);
    a.bassPresence = pAll > 0 ? clampd((pBass / pAll) * 3.0, 0.0, 1.0) : 0.0;

    // Dinamica: combina crest factor e LRA
    a.dynamicsScore = clampd(0.5 * clampd((a.crestFactorDb - 5.0) / 13.0, 0, 1)
                           + 0.5 * clampd(a.loudnessRangeLu / 12.0, 0, 1), 0, 1);

    // --------------------------------------------- palpite de estilo
    struct Cand { const char* name; double score; };
    std::vector<Cand> cands;
    double lowE = bandPower(20, 120) / std::max(pAll, 1e-15);
    double hiE  = bandPower(6000, 16000) / std::max(pAll, 1e-15);
    cands.push_back({ "hino / musica cristica congregacional",
        0.45 * a.vocalProbability + 0.25 * (a.bpm > 0 && a.bpm < 100 ? 1.0 : 0.3)
      + 0.15 * (1.0 - clampd(lowE * 4, 0, 1)) + 0.15 * a.dynamicsScore });
    cands.push_back({ "voz + violao / acustico",
        0.40 * a.vocalProbability + 0.30 * (1.0 - a.percussionProbability)
      + 0.30 * (1.0 - clampd(lowE * 4, 0, 1)) });
    cands.push_back({ "pop / rock com banda completa",
        0.35 * a.percussionProbability + 0.30 * a.vocalProbability
      + 0.35 * clampd(lowE * 3, 0, 1) });
    cands.push_back({ "eletronica / dance",
        0.40 * clampd(lowE * 4, 0, 1) + 0.25 * (a.bpm > 110 ? 1.0 : 0.2)
      + 0.20 * (1.0 - a.dynamicsScore) + 0.15 * clampd(hiE * 6, 0, 1) });
    cands.push_back({ "instrumental",
        0.60 * (1.0 - a.vocalProbability) + 0.40 * a.dynamicsScore });
    std::sort(cands.begin(), cands.end(), [](const Cand& p, const Cand& q) { return p.score > q.score; });
    a.genreGuess = cands[0].name;
    double sum = 0; for (auto& c : cands) sum += std::max(c.score, 0.0);
    a.genreConfidence = sum > 0 ? clampd(cands[0].score / sum, 0.0, 1.0) : 0.0;

    // ------------------------------------------------------------ notas
    if (a.clippedRuns > 0)
        a.notes.push_back("clipping detectado na fonte: " + std::to_string(a.clippedRuns) + " sequencias");
    if (a.hfCutoffHz < 16500 && a.hfCutoffHz > 0)
        a.notes.push_back("banda util ate ~" + std::to_string((int)(a.hfCutoffHz / 1000)) + " kHz (fonte comprimida)");
    if (a.crestFactorDb < 8.0)
        a.notes.push_back("fonte muito comprimida (crest " + std::to_string((int)a.crestFactorDb) + " dB)");
    if (ch > 1 && a.stereoCorrelation < 0.0)
        a.notes.push_back("correlacao estereo negativa: risco de cancelamento em mono");
    if (ch > 1 && std::fabs(a.channelBalanceDb) > 1.5)
        a.notes.push_back("desequilibrio entre canais");
    return a;
}

} // namespace me

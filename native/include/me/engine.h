// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "me/common.h"
#include "me/chain.h"
#include "me/decision.h"

namespace me {

// ---------------------------------------------------------- tempo real
// Analisa continuamente uma janela deslizante e reconfigura a cadeia sem
// clicks. Nao faz IA. Latencia = lookahead do limiter.
class RealtimeEngine {
public:
    void start(int sampleRate, int channels, Route route, Preset preset,
               LoudnessMode lm, const UserMacros& macros);
    // Injeta uma analise previa (do cache) para decidir logo no 1o bloco.
    void primeWithAnalysis(const Analysis& a);
    void setRoute(Route r);
    void setBypass(bool on);
    // Mistura A/B: 0.0 = original, 1.0 = aprimorado. O caminho seco e
    // ATRASADO pela latencia da cadeia antes de somar, senao as duas copias
    // do mesmo sinal se somam desalinhadas e viram filtro pente.
    void setMix(double wet);
    double mix() const { return mix_; }
    void setMacros(const UserMacros& m);
    void process(const float* in, float* out, size_t frames);
    size_t latencyFrames() const { return chain_.latencyFrames(); }
    const ChainParams& params() const { return chain_.params(); }
    bool bypassed() const { return bypass_; }
private:
    void rebuild();
    void captureDry(const float* in, size_t frames);
    Chain chain_;
    DecisionInput in_;
    bool bypass_ = false, configured_ = false;
    std::vector<float> window_;      // janela para analise progressiva
    size_t windowFill_ = 0;
    double mix_ = 1.0;
    std::vector<float> dry_;         // linha de atraso do caminho seco
    std::vector<float> dryOut_;
    size_t dryPos_ = 0, dryFrames_ = 0;
    int fs_ = 48000, ch_ = 2;
};

// -------------------------------------------------- offline / qualidade
struct OfflineOptions {
    Route route = Route::Headphone;
    Preset preset = Preset::Auto;
    LoudnessMode loudnessMode = LoudnessMode::Normalized;
    UserMacros macros;
    double targetLufs = -14.0;
    bool qualityGuard = true;
    int maxGuardIterations = 4;
    // Tolerancias do Quality Guard
    double maxCrestLossDb = 3.5;
    double maxBandChangeDb = 4.0;   // desvio em relacao ao PLANEJADO
    double truePeakToleranceDb = 0.15;
};

// Processa o arquivo inteiro em duas passagens (analise -> decisao ->
// processamento -> reanalise -> Quality Guard). Retorna PCM aprimorado.
ProcessResult processOffline(const float* in, size_t frames, int sampleRate,
                             int channels, const OfflineOptions& opt,
                             std::vector<float>& outPcm,
                             bool (*cancelled)(void*) = nullptr, void* cancelCtx = nullptr);

} // namespace me

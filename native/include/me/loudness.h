// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "me/common.h"
#include "me/biquad.h"

namespace me {

// Medidor de loudness ITU-R BS.1770-4 / EBU R128.
// Filtros K derivados dos coeficientes normativos de 48 kHz e redigitalizados
// para a taxa alvo por transformada bilinear inversa (exato em 48 kHz).
class LoudnessMeter {
public:
    LoudnessMeter(int sampleRate, int channels);
    void reset();
    // interleaved float, frames = amostras por canal
    void push(const float* interleaved, size_t frames);
    double integratedLufs() const;   // -70 se nao houver conteudo acima do gate
    double loudnessRangeLu() const;  // LRA aproximada (percentis 10/95)
    double shortTermMaxLufs() const;
private:
    int fs_, ch_;
    std::vector<Biquad> pre_, rlb_;
    std::vector<double> blockAcc_;   // soma de quadrados ponderada, por bloco parcial
    std::vector<double> blockLoud_;  // niveis dos blocos de 400 ms
    size_t blockSamples_ = 0, stepSamples_ = 0, filled_ = 0;
    std::vector<double> ring_;       // soma de quadrados por sub-bloco de 100 ms
    size_t ringPos_ = 0;
    double subAcc_ = 0; size_t subCount_ = 0;
};

// True peak por sobreamostragem 4x (FIR sinc-Kaiser polifasico).
class TruePeakMeter {
public:
    explicit TruePeakMeter(int channels);
    void reset();
    void push(const float* interleaved, size_t frames);
    double truePeakDbtp() const { return lin2db(peak_); }
    double truePeakLinear() const { return peak_; }
private:
    int ch_;
    double peak_ = 0;
    std::vector<std::vector<double>> hist_; // por canal
    std::vector<size_t> pos_;
};

// Ganho necessario (dB) para levar de lufsAtual ao alvo.
double gainForTargetLufs(double currentLufs, double targetLufs);

} // namespace me

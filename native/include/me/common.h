// MusicEnhancer - nucleo DSP portatil (Android/NDK + host de testes)
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <cmath>

namespace me {

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------- rotas
enum class Route { Headphone = 0, Bluetooth = 1, Speaker = 2, Car = 3 };

// ---------------------------------------------------------------- presets
enum class Preset {
    Auto = 0, MaxQualityAI = 1, Headphone = 2, Hymns = 3, VoiceGuitar = 4,
    DrumImpact = 5, Instrumental = 6, Loudness = 7, Balanced = 8, Bypass = 9
};

// ------------------------------------------------------- modo de loudness
enum class LoudnessMode { Original = 0, Normalized = 1, MaxSafe = 2 };

// ---------------------------------------------------------------- analise
struct BandEnergy {                 // dBFS medios por faixa
    double sub = -120;              // 20-60 Hz
    double bass = -120;             // 60-120 Hz
    double lowMid = -120;           // 120-400 Hz
    double mid = -120;              // 400-2000 Hz
    double presence = -120;         // 2000-6000 Hz
    double brilliance = -120;       // 6000-16000 Hz
};

struct Analysis {
    bool valid = false;
    int   sampleRate = 0;
    int   channels = 0;
    double durationSec = 0;

    double integratedLufs = -70;    // ITU-R BS.1770-4 com gating
    double loudnessRangeLu = 0;     // EBU R128 LRA (aproximada)
    double truePeakDbtp = -120;     // 4x oversampling
    double samplePeakDbfs = -120;
    double rmsDbfs = -120;
    double crestFactorDb = 0;

    BandEnergy bands;
    double spectralCentroidHz = 0;
    double spectralRolloff85Hz = 0;
    double spectralFlatness = 0;    // 0 = tonal, 1 = ruido
    double hfCutoffHz = 0;          // banda util detectada (limite do MP3)

    double stereoCorrelation = 1.0; // -1..1
    double sideMidRatioDb = -120;
    double channelBalanceDb = 0;    // +: direito mais alto

    int    clippedRuns = 0;         // sequencias de amostras no teto
    double clippedRatio = 0;

    double bpm = 0;
    double bpmConfidence = 0;
    double vocalProbability = 0;
    double percussionProbability = 0;
    double bassPresence = 0;
    double dynamicsScore = 0;       // 0 = esmagado, 1 = dinamico

    std::string genreGuess = "indeterminado";
    double genreConfidence = 0;
    std::vector<std::string> notes;
};

// ---------------------------------------------- parametros da cadeia DSP
struct EqBand { double freq = 1000; double q = 1.0; double gainDb = 0; int type = 0; };
// type: 0 = peaking, 1 = low shelf, 2 = high shelf, 3 = high pass

struct CompBand {
    double thresholdDb = -24; double ratio = 2.0; double attackMs = 15;
    double releaseMs = 180;   double kneeDb = 6;  double makeupDb = 0;
};

struct ChainParams {
    bool   enabled = true;
    double intensity = 1.0;         // 0..1, reduzido pelo Quality Guard

    std::vector<EqBand> eq;

    bool   bassEnhancerOn = false;
    double bassEnhancerAmount = 0;  // 0..1
    double bassEnhancerFreq = 70;

    bool   multibandOn = false;
    CompBand low, midB, high;
    double  crossoverLowHz = 200;
    double  crossoverHighHz = 3000;

    bool   transientOn = false;
    double transientAmount = 0;     // -1..1

    bool   deEsserOn = false;
    double deEsserThresholdDb = -22;
    double deEsserAmount = 0;

    bool   vocalLiftOn = false;
    double vocalLiftDb = 0;

    double stereoWidth = 1.0;       // 1 = neutro
    bool   bassMonoOn = false;
    double bassMonoHz = 120;

    double preGainDb = 0;
    double outputGainDb = 0;

    bool   limiterOn = true;
    double ceilingDbtp = -1.0;
    // Margem para picos entre amostras. O limiter opera sobre amostras, mas
    // o DAC (e o codec Bluetooth) reconstroi o sinal continuo, que pode
    // ultrapassar o pico amostrado. O caminho offline mede e ajusta este
    // valor; o tempo real usa a margem fixa por seguranca.
    double ispMarginDb = 0.8;
    double limiterLookaheadMs = 2.5;
    double limiterReleaseMs = 120;

    std::string decisionLog;
};

// ------------------------------------------------------ resultado final
struct GuardReport {
    int    iterations = 1;
    double finalIntensity = 1.0;
    bool   revertedToOriginal = false;
    std::vector<std::string> violations;
};

struct ProcessResult {
    Analysis before;
    Analysis after;
    ChainParams params;
    GuardReport guard;
    std::vector<std::string> improvements;
    double processingSeconds = 0;
};

// ------------------------------------------------------------ utilitarios
inline double db2lin(double db) { return std::pow(10.0, db / 20.0); }
inline double lin2db(double lin) { return 20.0 * std::log10(std::max(lin, 1e-12)); }
inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

} // namespace me

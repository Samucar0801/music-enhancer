// SPDX-License-Identifier: Apache-2.0
#include "me/decision.h"
#include "me/loudness.h"
#include <algorithm>
#include <sstream>

namespace me {

// Alvos espectrais relativos (dB em relacao a banda media 400-2000 Hz).
// Nao sao "curvas de EQ" fixas: sao referencias contra as quais medimos o
// desvio da faixa, e cada correcao e limitada e escalada pela confianca.
struct Target { double sub, bass, lowMid, presence, brilliance; };

static Target targetFor(Route r, const Analysis& a) {
    Target t{ -6.0, -2.0, -1.0, -3.0, -9.0 };
    switch (r) {
        case Route::Headphone:
        case Route::Bluetooth: t = { -6.0, -2.0, -1.0, -3.0, -9.0 }; break;
        // Alto-falante de celular nao reproduz <300 Hz de forma util:
        // pedir grave ali so gasta headroom e gera distorcao.
        case Route::Speaker:   t = { -20.0, -12.0, -3.0, -1.0, -7.0 }; break;
        case Route::Car:       t = { -4.0, -1.0, -1.0, -4.0, -10.0 }; break;
    }
    // Conteudo com pouca percussao aceita menos enfase de grave.
    if (a.percussionProbability < 0.3) { t.sub -= 1.5; t.bass -= 1.0; }
    return t;
}

static double limitedCorrection(double measuredDb, double targetDb, double maxBoost, double maxCut) {
    double delta = targetDb - measuredDb;
    return clampd(delta, -maxCut, maxBoost);
}

ChainParams decide(const DecisionInput& in) {
    const Analysis& a = in.analysis;
    ChainParams p;
    std::ostringstream log;

    if (in.preset == Preset::Bypass || !a.valid) {
        p.enabled = false;
        p.limiterOn = false;
        log << "bypass solicitado ou analise invalida\n";
        p.decisionLog = log.str();
        return p;
    }

    const double fs = (double)a.sampleRate;
    const Target tgt = targetFor(in.route, a);
    const double ref = a.bands.mid;   // referencia = banda media

    // Confianca global: fontes muito ruins ou muito curtas recebem menos acao.
    double conf = 1.0;
    if (a.durationSec < 5.0) conf *= 0.5;
    if (a.hfCutoffHz > 0 && a.hfCutoffHz < 12000) conf *= 0.85;
    log << "rota=" << (int)in.route << " conf=" << conf << "\n";

    // ------------------------------------------------------------- EQ
    // Correcoes limitadas a +4.5 / -6 dB. Nunca "bass +10".
    auto add = [&](double f, double q, double g, int type, const char* why) {
        if (std::fabs(g) < 0.25) return;
        p.eq.push_back({ f, q, g, type });
        log << "  eq " << why << " " << f << "Hz " << (g > 0 ? "+" : "") << g << "dB\n";
    };

    double gSub  = limitedCorrection(a.bands.sub - ref, tgt.sub, 3.0, 6.0) * conf;
    double gBass = limitedCorrection(a.bands.bass - ref, tgt.bass, 4.0, 6.0) * conf;
    double gLowM = limitedCorrection(a.bands.lowMid - ref, tgt.lowMid, 2.5, 5.0) * conf;
    double gPres = limitedCorrection(a.bands.presence - ref, tgt.presence, 4.0, 5.0) * conf;
    double gBril = limitedCorrection(a.bands.brilliance - ref, tgt.brilliance, 4.5, 6.0) * conf;

    // Regra: se ja ha grave forte, nao empurrar mais. Se ha clipping, so cortar.
    if (a.clippedRuns > 0 || a.truePeakDbtp > -0.3) {
        gSub = std::min(gSub, 0.0); gBass = std::min(gBass, 0.0);
        gPres = std::min(gPres, 1.5); gBril = std::min(gBril, 1.5);
        log << "  fonte com clipping/true peak alto: ganhos positivos restritos\n";
    }
    // Se a fonte perdeu HF (MP3 baixo bitrate), nao inventar brilho:
    // realcar acima do corte so amplifica ruido de codificacao.
    if (a.hfCutoffHz > 0 && a.hfCutoffHz < 15000) {
        gBril = std::min(gBril, 1.5);
        log << "  corte de banda em " << (int)a.hfCutoffHz << " Hz: brilho limitado\n";
    }

    // Macros do usuario somam-se, tambem limitadas.
    gBass += in.macros.bass * 3.0;
    gPres += in.macros.mid * 2.0;
    gBril += in.macros.treble * 3.0;

    // Rumble abaixo do util: sempre remover (economiza headroom real).
    double hpF = (in.route == Route::Speaker) ? 150.0 : 28.0;
    p.eq.push_back({ hpF, 0.707, 0.0, 3 });
    log << "  high-pass " << hpF << "Hz (headroom)\n";

    add(45,   0.7, clampd(gSub, -6, 3),  1, "sub");
    add(90,   0.9, clampd(gBass, -6, 4), 0, "grave");
    add(250,  1.0, clampd(gLowM, -5, 2.5), 0, "medio-grave");
    add(3200, 0.9, clampd(gPres, -5, 4), 0, "presenca");
    add(9000, 0.7, clampd(gBril, -6, 4.5), 2, "brilho");

    // Mascaramento de voz: excesso de 200-400 Hz suja o vocal.
    if (a.vocalProbability > 0.5 && (a.bands.lowMid - ref) > (tgt.lowMid + 2.0)) {
        double cut = -clampd(((a.bands.lowMid - ref) - tgt.lowMid) * 0.5, 0, 3.0) * conf;
        add(320, 1.4, cut, 0, "des-mascaramento vocal");
    }

    // ------------------------------------------------------ voz / vocal
    if (a.vocalProbability > 0.45) {
        p.vocalLiftOn = true;
        double base = (a.vocalProbability - 0.45) / 0.55;   // 0..1
        p.vocalLiftDb = clampd(base * 2.0 + in.macros.vocal * 2.5, -3.0, 3.0) * conf;
        if (in.preset == Preset::Hymns || in.preset == Preset::VoiceGuitar)
            p.vocalLiftDb = clampd(p.vocalLiftDb + 0.8, -3.0, 3.5);
        log << "  realce vocal " << p.vocalLiftDb << " dB (p_voz=" << a.vocalProbability << ")\n";
        // De-esser proporcional ao realce, evita sibilancia artificial.
        if (p.vocalLiftDb > 0.5 || (a.bands.brilliance - ref) > tgt.brilliance + 3.0) {
            p.deEsserOn = true;
            p.deEsserThresholdDb = -24.0;
            p.deEsserAmount = clampd(0.25 + p.vocalLiftDb * 0.12, 0.0, 0.6);
            log << "  de-esser " << p.deEsserAmount << "\n";
        }
    }

    // ------------------------------------------------------------ grave
    // Em alto-falante, gerar harmonicos (efeito de fundamental ausente)
    // em vez de amplificar 60 Hz que o transdutor nao reproduz.
    if (in.route == Route::Speaker && a.bassPresence > 0.15) {
        p.bassEnhancerOn = true;
        p.bassEnhancerFreq = 90;
        p.bassEnhancerAmount = clampd(0.35 + in.macros.bass * 0.2, 0.0, 0.6);
        log << "  exciter de grave (harmonicos) para alto-falante\n";
    } else if (a.bassPresence < 0.25 && a.percussionProbability > 0.4
               && in.route != Route::Speaker && a.clippedRuns == 0) {
        p.bassEnhancerOn = true;
        p.bassEnhancerFreq = 70;
        p.bassEnhancerAmount = clampd(0.2 + in.macros.bass * 0.2, 0.0, 0.4);
        log << "  exciter de grave leve (definicao)\n";
    }

    // ------------------------------------------------------- dinamica
    // So comprimir se a faixa realmente tem dinamica sobrando.
    double compNeed = clampd((a.dynamicsScore - 0.35) / 0.5, 0.0, 1.0);
    compNeed = clampd(compNeed + in.macros.compression * 0.5, 0.0, 1.0);
    if (a.crestFactorDb < 7.0) {
        compNeed = 0.0;
        log << "  fonte ja esmagada (crest " << a.crestFactorDb << " dB): sem compressao\n";
    }
    if (compNeed > 0.05) {
        p.multibandOn = true;
        p.crossoverLowHz = 200; p.crossoverHighHz = 3000;
        double r = 1.0 + compNeed * 1.2;                 // 1.0 .. 2.2 - suave
        // makeup automatico: compensa a reducao media esperada para que o
        // multibanda mude a DINAMICA sem mudar o balanco tonal da faixa.
        auto mk = [](double thr, double ratio) {
            return clampd((1.0 - 1.0 / ratio) * (-thr) * 0.45, 0.0, 6.0);
        };
        p.low  = { -22.0, r,       20.0, 220.0, 6.0, mk(-22.0, r) };
        p.midB = { -20.0, r * 0.9, 12.0, 150.0, 6.0, mk(-20.0, r * 0.9) };
        p.high = { -22.0, r * 0.8,  6.0, 100.0, 6.0, mk(-22.0, r * 0.8) };
        log << "  multibanda ratio~" << r << " (necessidade " << compNeed << ")\n";
    }

    // ------------------------------------------------------ transientes
    if (a.percussionProbability > 0.35 && a.crestFactorDb > 8.0) {
        p.transientOn = true;
        p.transientAmount = clampd(0.12 + (in.preset == Preset::DrumImpact ? 0.15 : 0.0), 0.0, 0.30);
        log << "  realce de transientes " << p.transientAmount << "\n";
    } else if (a.crestFactorDb < 7.0 && a.percussionProbability > 0.3) {
        // Fonte esmagada: recuperar um pouco de ataque.
        p.transientOn = true;
        p.transientAmount = 0.10;
        log << "  recuperacao leve de transientes (fonte comprimida)\n";
    }

    // --------------------------------------------------------- estereo
    if (a.channels > 1) {
        double w = 1.0 + in.macros.stereo * 0.25;
        if (a.sideMidRatioDb < -14.0 && a.stereoCorrelation > 0.85) {
            w += 0.12;                                    // quase mono: alargar pouco
            log << "  imagem estreita: alargamento minimo\n";
        }
        if (a.stereoCorrelation < 0.25) {
            w = std::min(w, 1.0);                         // risco de fase: nao alargar
            log << "  correlacao baixa (" << a.stereoCorrelation << "): alargamento desativado\n";
        }
        p.stereoWidth = clampd(w, 0.8, 1.25);
        // Grave em mono melhora foco e reduz consumo de headroom.
        p.bassMonoOn = (in.route != Route::Headphone) || (a.stereoCorrelation < 0.6);
        p.bassMonoHz = 110;
    } else {
        p.stereoWidth = 1.0; p.bassMonoOn = false;
    }

    // -------------------------------------------------------- loudness
    p.limiterOn = true;
    switch (in.route) {
        case Route::Speaker: p.ceilingDbtp = -1.5; break;   // margem extra: DAC + amp
        case Route::Bluetooth: p.ceilingDbtp = -1.2; break; // margem p/ codec com perdas
        default: p.ceilingDbtp = -1.0; break;
    }
    double target = in.targetLufs;
    if (in.loudnessMode == LoudnessMode::Original) {
        p.outputGainDb = 0.0;
        log << "  loudness: original (sem normalizacao)\n";
    } else {
        if (in.loudnessMode == LoudnessMode::MaxSafe) target = std::max(target, -11.0);
        target += in.macros.loudness * 2.0;
        double g = gainForTargetLufs(a.integratedLufs, target);
        // Nunca subir mais que o headroom disponivel + 6 dB de limiter.
        double headroom = p.ceilingDbtp - a.truePeakDbtp;
        double maxUp = headroom + 6.0;
        if (g > maxUp) {
            log << "  ganho pedido " << g << " dB reduzido para " << maxUp << " dB (headroom)\n";
            g = maxUp;
        }
        p.outputGainDb = clampd(g, -15.0, 20.0);
        log << "  loudness: " << a.integratedLufs << " -> alvo " << target
            << " LUFS, ganho " << p.outputGainDb << " dB\n";
    }

    // Pre-ganho negativo para os boosts de EQ nao estourarem antes do limiter.
    double maxBoost = 0;
    for (const auto& b : p.eq) maxBoost = std::max(maxBoost, b.gainDb);
    if (p.vocalLiftOn) maxBoost = std::max(maxBoost, p.vocalLiftDb);
    p.preGainDb = -clampd(maxBoost * 0.6, 0.0, 4.0);

    p.limiterLookaheadMs = 2.5;
    p.limiterReleaseMs = (a.percussionProbability > 0.5) ? 90.0 : 160.0;
    p.intensity = 1.0;

    (void)fs;
    p.decisionLog = log.str();
    return p;
}

} // namespace me

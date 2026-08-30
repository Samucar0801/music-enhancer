// SPDX-License-Identifier: Apache-2.0
#include "me/engine.h"
#include "me/analyzer.h"
#include <algorithm>
#include <chrono>
#include <cstring>

namespace me {

// ============================================================== tempo real
static const double kWindowSec = 6.0;   // janela de analise progressiva

void RealtimeEngine::start(int fs, int ch, Route route, Preset preset,
                           LoudnessMode lm, const UserMacros& macros) {
    fs_ = fs; ch_ = std::max(1, ch);
    in_ = DecisionInput{};
    in_.route = route; in_.preset = preset; in_.loudnessMode = lm; in_.macros = macros;
    window_.assign((size_t)(kWindowSec * fs_) * ch_, 0.0f);
    windowFill_ = 0;
    configured_ = false;
    bypass_ = false;
    // Configuracao inicial conservadora: so limiter, sem coloracao,
    // ate haver material suficiente para decidir.
    ChainParams safe;
    safe.enabled = true; safe.limiterOn = true; safe.ceilingDbtp = -1.0;
    safe.eq.clear(); safe.multibandOn = false;
    chain_.configure(safe, fs_, ch_);
}

void RealtimeEngine::primeWithAnalysis(const Analysis& a) {
    in_.analysis = a;
    configured_ = true;
    rebuild();
}

void RealtimeEngine::rebuild() {
    ChainParams p = decide(in_);
    if (bypass_) { p.enabled = false; p.limiterOn = false; }
    chain_.configure(p, fs_, ch_);
}

void RealtimeEngine::setRoute(Route r) { in_.route = r; if (configured_) rebuild(); }
void RealtimeEngine::setMacros(const UserMacros& m) { in_.macros = m; if (configured_) rebuild(); }
void RealtimeEngine::setBypass(bool on) {
    if (bypass_ == on) return;
    bypass_ = on;
    if (configured_) rebuild();
    else {
        ChainParams safe; safe.enabled = !on; safe.limiterOn = !on;
        chain_.configure(safe, fs_, ch_);
    }
}

void RealtimeEngine::setMix(double wet) {
    mix_ = wet < 0.0 ? 0.0 : (wet > 1.0 ? 1.0 : wet);
}

// Le o sinal seco atrasado por `dryFrames_` e empurra a entrada nova.
// Chamado ANTES de chain_.process porque `out` pode ser o mesmo buffer
// que `in` (processamento in-place no Media3).
void RealtimeEngine::captureDry(const float* in, size_t frames) {
    size_t need = chain_.latencyFrames();
    if (need != dryFrames_) {
        dryFrames_ = need;
        dry_.assign((dryFrames_ + 1) * ch_, 0.0f);
        dryPos_ = 0;
    }
    if (dryOut_.size() < frames * ch_) dryOut_.resize(frames * ch_);
    if (dryFrames_ == 0) {
        std::memcpy(dryOut_.data(), in, frames * ch_ * sizeof(float));
        return;
    }
    for (size_t f = 0; f < frames; ++f) {
        for (int c = 0; c < ch_; ++c) {
            dryOut_[f * ch_ + c] = dry_[dryPos_ * ch_ + c];
            dry_[dryPos_ * ch_ + c] = in[f * ch_ + c];
        }
        dryPos_ = (dryPos_ + 1) % dryFrames_;
    }
}

void RealtimeEngine::process(const float* in, float* out, size_t frames) {
    if (bypass_) {
        // Bypass e o caminho bit-transparente: sem atraso, sem soma.
        if (in != out) std::memcpy(out, in, frames * ch_ * sizeof(float));
        return;
    }
    if (!configured_) {
        size_t cap = window_.size() / ch_;
        size_t n = std::min(frames, cap - windowFill_);
        std::memcpy(window_.data() + windowFill_ * ch_, in, n * ch_ * sizeof(float));
        windowFill_ += n;
        if (windowFill_ >= cap) {
            in_.analysis = analyze(window_.data(), windowFill_, fs_, ch_);
            configured_ = true;
            rebuild();
        }
    }
    if (mix_ >= 1.0) { chain_.process(in, out, frames); return; }

    captureDry(in, frames);
    chain_.process(in, out, frames);
    const float w = static_cast<float>(mix_), d = 1.0f - w;
    for (size_t i = 0; i < frames * static_cast<size_t>(ch_); ++i)
        out[i] = w * out[i] + d * dryOut_[i];
}

// ================================================================ offline
// Compara o BALANCO espectral, nao o nivel. Duas armadilhas ja corrigidas
// aqui, ambas encontradas por medicao:
//  1) normalizar pelo nivel absoluto faz uma normalizacao legitima de +14 dB
//     parecer distorcao espectral;
//  2) normalizar pelo RMS tambem falha, porque remover o grave de proposito
//     (perfil de alto-falante) derruba o RMS e desloca todas as outras
//     bandas. A referencia estavel e a banda media - a mesma que o motor de
//     decisao usa como zero.
static const double kBandCenter[6] = { 34.6, 84.9, 219.0, 894.0, 3464.0, 9798.0 };

// Resposta PRETENDIDA da cadeia (soma dos filtros) no centro de cada banda,
// normalizada pela banda media. Permite ao guard distinguir uma correcao
// grande e deliberada de um processamento que fugiu do controle.
static void intendedBandChange(const ChainParams& p, int fs, double out[6]) {
    double raw[6];
    for (int i = 0; i < 6; ++i) {
        double g = 0;
        for (const auto& e : p.eq) {
            Biquad q;
            switch (e.type) {
                case 1: q = Biquad::lowShelf(fs, e.freq, e.q, e.gainDb); break;
                case 2: q = Biquad::highShelf(fs, e.freq, e.q, e.gainDb); break;
                case 3: q = Biquad::highPass(fs, e.freq, e.q); break;
                default: q = Biquad::peaking(fs, e.freq, e.q, e.gainDb); break;
            }
            g += q.magnitudeDb(fs, kBandCenter[i]);
        }
        if (p.vocalLiftOn)
            g += Biquad::peaking(fs, 2400, 1.1, p.vocalLiftDb).magnitudeDb(fs, kBandCenter[i]);
        raw[i] = g;
    }
    for (int i = 0; i < 6; ++i) out[i] = raw[i] - raw[3];   // relativo a banda media
}

static double bandDeltaMax(const Analysis& a, const Analysis& b, double highPassHz,
                           const double intended[6]) {
    const double A[6] = { a.bands.sub, a.bands.bass, a.bands.lowMid,
                          a.bands.mid, a.bands.presence, a.bands.brilliance };
    const double B[6] = { b.bands.sub, b.bands.bass, b.bands.lowMid,
                          b.bands.mid, b.bands.presence, b.bands.brilliance };
    double m = 0;
    for (int i = 0; i < 6; ++i) {
        if (i == 3) continue;                          // a propria referencia
        if (A[i] < -100 || B[i] < -100) continue;      // banda vazia: ignorar
        // Bandas abaixo do high-pass aplicado sao removidas de proposito
        // (o alto-falante do celular nao reproduz <300 Hz de forma util).
        // Contar isso como "distorcao espectral" faria o guard reverter um
        // processamento correto.
        if (kBandCenter[i] < highPassHz * 2.5) continue;
        double refA = (a.bands.mid > -100) ? a.bands.mid : a.rmsDbfs;
        double refB = (b.bands.mid > -100) ? b.bands.mid : b.rmsDbfs;
        double da = A[i] - refA;
        double dbb = B[i] - refB;
        // Descontamos a mudanca pretendida: so sobra o desvio NAO planejado
        // (compressor pesado demais, exciter fora de controle, bombeamento
        // do limiter). Uma correcao grande e deliberada de EQ nao e violacao.
        m = std::max(m, std::fabs((dbb - da) - intended[i]));
    }
    return m;
}

// Cadeia minima de seguranca: sem coloracao, so o necessario para o sinal
// nao sair acima do teto de true peak. Usada quando o guard reverte.
static ChainParams safetyOnly(const Analysis& a, double ceilingDbtp) {
    ChainParams s;
    s.enabled = true; s.eq.clear();
    s.multibandOn = s.transientOn = s.deEsserOn = s.bassEnhancerOn = false;
    s.vocalLiftOn = false; s.stereoWidth = 1.0; s.bassMonoOn = false;
    s.preGainDb = 0.0;
    s.outputGainDb = (a.truePeakDbtp > ceilingDbtp) ? (ceilingDbtp - a.truePeakDbtp) : 0.0;
    s.limiterOn = true; s.ceilingDbtp = ceilingDbtp;
    s.limiterLookaheadMs = 2.5; s.limiterReleaseMs = 150.0; s.ispMarginDb = 1.0;
    s.decisionLog = "quality guard: revertido ao original com protecao de pico\n";
    return s;
}

static void runChain(const float* in, size_t frames, int fs, int ch,
                     const ChainParams& p, double intensity, std::vector<float>& out) {
    Chain c;
    c.configure(p, fs, ch);
    if (intensity < 0.999) c.setIntensity(intensity);
    size_t lat = c.latencyFrames();
    // Processa com cauda extra para recuperar as amostras retidas no lookahead.
    std::vector<float> padded((frames + lat) * ch, 0.0f);
    std::memcpy(padded.data(), in, frames * ch * sizeof(float));
    std::vector<float> res(padded.size());
    c.process(padded.data(), res.data(), frames + lat);
    out.assign(frames * ch, 0.0f);
    std::memcpy(out.data(), res.data() + lat * ch, frames * ch * sizeof(float));
}

ProcessResult processOffline(const float* in, size_t frames, int fs, int ch,
                             const OfflineOptions& opt, std::vector<float>& outPcm,
                             bool (*cancelled)(void*), void* ctx) {
    using clk = std::chrono::steady_clock;
    auto t0 = clk::now();
    ProcessResult r;

    r.before = analyze(in, frames, fs, ch);
    DecisionInput di;
    di.analysis = r.before; di.route = opt.route; di.preset = opt.preset;
    di.loudnessMode = opt.loudnessMode; di.macros = opt.macros; di.targetLufs = opt.targetLufs;
    ChainParams p = decide(di);
    r.params = p;

    if (!p.enabled) {                       // bypass explicito
        outPcm.assign(in, in + frames * ch);
        r.after = r.before;
        r.guard.revertedToOriginal = true;
        r.processingSeconds = std::chrono::duration<double>(clk::now() - t0).count();
        return r;
    }

    double intensity = 1.0;
    int iter = 0;
    std::vector<float> work;
    Analysis after;

    while (true) {
        ++iter;
        if (cancelled && cancelled(ctx)) {
            outPcm.assign(in, in + frames * ch);
            r.after = r.before;
            r.guard.revertedToOriginal = true;
            r.guard.violations.push_back("cancelado pelo usuario");
            r.processingSeconds = std::chrono::duration<double>(clk::now() - t0).count();
            return r;
        }
        runChain(in, frames, fs, ch, p, intensity, work);
        after = analyze(work.data(), frames, fs, ch);

        // ------------------------------------------- trim de loudness (2a passagem)
        // A cadeia (EQ, multibanda, limiter) altera o loudness de forma que
        // nao da para prever em malha aberta. Medimos o resultado real e
        // corrigimos o ganho de saida. Converge em 1-2 iteracoes.
        if (opt.loudnessMode != LoudnessMode::Original && p.outputGainDb > -90.0) {
            double tgt = opt.targetLufs + opt.macros.loudness * 2.0;
            if (opt.loudnessMode == LoudnessMode::MaxSafe) tgt = std::max(tgt, -11.0);
            for (int trim = 0; trim < 2; ++trim) {
                double err = tgt - after.integratedLufs;
                if (std::fabs(err) < 0.4) break;
                double before = p.outputGainDb;
                p.outputGainDb = clampd(p.outputGainDb + err, -15.0, 20.0);
                if (std::fabs(p.outputGainDb - before) < 0.05) break;
                runChain(in, frames, fs, ch, p, intensity, work);
                after = analyze(work.data(), frames, fs, ch);
            }
            r.params = p;
        }

        // ------------------------------------ correcao medida de true peak
        // Em vez de chutar a margem ISP, medimos o true peak real da saida e
        // aumentamos a margem exatamente pelo excesso. Converge sempre.
        if (p.limiterOn) {
            for (int tpIter = 0; tpIter < 2; ++tpIter) {
                double excess = after.truePeakDbtp - p.ceilingDbtp;
                if (excess <= 0.02) break;
                p.ispMarginDb = clampd(p.ispMarginDb + excess + 0.08, 0.0, 4.0);
                runChain(in, frames, fs, ch, p, intensity, work);
                after = analyze(work.data(), frames, fs, ch);
            }
            r.params = p;
        }

        if (!opt.qualityGuard) break;

        std::vector<std::string> viol;
        if (after.truePeakDbtp > p.ceilingDbtp + opt.truePeakToleranceDb)
            viol.push_back("true peak acima do teto: " + std::to_string(after.truePeakDbtp) + " dBTP");
        if (after.clippedRuns > r.before.clippedRuns)
            viol.push_back("clipping introduzido pelo processamento");
        double crestLoss = r.before.crestFactorDb - after.crestFactorDb;
        if (crestLoss > opt.maxCrestLossDb)
            viol.push_back("perda de dinamica: " + std::to_string(crestLoss) + " dB de crest factor");
        double hpHz = 0.0;
        for (const auto& e : p.eq) if (e.type == 3) hpHz = std::max(hpHz, e.freq);
        double intended[6]; intendedBandChange(p, fs, intended);
        double bd = bandDeltaMax(r.before, after, hpHz, intended);
        if (bd > opt.maxBandChangeDb)
            viol.push_back("desvio espectral nao planejado: " + std::to_string(bd) + " dB");
        if (ch > 1 && after.stereoCorrelation < r.before.stereoCorrelation - 0.25
                   && after.stereoCorrelation < 0.3)
            viol.push_back("correlacao estereo degradada (risco de fase)");

        if (viol.empty()) { r.guard.violations.clear(); break; }
        r.guard.violations = viol;

        if (iter >= opt.maxGuardIterations || intensity <= 0.26) {
            // Regra 84: preferir o original a um processamento pior.
            // Mas ainda assim proteger o pico: uma fonte ja clipada nao pode
            // sair acima do teto so porque decidimos nao "melhorar" nada.
            ChainParams sp = safetyOnly(r.before, p.ceilingDbtp);
            runChain(in, frames, fs, ch, sp, 1.0, work);
            outPcm.swap(work);
            r.after = analyze(outPcm.data(), frames, fs, ch);
            r.params = sp;
            r.guard.iterations = iter;
            r.guard.finalIntensity = 0.0;
            r.guard.revertedToOriginal = true;
            r.improvements.push_back("Processamento revertido: o original ja estava melhor");
            if (sp.outputGainDb < -0.05)
                r.improvements.push_back("Pico limitado ao teto de seguranca");
            r.processingSeconds = std::chrono::duration<double>(clk::now() - t0).count();
            return r;
        }
        intensity *= 0.7;
    }

    outPcm.swap(work);
    r.after = after;
    r.guard.iterations = iter;
    r.guard.finalIntensity = intensity;

    // ------------------------------------------- relatorio de melhorias
    auto db = [](double v) { char b[32]; std::snprintf(b, sizeof b, "%+.1f dB", v); return std::string(b); };
    if (std::fabs(after.integratedLufs - r.before.integratedLufs) > 0.5)
        r.improvements.push_back("Loudness ajustado (" + db(after.integratedLufs - r.before.integratedLufs) + ")");
    if (after.bands.presence - after.bands.mid > r.before.bands.presence - r.before.bands.mid + 0.5)
        r.improvements.push_back("Clareza / presenca vocal");
    if (std::fabs(after.bands.bass - r.before.bands.bass) > 0.5)
        r.improvements.push_back("Controle de graves (" + db(after.bands.bass - r.before.bands.bass) + ")");
    if (after.bands.brilliance - r.before.bands.brilliance > 0.5)
        r.improvements.push_back("Extensao de agudos");
    else if (r.before.bands.brilliance - after.bands.brilliance > 0.5)
        r.improvements.push_back("Agudos agressivos controlados");
    if (after.truePeakDbtp < r.before.truePeakDbtp - 0.3)
        r.improvements.push_back("Margem de pico recuperada");
    if (r.before.clippedRuns > 0 && after.clippedRuns == 0)
        r.improvements.push_back("Clipping da fonte contido");
    if (r.improvements.empty())
        r.improvements.push_back("Diferenca minima: a fonte ja estava proxima do ideal");

    r.processingSeconds = std::chrono::duration<double>(clk::now() - t0).count();
    return r;
}

} // namespace me

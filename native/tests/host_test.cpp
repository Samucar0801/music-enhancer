// SPDX-License-Identifier: Apache-2.0
// Banco de testes do nucleo DSP. Roda no host (Linux/macOS/Windows) e no CI.
#include "me/engine.h"
#include "me/analyzer.h"
#include "me/loudness.h"
#include "signals.h"
#include "wav.h"
#include <cstdio>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

using namespace me;

static int gPass = 0, gFail = 0;
static bool gDumpWav = false;

static void check(bool ok, const std::string& name, const std::string& detail = "") {
    if (ok) { gPass++; std::printf("  [ OK ] %s%s%s\n", name.c_str(),
              detail.empty()?"":" - ", detail.c_str()); }
    else    { gFail++; std::printf("  [FALHA] %s%s%s\n", name.c_str(),
              detail.empty()?"":" - ", detail.c_str()); }
}

static std::string f2(double v) { char b[48]; std::snprintf(b,sizeof b,"%.2f",v); return b; }

// =============================================== 1. calibracao dos medidores
static void testMeters() {
    std::printf("\n== BLOCO 1: calibracao dos medidores ==\n");
    const int fs = 48000, ch = 2;

    // EBU Tech 3341, caso 1: seno 1 kHz a -23 dBFS -> -23.0 LUFS (+-0.1)
    {
        std::vector<float> b((size_t)fs*20*ch, 0.f);
        double amp = std::pow(10.0, -23.0/20.0);
        for (size_t i=0;i<(size_t)fs*20;++i) {
            float v=(float)(amp*std::sin(2*M_PI*1000.0*i/fs));
            b[i*ch]=v; b[i*ch+1]=v;
        }
        LoudnessMeter m(fs,ch); m.push(b.data(), (size_t)fs*20);
        double l = m.integratedLufs();
        check(std::fabs(l+23.0)<0.15, "LUFS integrado (EBU 3341 caso 1)",
              "medido "+f2(l)+" LUFS, esperado -23.00 +-0.15");
    }
    // Caso 2: -33 dBFS -> -33.0 LUFS
    {
        std::vector<float> b((size_t)fs*20*ch, 0.f);
        double amp = std::pow(10.0, -33.0/20.0);
        for (size_t i=0;i<(size_t)fs*20;++i) {
            float v=(float)(amp*std::sin(2*M_PI*1000.0*i/fs));
            b[i*ch]=v; b[i*ch+1]=v;
        }
        LoudnessMeter m(fs,ch); m.push(b.data(), (size_t)fs*20);
        double l = m.integratedLufs();
        check(std::fabs(l+33.0)<0.15, "LUFS integrado (EBU 3341 caso 2)",
              "medido "+f2(l)+" LUFS, esperado -33.00 +-0.15");
    }
    // Linearidade: +6 dB de ganho -> +6 LU
    {
        std::vector<float> b((size_t)fs*10*ch, 0.f);
        sig::addVoice(b, fs, ch, 220, 0.5, 0.0, 10.0);
        sig::scaleTo(b, -12.0);
        LoudnessMeter m1(fs,ch); m1.push(b.data(),(size_t)fs*10);
        std::vector<float> b2=b; for(auto&v:b2) v*=2.f;
        LoudnessMeter m2(fs,ch); m2.push(b2.data(),(size_t)fs*10);
        double d = m2.integratedLufs()-m1.integratedLufs();
        check(std::fabs(d-6.0206)<0.05, "linearidade do medidor (+6 dB)", "delta "+f2(d)+" LU");
    }
    // True peak: seno em fs/4 amostrado em 45 graus -> pico entre amostras +3 dB
    {
        const size_t n = 4096;
        std::vector<float> b(n*ch);
        double A = 0.5;
        for (size_t i=0;i<n;++i) {
            float v=(float)(A*std::sin(2*M_PI*(fs/4.0)*i/fs + M_PI/4.0));
            b[i*ch]=v; b[i*ch+1]=v;
        }
        double samplePeak=0; for(float v:b) samplePeak=std::max(samplePeak,(double)std::fabs(v));
        TruePeakMeter t(ch); t.push(b.data(), n);
        double over = lin2db(t.truePeakLinear()) - lin2db(samplePeak);
        check(over > 2.5 && over < 3.5, "true peak detecta pico entre amostras",
              "sample "+f2(lin2db(samplePeak))+" dBFS, true "+f2(t.truePeakDbtp())+" dBTP (+"+f2(over)+" dB)");
    }
    // Redigitalizacao dos filtros K para 44,1 kHz nao muda a leitura
    {
        for (int rate : {44100, 48000}) {
            std::vector<float> b((size_t)rate*15*ch, 0.f);
            double amp = std::pow(10.0, -23.0/20.0);
            for (size_t i=0;i<(size_t)rate*15;++i) {
                float v=(float)(amp*std::sin(2*M_PI*1000.0*i/rate));
                b[i*ch]=v; b[i*ch+1]=v;
            }
            LoudnessMeter m(rate,ch); m.push(b.data(),(size_t)rate*15);
            double l=m.integratedLufs();
            check(std::fabs(l+23.0)<0.15, "LUFS a "+std::to_string(rate)+" Hz", "medido "+f2(l));
        }
    }
}

// ======================================================= geradores de casos
struct Case { std::string name; std::vector<float> pcm; int fs, ch; Route route; };

static Case makeCase(const std::string& name, int fs, int ch, double dur,
                     bool voice, bool drums, bool bass, double peakDb,
                     bool clip, double bandLimitHz, Route route) {
    Case c; c.name=name; c.fs=fs; c.ch=ch; c.route=route;
    c.pcm.assign((size_t)(dur*fs)*ch, 0.f);
    if (voice) { sig::addVoice(c.pcm, fs, ch, 196, 0.55, 0.3, dur-0.4);
                 sig::addVoice(c.pcm, fs, ch, 294, 0.30, 0.5, dur-0.6); }
    if (drums) sig::addDrums(c.pcm, fs, ch, 92, 0.42, 0.0, dur);
    if (bass)  { sig::addTone(c.pcm, fs, ch, 55, 0.35, 0.0, dur);
                 sig::addTone(c.pcm, fs, ch, 82.4, 0.22, dur*0.35, dur); }
    // acompanhamento harmonico (violao/piano)
    sig::addTone(c.pcm, fs, ch, 130.8, 0.10, 0.0, dur, -0.3);
    sig::addTone(c.pcm, fs, ch, 196.0, 0.09, 0.0, dur,  0.3);
    sig::addTone(c.pcm, fs, ch, 329.6, 0.07, dur*0.2, dur, -0.2);
    sig::addTone(c.pcm, fs, ch, 2093.0, 0.03, dur*0.1, dur, 0.4);
    sig::addNoise(c.pcm, 0.0016);
    if (bandLimitHz > 0) sig::bandLimit(c.pcm, fs, ch, bandLimitHz, 8);
    sig::scaleTo(c.pcm, peakDb);
    if (clip) { sig::scaleTo(c.pcm, 3.0); sig::hardClip(c.pcm, 0.999); }
    return c;
}

// ============================================ 2. testes por tipo de material
static void testMaterial() {
    std::printf("\n== BLOCO 2: material musical (analise + processamento + guard) ==\n");
    const int FS=44100;
    std::vector<Case> cases;
    cases.push_back(makeCase("hino coral (voz, sem bateria)",       FS,2,12,true,false,false,-6.0,false,0,   Route::Headphone));
    cases.push_back(makeCase("louvor banda completa",               FS,2,12,true,true, true, -3.0,false,0,   Route::Bluetooth));
    cases.push_back(makeCase("voz + violao",                        FS,2,12,true,false,false,-9.0,false,0,   Route::Headphone));
    cases.push_back(makeCase("instrumental",                        FS,2,12,false,true,true, -4.0,false,0,   Route::Headphone));
    cases.push_back(makeCase("bateria forte (transientes)",         FS,2,12,false,true,true, -2.0,false,0,   Route::Bluetooth));
    cases.push_back(makeCase("graves fortes",                       FS,2,12,true,true, true, -2.0,false,0,   Route::Speaker));
    cases.push_back(makeCase("mono",                                FS,1,10,true,true, true, -6.0,false,0,   Route::Speaker));
    cases.push_back(makeCase("MP3 baixo bitrate (corte 11 kHz)",    FS,2,12,true,true, true, -5.0,false,11000,Route::Bluetooth));
    cases.push_back(makeCase("alto bitrate (banda cheia)",          FS,2,12,true,true, true, -5.0,false,0,   Route::Headphone));
    cases.push_back(makeCase("muito baixo (-32 dBFS)",              FS,2,12,true,true, true,-32.0,false,0,   Route::Headphone));
    cases.push_back(makeCase("muito alto (topo)",                   FS,2,12,true,true, true,-0.2,false,0,    Route::Headphone));
    cases.push_back(makeCase("com clipping na fonte",               FS,2,12,true,true, true,-3.0,true, 0,    Route::Headphone));
    cases.push_back(makeCase("48 kHz",                              48000,2,10,true,true,true,-5.0,false,0,  Route::Headphone));

    std::printf("\n%-34s %7s %7s %7s %7s %6s %6s %5s\n",
        "caso","LUFS in","LUFS out","TP in","TP out","Cr in","Cr out","guard");
    std::printf("%s\n", std::string(96,'-').c_str());

    for (auto& c : cases) {
        OfflineOptions opt; opt.route=c.route; opt.preset=Preset::Auto;
        opt.loudnessMode=LoudnessMode::Normalized; opt.targetLufs=-14.0;
        std::vector<float> out;
        ProcessResult r = processOffline(c.pcm.data(), c.pcm.size()/c.ch, c.fs, c.ch, opt, out);

        std::printf("%-34s %7.2f %7.2f %7.2f %7.2f %6.1f %6.1f %5s\n",
            c.name.c_str(), r.before.integratedLufs, r.after.integratedLufs,
            r.before.truePeakDbtp, r.after.truePeakDbtp,
            r.before.crestFactorDb, r.after.crestFactorDb,
            r.guard.revertedToOriginal ? "REV" : (r.guard.iterations>1?"red":"ok"));

        // INVARIANTES OBRIGATORIAS
        double ceiling = (c.route==Route::Speaker) ? -1.5 : (c.route==Route::Bluetooth ? -1.2 : -1.0);
        check(r.after.truePeakDbtp <= ceiling + 0.2,
              "["+c.name+"] true peak dentro do teto",
              f2(r.after.truePeakDbtp)+" <= "+f2(ceiling+0.2)+" dBTP");
        check(r.after.clippedRuns <= r.before.clippedRuns,
              "["+c.name+"] nenhum clipping introduzido");
        check(out.size()==c.pcm.size(), "["+c.name+"] tamanho do buffer preservado");
        bool finite=true; for(float v:out) if(!std::isfinite(v)) {finite=false;break;}
        check(finite, "["+c.name+"] saida sem NaN/Inf");
        if (!r.guard.revertedToOriginal) {
            check(r.before.crestFactorDb - r.after.crestFactorDb <= 3.6,
                  "["+c.name+"] dinamica preservada",
                  "perda "+f2(r.before.crestFactorDb-r.after.crestFactorDb)+" dB");
        }
        if (gDumpWav) {
            wavWrite("out_"+std::to_string(&c-&cases[0])+"_orig.wav", c.pcm, c.fs, c.ch);
            wavWrite("out_"+std::to_string(&c-&cases[0])+"_enh.wav", out, c.fs, c.ch);
        }
    }
}

// ================================================ 3. regras anti-preset-cego
static void testAdaptivity() {
    std::printf("\n== BLOCO 3: o motor e adaptativo (nao aplica preset cego) ==\n");
    const int FS=44100;
    auto gainAt=[&](const ChainParams& p, double f)->double{
        double best=0; for(const auto& b:p.eq) if(std::fabs(b.freq-f)<f*0.45) best=b.gainDb; return best; };

    Case weak = makeCase("grave fraco", FS,2,12,true,true,false,-6.0,false,0,Route::Headphone);
    Case strong = makeCase("grave forte", FS,2,12,true,true,true,-6.0,false,0,Route::Headphone);
    // reforcar ainda mais o grave do segundo caso
    sig::addTone(strong.pcm, FS,2, 45, 0.55, 0.0, 12.0);
    sig::addTone(strong.pcm, FS,2, 62, 0.40, 0.0, 12.0);
    sig::scaleTo(strong.pcm, -6.0);

    auto decideFor=[&](Case& c, Route r)->ChainParams{
        DecisionInput di; di.analysis=analyze(c.pcm.data(),c.pcm.size()/c.ch,c.fs,c.ch);
        di.route=r; di.preset=Preset::Auto; di.loudnessMode=LoudnessMode::Normalized;
        return decide(di); };

    ChainParams pw = decideFor(weak, Route::Headphone);
    ChainParams ps = decideFor(strong, Route::Headphone);
    double gw=gainAt(pw,90), gs=gainAt(ps,90);
    check(gs < gw, "grave ja forte recebe MENOS reforco que grave fraco",
          "fraco "+f2(gw)+" dB vs forte "+f2(gs)+" dB");

    ChainParams php = decideFor(strong, Route::Headphone);
    ChainParams psp = decideFor(strong, Route::Speaker);
    check(psp.eq[0].freq > php.eq[0].freq,
          "alto-falante usa high-pass mais alto que fone",
          f2(psp.eq[0].freq)+" Hz vs "+f2(php.eq[0].freq)+" Hz");
    check(psp.bassEnhancerOn, "alto-falante usa exciter harmonico em vez de amplificar 60 Hz");

    // fonte com clipping nao pode receber ganho positivo em grave/agudo
    Case clipped = makeCase("clipada", FS,2,12,true,true,true,-3.0,true,0,Route::Headphone);
    ChainParams pc = decideFor(clipped, Route::Headphone);
    bool anyPos=false; for(const auto& b:pc.eq) if(b.type!=3 && b.gainDb>1.6) anyPos=true;
    check(!anyPos, "fonte com clipping nao recebe reforco agressivo de EQ");

    // fonte com corte de banda nao recebe brilho inventado
    Case lim = makeCase("banda cortada", FS,2,12,true,true,true,-6.0,false,10000,Route::Headphone);
    ChainParams pl = decideFor(lim, Route::Headphone);
    double gb=gainAt(pl,9000);
    check(gb <= 1.6, "banda cortada: agudo nao e inventado", f2(gb)+" dB (teto 1.5)");

    // fonte ja esmagada nao recebe mais compressao
    Case squash = makeCase("esmagada", FS,2,12,true,true,true,-1.0,true,0,Route::Headphone);
    ChainParams pq = decideFor(squash, Route::Headphone);
    check(!pq.multibandOn, "fonte ja esmagada nao recebe compressao adicional");

    // todos os ganhos dentro dos limites declarados
    bool inRange=true; double worst=0;
    for (Route r : {Route::Headphone,Route::Bluetooth,Route::Speaker,Route::Car})
        for (Case* c : {&weak,&strong,&clipped,&lim,&squash}) {
            ChainParams p=decideFor(*c,r);
            for (const auto& b:p.eq) { if(b.type==3) continue;
                if(std::fabs(b.gainDb)>6.01) inRange=false;
                worst=std::max(worst,std::fabs(b.gainDb)); }
        }
    check(inRange, "nenhum ganho de EQ excede +-6 dB em nenhuma rota",
          "maior magnitude "+f2(worst)+" dB");
}

// =============================================== 4. Quality Guard e fallback
static void testGuard() {
    std::printf("\n== BLOCO 4: Quality Guard, bypass e robustez ==\n");
    const int FS=44100;

    Case c = makeCase("guard", FS,2,10,true,true,true,-4.0,false,0,Route::Headphone);

    // Loudness absurdo: o guard deve conter o resultado, nunca estourar
    {
        OfflineOptions o; o.loudnessMode=LoudnessMode::MaxSafe; o.targetLufs=-5.0;
        o.macros.loudness=1.0; o.macros.bass=1.0; o.macros.treble=1.0;
        std::vector<float> out;
        ProcessResult r=processOffline(c.pcm.data(),c.pcm.size()/2,FS,2,o,out);
        check(r.after.truePeakDbtp <= -0.8, "pedido extremo de volume nao estoura o teto",
              f2(r.after.truePeakDbtp)+" dBTP");
        bool fin=true; for(float v:out) if(!std::isfinite(v)) fin=false;
        check(fin,"saida finita sob pedido extremo");
    }
    // Guard desligado vs ligado: ligado nunca pode ser pior no true peak
    {
        OfflineOptions on, off; off.qualityGuard=false;
        on.macros.bass=1.0; off.macros.bass=1.0;
        std::vector<float> a,b;
        ProcessResult ra=processOffline(c.pcm.data(),c.pcm.size()/2,FS,2,on,a);
        ProcessResult rb=processOffline(c.pcm.data(),c.pcm.size()/2,FS,2,off,b);
        check(ra.after.truePeakDbtp <= rb.after.truePeakDbtp+0.35,
              "guard ligado nunca piora o true peak");
    }
    // Bypass devolve a amostra identica
    {
        OfflineOptions o; o.preset=Preset::Bypass;
        std::vector<float> out;
        processOffline(c.pcm.data(),c.pcm.size()/2,FS,2,o,out);
        double maxd=0; for(size_t i=0;i<out.size();++i) maxd=std::max(maxd,(double)std::fabs(out[i]-c.pcm[i]));
        check(maxd<1e-9, "bypass e bit-transparente", "delta max "+f2(maxd));
    }
    // Arquivo corrompido / vazio / silencio
    {
        std::vector<float> empty;
        Analysis a=analyze(empty.data(),0,FS,2);
        check(!a.valid,"buffer vazio e rejeitado sem crash");
        std::vector<float> silence((size_t)FS*3*2,0.f);
        OfflineOptions o; std::vector<float> out;
        ProcessResult r=processOffline(silence.data(),FS*3,FS,2,o,out);
        bool fin=true; for(float v:out) if(!std::isfinite(v)) fin=false;
        check(fin && out.size()==silence.size(),"silencio total nao gera NaN nem ganho infinito");
        std::vector<float> nan((size_t)FS*2,0.f); nan[100]=NAN; nan[101]=INFINITY;
        Analysis an=analyze(nan.data(),FS,FS,1);
        check(true,"buffer com NaN nao trava a analise",
              std::isfinite(an.integratedLufs)?"LUFS finito":"LUFS nao-finito (tratado a montante)");
    }
    // Tempo real: bypass A/B sem descontinuidade grosseira
    {
        RealtimeEngine e;
        e.start(FS,2,Route::Headphone,Preset::Auto,LoudnessMode::Normalized,UserMacros{});
        std::vector<float> out(c.pcm.size());
        size_t blk=1024, n=c.pcm.size()/2;
        for (size_t i=0;i<n;i+=blk) {
            size_t k=std::min(blk,n-i);
            if (i==n/2) e.setBypass(true);
            e.process(c.pcm.data()+i*2, out.data()+i*2, k);
        }
        double maxJump=0;
        for (size_t i=1;i<out.size();++i) maxJump=std::max(maxJump,(double)std::fabs(out[i]-out[i-1]));
        check(maxJump<0.9,"alternar bypass em tempo real nao gera estouro",
              "maior salto entre amostras "+f2(maxJump));
        bool fin=true; for(float v:out) if(!std::isfinite(v)) fin=false;
        check(fin,"tempo real: saida finita");
    }
    // Mistura A/B: o caminho seco precisa sair ATRASADO pela latencia da
    // cadeia. Sem isso as duas copias do mesmo sinal somam desalinhadas e
    // produzem filtro pente (nulos em 218 Hz e multiplos, com 110 amostras).
    {
        size_t n = std::min<size_t>(c.pcm.size()/2, (size_t)FS*4);
        auto run=[&](double mix, std::vector<float>& out){
            RealtimeEngine e;
            e.start(FS,2,Route::Headphone,Preset::Auto,LoudnessMode::Normalized,UserMacros{});
            e.setMix(mix);
            out.assign(n*2,0.f);
            size_t blk=1024;
            for (size_t i=0;i<n;i+=blk) {
                size_t k=std::min(blk,n-i);
                e.process(c.pcm.data()+i*2, out.data()+i*2, k);
            }
            return e.latencyFrames();
        };
        std::vector<float> o0,o1,oh;
        size_t lat=run(0.0,o0); run(1.0,o1); run(0.5,oh);

        // A) mix=0 devolve o ORIGINAL atrasado, amostra por amostra
        double maxd=0; size_t start=lat+16;
        for (size_t f=start; f<n; ++f)
            for (int ch=0; ch<2; ++ch)
                maxd=std::max(maxd,(double)std::fabs(o0[f*2+ch]-c.pcm[(f-lat)*2+ch]));
        check(maxd<1e-9,"mix=0 devolve o original atrasado pela latencia (sem filtro pente)",
              "atraso "+std::to_string(lat)+" amostras, delta max "+f2(maxd));

        // B) mix=0.5 e a media exata dos dois extremos ja alinhados
        double maxm=0;
        for (size_t i=start*2;i<n*2;++i)
            maxm=std::max(maxm,(double)std::fabs(oh[i]-0.5f*(o0[i]+o1[i])));
        check(maxm<1e-6,"mix=0.5 e o crossfade linear exato de original e aprimorado",
              "desvio max "+f2(maxm));

        // C) nenhum cancelamento: a mistura nao pode ficar mais fraca que as pontas
        auto rms=[&](const std::vector<float>& v){ double s=0;
            for(size_t i=start*2;i<n*2;++i) s+=(double)v[i]*v[i];
            return std::sqrt(s/(double)((n-start)*2)); };
        double r0=rms(o0), r1=rms(o1), rh=rms(oh);
        check(rh > 0.85*std::min(r0,r1),
              "mistura A/B nao cancela o sinal",
              "rms orig "+f2(r0)+" | mix 0.5 "+f2(rh)+" | aprim "+f2(r1));
    }
}

// ==================================================== 5. custo de execucao
static void testPerformance() {
    std::printf("\n== BLOCO 5: custo de processamento (host, NAO e o Redmi) ==\n");
    const int FS=44100; const double dur=60.0;
    Case c = makeCase("perf",FS,2,dur,true,true,true,-6.0,false,0,Route::Headphone);

    RealtimeEngine e;
    e.start(FS,2,Route::Bluetooth,Preset::Auto,LoudnessMode::Normalized,UserMacros{});
    std::vector<float> out(c.pcm.size());
    auto t0=std::chrono::steady_clock::now();
    size_t blk=1024,n=c.pcm.size()/2;
    for(size_t i=0;i<n;i+=blk){size_t k=std::min(blk,n-i);e.process(c.pcm.data()+i*2,out.data()+i*2,k);}
    double rt=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
    std::printf("  tempo real : %.3f s de CPU para %.0f s de audio = %.0fx realtime\n", rt, dur, dur/rt);
    check(rt < dur*0.5, "DSP em tempo real cabe com folga", f2(dur/rt)+"x realtime no host");

    OfflineOptions o; std::vector<float> ov;
    auto t1=std::chrono::steady_clock::now();
    ProcessResult r=processOffline(c.pcm.data(),n,FS,2,o,ov);
    double off=std::chrono::duration<double>(std::chrono::steady_clock::now()-t1).count();
    std::printf("  offline    : %.3f s para %.0f s de audio = %.1fx realtime (%d passagem(ns) do guard)\n",
                off, dur, dur/off, r.guard.iterations);
    std::printf("  latencia   : %zu amostras (%.1f ms)\n",
                e.latencyFrames(), e.latencyFrames()*1000.0/FS);
    check(e.latencyFrames()*1000.0/FS < 12.0, "latencia do limiter aceitavel");
}

int main(int argc, char** argv) {
    for (int i=1;i<argc;++i) if (std::string(argv[i])=="--wav") gDumpWav=true;
    std::printf("=====================================================\n");
    std::printf(" MUSIC ENHANCER - banco de testes do nucleo DSP\n");
    std::printf("=====================================================\n");
    testMeters();
    testMaterial();
    testAdaptivity();
    testGuard();
    testPerformance();
    std::printf("\n=====================================================\n");
    std::printf(" RESULTADO: %d passaram, %d falharam\n", gPass, gFail);
    std::printf("=====================================================\n");
    return gFail==0 ? 0 : 1;
}

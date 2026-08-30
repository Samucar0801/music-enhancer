// SPDX-License-Identifier: Apache-2.0
// mecli - processa um WAV com o mesmo motor do app. Util para validar o DSP
// no PC antes/depois de compilar o APK.
#include "me/engine.h"
#include "me/analyzer.h"
#include "wav.h"
#include <cstdio>
#include <cstring>
#include <string>
using namespace me;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("uso: mecli entrada.wav saida.wav [--route fone|bt|speaker|carro]\n"
                    "                                  [--lufs -14] [--preset auto|hinos|voz|bateria]\n");
        return 1;
    }
    OfflineOptions o;
    for (int i = 3; i < argc - 1; ++i) {
        std::string a = argv[i], v = argv[i + 1];
        if (a == "--route") o.route = (v=="bt")?Route::Bluetooth:(v=="speaker")?Route::Speaker
                                     :(v=="carro")?Route::Car:Route::Headphone;
        if (a == "--lufs") o.targetLufs = atof(v.c_str());
        if (a == "--preset") o.preset = (v=="hinos")?Preset::Hymns:(v=="voz")?Preset::VoiceGuitar
                                       :(v=="bateria")?Preset::DrumImpact:Preset::Auto;
    }
    Wav w;
    if (!wavRead(argv[1], w)) { std::printf("erro: nao consegui ler %s\n", argv[1]); return 2; }
    std::printf("entrada: %d Hz, %d canais, %.1f s\n", w.sampleRate, w.channels,
                (double)w.pcm.size()/w.channels/w.sampleRate);
    std::vector<float> out;
    ProcessResult r = processOffline(w.pcm.data(), w.pcm.size()/w.channels,
                                     w.sampleRate, w.channels, o, out);
    std::printf("\n--- ANALISE ---\n");
    std::printf("estilo provavel : %s (%.0f%%)\n", r.before.genreGuess.c_str(), r.before.genreConfidence*100);
    std::printf("BPM             : %.0f (confianca %.0f%%)\n", r.before.bpm, r.before.bpmConfidence*100);
    std::printf("voz / percussao : %.0f%% / %.0f%%\n", r.before.vocalProbability*100, r.before.percussionProbability*100);
    std::printf("banda util ate  : %.0f Hz\n", r.before.hfCutoffHz);
    for (auto& n : r.before.notes) std::printf("nota            : %s\n", n.c_str());
    std::printf("\n--- ANTES -> DEPOIS ---\n");
    std::printf("LUFS integrado  : %7.2f -> %7.2f\n", r.before.integratedLufs, r.after.integratedLufs);
    std::printf("true peak       : %7.2f -> %7.2f dBTP\n", r.before.truePeakDbtp, r.after.truePeakDbtp);
    std::printf("crest factor    : %7.2f -> %7.2f dB\n", r.before.crestFactorDb, r.after.crestFactorDb);
    std::printf("LRA             : %7.2f -> %7.2f LU\n", r.before.loudnessRangeLu, r.after.loudnessRangeLu);
    std::printf("clipping (runs) : %7d -> %7d\n", r.before.clippedRuns, r.after.clippedRuns);
    std::printf("\n--- QUALITY GUARD ---\n");
    std::printf("iteracoes=%d intensidade final=%.2f revertido=%s\n",
                r.guard.iterations, r.guard.finalIntensity, r.guard.revertedToOriginal?"SIM":"nao");
    for (auto& v : r.guard.violations) std::printf("  aviso: %s\n", v.c_str());
    std::printf("\n--- MELHORIAS ---\n");
    for (auto& m : r.improvements) std::printf("  + %s\n", m.c_str());
    std::printf("\n--- DECISAO ---\n%s", r.params.decisionLog.c_str());
    std::printf("\ntempo de processamento: %.2f s\n", r.processingSeconds);
    if (!wavWrite(argv[2], out, w.sampleRate, w.channels)) { std::printf("erro ao gravar\n"); return 3; }
    std::printf("gravado: %s\n", argv[2]);
    return 0;
}

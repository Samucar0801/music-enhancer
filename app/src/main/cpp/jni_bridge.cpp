// SPDX-License-Identifier: Apache-2.0
// Ponte JNI. Toda a logica de audio esta no nucleo portatil em native/.
#include <jni.h>
#include <android/log.h>
#include <memory>
#include <mutex>
#include <atomic>
#include "me/engine.h"
#include "me/analyzer.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "MEnative", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "MEnative", __VA_ARGS__)

using namespace me;

namespace {
struct RtHandle {
    RealtimeEngine engine;
    std::mutex mtx;
    int fs = 48000, ch = 2;
};
std::atomic<bool> gCancel{false};
bool cancelCb(void*) { return gCancel.load(); }

jdoubleArray analysisToArray(JNIEnv* env, const Analysis& a) {
    // Layout fixo consumido por Analysis.fromArray() no Kotlin.
    double v[28] = {
        a.valid ? 1.0 : 0.0, (double)a.sampleRate, (double)a.channels, a.durationSec,
        a.integratedLufs, a.loudnessRangeLu, a.truePeakDbtp, a.samplePeakDbfs,
        a.rmsDbfs, a.crestFactorDb,
        a.bands.sub, a.bands.bass, a.bands.lowMid, a.bands.mid, a.bands.presence, a.bands.brilliance,
        a.spectralCentroidHz, a.spectralRolloff85Hz, a.spectralFlatness, a.hfCutoffHz,
        a.stereoCorrelation, a.sideMidRatioDb, a.channelBalanceDb,
        (double)a.clippedRuns, a.bpm, a.vocalProbability, a.percussionProbability, a.dynamicsScore
    };
    jdoubleArray out = env->NewDoubleArray(28);
    env->SetDoubleArrayRegion(out, 0, 28, v);
    return out;
}
} // namespace

extern "C" {

JNIEXPORT jstring JNICALL
Java_com_musicenhancer_app_dsp_NativeEngine_nativeVersion(JNIEnv* env, jobject) {
    return env->NewStringUTF("MusicEnhancer DSP 1.0.0");
}

// ------------------------------------------------------------- tempo real
JNIEXPORT jlong JNICALL
Java_com_musicenhancer_app_dsp_NativeEngine_rtCreate(
        JNIEnv*, jobject, jint fs, jint ch, jint route, jint preset, jint loudnessMode) {
    auto* h = new RtHandle();
    h->fs = fs; h->ch = ch;
    h->engine.start(fs, ch, (Route)route, (Preset)preset, (LoudnessMode)loudnessMode, UserMacros{});
    LOGI("rtCreate fs=%d ch=%d route=%d", fs, ch, route);
    return reinterpret_cast<jlong>(h);
}

JNIEXPORT void JNICALL
Java_com_musicenhancer_app_dsp_NativeEngine_rtDestroy(JNIEnv*, jobject, jlong p) {
    delete reinterpret_cast<RtHandle*>(p);
}

JNIEXPORT void JNICALL
Java_com_musicenhancer_app_dsp_NativeEngine_rtSetRoute(JNIEnv*, jobject, jlong p, jint route) {
    auto* h = reinterpret_cast<RtHandle*>(p); if (!h) return;
    std::lock_guard<std::mutex> lk(h->mtx);
    h->engine.setRoute((Route)route);
}

JNIEXPORT void JNICALL
Java_com_musicenhancer_app_dsp_NativeEngine_rtSetBypass(JNIEnv*, jobject, jlong p, jboolean on) {
    auto* h = reinterpret_cast<RtHandle*>(p); if (!h) return;
    std::lock_guard<std::mutex> lk(h->mtx);
    h->engine.setBypass(on == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_musicenhancer_app_dsp_NativeEngine_rtSetMix(JNIEnv*, jobject, jlong p, jdouble wet) {
    auto* h = reinterpret_cast<RtHandle*>(p); if (!h) return;
    std::lock_guard<std::mutex> lk(h->mtx);
    h->engine.setMix(wet);
}

JNIEXPORT void JNICALL
Java_com_musicenhancer_app_dsp_NativeEngine_rtSetMacros(
        JNIEnv*, jobject, jlong p, jdouble bass, jdouble mid, jdouble treble,
        jdouble vocal, jdouble clarity, jdouble stereo, jdouble loud, jdouble comp) {
    auto* h = reinterpret_cast<RtHandle*>(p); if (!h) return;
    UserMacros m; m.bass=bass; m.mid=mid; m.treble=treble; m.vocal=vocal;
    m.clarity=clarity; m.stereo=stereo; m.loudness=loud; m.compression=comp;
    std::lock_guard<std::mutex> lk(h->mtx);
    h->engine.setMacros(m);
}

// Processa in-place um bloco intercalado. Chamado da thread de audio:
// sem alocacao, sem JNI callbacks, sem I/O.
JNIEXPORT void JNICALL
Java_com_musicenhancer_app_dsp_NativeEngine_rtProcess(
        JNIEnv* env, jobject, jlong p, jfloatArray buf, jint frames) {
    auto* h = reinterpret_cast<RtHandle*>(p); if (!h) return;
    jfloat* d = env->GetFloatArrayElements(buf, nullptr);
    if (!d) return;
    {
        std::lock_guard<std::mutex> lk(h->mtx);
        h->engine.process(d, d, (size_t)frames);
    }
    env->ReleaseFloatArrayElements(buf, d, 0);
}

JNIEXPORT jint JNICALL
Java_com_musicenhancer_app_dsp_NativeEngine_rtLatencyFrames(JNIEnv*, jobject, jlong p) {
    auto* h = reinterpret_cast<RtHandle*>(p);
    return h ? (jint)h->engine.latencyFrames() : 0;
}

// --------------------------------------------------------------- analise
JNIEXPORT jdoubleArray JNICALL
Java_com_musicenhancer_app_dsp_NativeEngine_analyzePcm(
        JNIEnv* env, jobject, jfloatArray pcm, jint fs, jint ch) {
    jsize n = env->GetArrayLength(pcm);
    jfloat* d = env->GetFloatArrayElements(pcm, nullptr);
    Analysis a = analyze(d, (size_t)(n / ch), fs, ch);
    env->ReleaseFloatArrayElements(pcm, d, JNI_ABORT);
    return analysisToArray(env, a);
}

JNIEXPORT jstring JNICALL
Java_com_musicenhancer_app_dsp_NativeEngine_genreOf(
        JNIEnv* env, jobject, jfloatArray pcm, jint fs, jint ch) {
    jsize n = env->GetArrayLength(pcm);
    jfloat* d = env->GetFloatArrayElements(pcm, nullptr);
    Analysis a = analyze(d, (size_t)(n / ch), fs, ch);
    env->ReleaseFloatArrayElements(pcm, d, JNI_ABORT);
    std::string s = a.genreGuess + "|" + std::to_string(a.genreConfidence);
    for (auto& nt : a.notes) s += "|" + nt;
    return env->NewStringUTF(s.c_str());
}

// --------------------------------------------------- offline / qualidade
JNIEXPORT void JNICALL
Java_com_musicenhancer_app_dsp_NativeEngine_cancelOffline(JNIEnv*, jobject) {
    gCancel.store(true);
}

// Retorna o PCM aprimorado; metricas ficam em outMetrics (64 doubles):
// [0..27] analise antes, [28..55] analise depois,
// [56] iteracoes do guard, [57] intensidade final, [58] revertido,
// [59] segundos de processamento, [60] latencia, [61..63] reservado.
JNIEXPORT jfloatArray JNICALL
Java_com_musicenhancer_app_dsp_NativeEngine_processOfflinePcm(
        JNIEnv* env, jobject, jfloatArray pcm, jint fs, jint ch,
        jint route, jint preset, jint loudnessMode, jdouble targetLufs,
        jboolean guard, jdoubleArray outMetrics) {
    gCancel.store(false);
    jsize n = env->GetArrayLength(pcm);
    jfloat* d = env->GetFloatArrayElements(pcm, nullptr);
    if (!d) return nullptr;

    OfflineOptions o;
    o.route = (Route)route; o.preset = (Preset)preset;
    o.loudnessMode = (LoudnessMode)loudnessMode; o.targetLufs = targetLufs;
    o.qualityGuard = (guard == JNI_TRUE);

    std::vector<float> out;
    ProcessResult r;
    try {
        r = processOffline(d, (size_t)(n / ch), fs, ch, o, out, cancelCb, nullptr);
    } catch (const std::exception& e) {
        LOGE("processOffline falhou: %s", e.what());
        env->ReleaseFloatArrayElements(pcm, d, JNI_ABORT);
        return nullptr;                    // Kotlin faz fallback para tempo real
    } catch (...) {
        LOGE("processOffline falhou (excecao desconhecida)");
        env->ReleaseFloatArrayElements(pcm, d, JNI_ABORT);
        return nullptr;
    }
    env->ReleaseFloatArrayElements(pcm, d, JNI_ABORT);

    if (outMetrics && env->GetArrayLength(outMetrics) >= 64) {
        jdouble m[64] = {0};
        auto fill = [&](int base, const Analysis& a) {
            double v[28] = { a.valid?1.0:0.0,(double)a.sampleRate,(double)a.channels,a.durationSec,
                a.integratedLufs,a.loudnessRangeLu,a.truePeakDbtp,a.samplePeakDbfs,a.rmsDbfs,a.crestFactorDb,
                a.bands.sub,a.bands.bass,a.bands.lowMid,a.bands.mid,a.bands.presence,a.bands.brilliance,
                a.spectralCentroidHz,a.spectralRolloff85Hz,a.spectralFlatness,a.hfCutoffHz,
                a.stereoCorrelation,a.sideMidRatioDb,a.channelBalanceDb,(double)a.clippedRuns,
                a.bpm,a.vocalProbability,a.percussionProbability,a.dynamicsScore };
            for (int i=0;i<28;++i) m[base+i]=v[i];
        };
        fill(0, r.before); fill(28, r.after);
        m[56]=r.guard.iterations; m[57]=r.guard.finalIntensity;
        m[58]=r.guard.revertedToOriginal?1.0:0.0; m[59]=r.processingSeconds;
        env->SetDoubleArrayRegion(outMetrics, 0, 64, m);
    }

    jfloatArray res = env->NewFloatArray((jsize)out.size());
    if (!res) return nullptr;
    env->SetFloatArrayRegion(res, 0, (jsize)out.size(), out.data());
    return res;
}

} // extern "C"

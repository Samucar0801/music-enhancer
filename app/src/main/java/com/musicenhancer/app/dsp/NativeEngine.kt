package com.musicenhancer.app.dsp

/** Rota de saída detectada. A ordem espelha `me::Route` no C++. */
enum class Route { HEADPHONE, BLUETOOTH, SPEAKER, CAR }

/** Ordem espelha `me::Preset`. */
enum class Preset { AUTO, MAX_QUALITY_AI, HEADPHONE, HYMNS, VOICE_GUITAR,
                    DRUM_IMPACT, INSTRUMENTAL, LOUDNESS, BALANCED, BYPASS }

/** Ordem espelha `me::LoudnessMode`. */
enum class LoudnessMode { ORIGINAL, NORMALIZED, MAX_SAFE }

data class Macros(
    val bass: Double = 0.0, val mid: Double = 0.0, val treble: Double = 0.0,
    val vocal: Double = 0.0, val clarity: Double = 0.0, val stereo: Double = 0.0,
    val loudness: Double = 0.0, val compression: Double = 0.0
)

data class Analysis(
    val valid: Boolean, val sampleRate: Int, val channels: Int, val durationSec: Double,
    val integratedLufs: Double, val loudnessRangeLu: Double, val truePeakDbtp: Double,
    val samplePeakDbfs: Double, val rmsDbfs: Double, val crestFactorDb: Double,
    val sub: Double, val bass: Double, val lowMid: Double, val mid: Double,
    val presence: Double, val brilliance: Double,
    val centroidHz: Double, val rolloff85Hz: Double, val flatness: Double, val hfCutoffHz: Double,
    val stereoCorrelation: Double, val sideMidRatioDb: Double, val channelBalanceDb: Double,
    val clippedRuns: Int, val bpm: Double, val vocalProbability: Double,
    val percussionProbability: Double, val dynamicsScore: Double
) {
    companion object {
        const val SIZE = 28
        fun fromArray(v: DoubleArray, off: Int = 0) = Analysis(
            v[off] > 0.5, v[off+1].toInt(), v[off+2].toInt(), v[off+3],
            v[off+4], v[off+5], v[off+6], v[off+7], v[off+8], v[off+9],
            v[off+10], v[off+11], v[off+12], v[off+13], v[off+14], v[off+15],
            v[off+16], v[off+17], v[off+18], v[off+19],
            v[off+20], v[off+21], v[off+22], v[off+23].toInt(),
            v[off+24], v[off+25], v[off+26], v[off+27]
        )
    }
    /** Descreve a limitação da fonte sem fingir que recuperou o que se perdeu. */
    fun sourceLimitationOrNull(): String? = when {
        hfCutoffHz in 1.0..13000.0 ->
            "Banda útil até ~${(hfCutoffHz / 1000).toInt()} kHz. Detalhes de alta " +
            "frequência acima disso não existem no arquivo e não serão inventados."
        clippedRuns > 0 ->
            "Clipping presente na gravação original ($clippedRuns trechos). " +
            "O app contém o pico, mas não restaura a forma de onda perdida."
        crestFactorDb < 7.0 ->
            "Fonte muito comprimida (crest ${"%.1f".format(crestFactorDb)} dB). " +
            "A dinâmica original não pode ser recriada."
        else -> null
    }
}

data class OfflineResult(
    val pcm: FloatArray, val before: Analysis, val after: Analysis,
    val guardIterations: Int, val guardIntensity: Double, val revertedToOriginal: Boolean,
    val processingSeconds: Double
) {
    /** Lista apenas melhorias que foram efetivamente MEDIDAS. Regra 46/85. */
    fun measuredImprovements(): List<String> = buildList {
        if (revertedToOriginal) {
            add("Original mantido: o processamento não trouxe ganho mensurável")
            return@buildList
        }
        val dl = after.integratedLufs - before.integratedLufs
        if (kotlin.math.abs(dl) > 0.5) add("Loudness ajustado (%+.1f LU)".format(dl))
        val dp = (after.presence - after.mid) - (before.presence - before.mid)
        if (dp > 0.5) add("Clareza e presença vocal (%+.1f dB)".format(dp))
        val db = after.bass - before.bass
        if (kotlin.math.abs(db) > 0.5) add("Controle de graves (%+.1f dB)".format(db))
        val dt = before.truePeakDbtp - after.truePeakDbtp
        if (dt > 0.3) add("Margem de pico recuperada (%.1f dB)".format(dt))
        if (before.clippedRuns > 0 && after.clippedRuns == 0) add("Clipping da fonte contido")
        val dc = after.crestFactorDb - before.crestFactorDb
        if (dc > 0.5) add("Dinâmica preservada (%+.1f dB de crest factor)".format(dc))
        if (isEmpty()) add("Diferença mínima: a fonte já estava próxima do ideal")
    }
    override fun equals(other: Any?) = this === other
    override fun hashCode() = System.identityHashCode(this)
}

/**
 * Ponte para o núcleo DSP em C++. Toda a decisão musical vive no nativo:
 * este arquivo só transporta dados.
 */
object NativeEngine {
    @Volatile var available: Boolean = false; private set
    @Volatile var loadError: String? = null; private set

    init {
        try {
            System.loadLibrary("musicenhancer")
            available = true
        } catch (t: Throwable) {
            available = false
            loadError = t.message ?: t.javaClass.simpleName
        }
    }

    external fun nativeVersion(): String

    /** Versão do núcleo, ou uma explicação — nunca um valor inventado. */
    fun safeVersion(): String =
        if (!available) "indisponível"
        else runCatching { nativeVersion() }.getOrDefault("não informada")

    // tempo real
    external fun rtCreate(fs: Int, ch: Int, route: Int, preset: Int, loudnessMode: Int): Long
    external fun rtDestroy(handle: Long)
    external fun rtSetRoute(handle: Long, route: Int)
    external fun rtSetBypass(handle: Long, on: Boolean)

    /** Mistura A/B. 0.0 = original (atrasado p/ alinhar), 1.0 = aprimorado. */
    external fun rtSetMix(handle: Long, wet: Double)
    external fun rtSetMacros(handle: Long, bass: Double, mid: Double, treble: Double,
                             vocal: Double, clarity: Double, stereo: Double,
                             loudness: Double, compression: Double)
    external fun rtProcess(handle: Long, buf: FloatArray, frames: Int)
    external fun rtLatencyFrames(handle: Long): Int

    // análise e offline
    external fun analyzePcm(pcm: FloatArray, fs: Int, ch: Int): DoubleArray
    external fun genreOf(pcm: FloatArray, fs: Int, ch: Int): String
    external fun cancelOffline()
    external fun processOfflinePcm(
        pcm: FloatArray, fs: Int, ch: Int, route: Int, preset: Int,
        loudnessMode: Int, targetLufs: Double, guard: Boolean, outMetrics: DoubleArray
    ): FloatArray?

    fun analyze(pcm: FloatArray, fs: Int, ch: Int): Analysis? {
        if (!available) return null
        return runCatching { Analysis.fromArray(analyzePcm(pcm, fs, ch)) }.getOrNull()
    }

    fun processOffline(
        pcm: FloatArray, fs: Int, ch: Int, route: Route, preset: Preset,
        loudnessMode: LoudnessMode, targetLufs: Double, guard: Boolean = true
    ): OfflineResult? {
        if (!available) return null
        val m = DoubleArray(64)
        val out = runCatching {
            processOfflinePcm(pcm, fs, ch, route.ordinal, preset.ordinal,
                loudnessMode.ordinal, targetLufs, guard, m)
        }.getOrNull() ?: return null
        return OfflineResult(
            out, Analysis.fromArray(m, 0), Analysis.fromArray(m, 28),
            m[56].toInt(), m[57], m[58] > 0.5, m[59]
        )
    }
}

package com.musicenhancer.app.playback

import androidx.media3.common.audio.AudioProcessor
import androidx.media3.common.audio.AudioProcessor.AudioFormat
import androidx.media3.common.C
import androidx.media3.common.util.UnstableApi
import com.musicenhancer.app.dsp.*
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Insere o DSP dentro do pipeline de áudio do Media3, ANTES do AudioSink.
 *
 * Por que aqui e não via android.media.audiofx.AudioEffect:
 *  - AudioEffect anexa a um audio session e, segundo relatos recorrentes de
 *    usuários, frequentemente não se aplica à rota Bluetooth;
 *  - o efeito do sistema roda depois de qualquer coisa que o fabricante já
 *    tenha empilhado, o que produz a distorção clássica de EQs sobrepostos;
 *  - aqui temos float PCM, controle total de headroom e o mesmo resultado em
 *    alto-falante, fone e Bluetooth.
 */
@UnstableApi
class EnhancerAudioProcessor : AudioProcessor {

    private var inputFormat = AudioFormat.NOT_SET
    private var outputFormat = AudioFormat.NOT_SET
    private var buffer: ByteBuffer = AudioProcessor.EMPTY_BUFFER
    private var outputBuffer: ByteBuffer = AudioProcessor.EMPTY_BUFFER
    private var inputEnded = false

    private var handle: Long = 0L
    private var scratch = FloatArray(0)

    @Volatile private var pendingRoute: Route? = null
    @Volatile private var bypass = false
    @Volatile private var mix = 1.0
    @Volatile private var macros = Macros()
    @Volatile private var preset = Preset.AUTO
    @Volatile private var loudnessMode = LoudnessMode.NORMALIZED
    @Volatile private var currentRoute = Route.HEADPHONE

    /** Ativo somente se a biblioteca nativa carregou. Senão, passthrough. */
    val engineAvailable get() = NativeEngine.available

    fun setRoute(r: Route) { pendingRoute = r }
    fun setBypass(on: Boolean) { bypass = on; if (handle != 0L) NativeEngine.rtSetBypass(handle, on) }
    fun isBypassed() = bypass

    /**
     * Posicao do A/B: 0.0 = original, 1.0 = aprimorado. O caminho seco sai
     * atrasado pela latencia da cadeia, entao posicoes intermediarias nao
     * viram filtro pente. Verificado no host: em 0.0 a saida e o original
     * atrasado 110 amostras, bit-exato.
     */
    fun setMix(wet: Double) { mix = wet; if (handle != 0L) NativeEngine.rtSetMix(handle, wet) }
    fun currentMix() = mix
    fun setMacros(m: Macros) {
        macros = m
        if (handle != 0L) NativeEngine.rtSetMacros(handle, m.bass, m.mid, m.treble,
            m.vocal, m.clarity, m.stereo, m.loudness, m.compression)
    }
    fun setPreset(p: Preset) { preset = p; recreate() }
    fun setLoudnessMode(m: LoudnessMode) { loudnessMode = m; recreate() }
    fun latencyMs(): Double =
        if (handle == 0L || inputFormat.sampleRate <= 0) 0.0
        else NativeEngine.rtLatencyFrames(handle) * 1000.0 / inputFormat.sampleRate

    override fun configure(input: AudioFormat): AudioFormat {
        // Exigimos float PCM: processar em 16 bits antes do limiter destruiria
        // headroom e reintroduziria justamente o clipping que queremos evitar.
        if (input.encoding != C.ENCODING_PCM_16BIT && input.encoding != C.ENCODING_PCM_FLOAT) {
            throw AudioProcessor.UnhandledAudioFormatException(input)
        }
        inputFormat = input
        outputFormat = AudioFormat(input.sampleRate, input.channelCount, C.ENCODING_PCM_FLOAT)
        recreate()
        return outputFormat
    }

    private fun recreate() {
        releaseHandle()
        if (!NativeEngine.available || inputFormat == AudioFormat.NOT_SET) return
        handle = runCatching {
            NativeEngine.rtCreate(inputFormat.sampleRate, inputFormat.channelCount,
                currentRoute.ordinal, preset.ordinal, loudnessMode.ordinal)
        }.getOrDefault(0L)
        if (handle != 0L) {
            NativeEngine.rtSetBypass(handle, bypass)
            NativeEngine.rtSetMix(handle, mix)
            setMacros(macros)
        }
    }

    private fun releaseHandle() {
        if (handle != 0L) { runCatching { NativeEngine.rtDestroy(handle) }; handle = 0L }
    }

    override fun isActive(): Boolean = inputFormat != AudioFormat.NOT_SET

    override fun queueInput(input: ByteBuffer) {
        val remaining = input.remaining()
        if (remaining == 0) return
        val ch = inputFormat.channelCount
        val bytesPerSample = if (inputFormat.encoding == C.ENCODING_PCM_FLOAT) 4 else 2
        val samples = remaining / bytesPerSample
        val frames = samples / ch

        if (scratch.size < samples) scratch = FloatArray(samples)

        val src = input.order(ByteOrder.nativeOrder())
        if (bytesPerSample == 4) {
            val fb = src.asFloatBuffer()
            fb.get(scratch, 0, samples)
        } else {
            for (i in 0 until samples) scratch[i] = src.short / 32768.0f
        }
        input.position(input.position() + remaining)

        pendingRoute?.let { r ->
            if (r != currentRoute) {
                currentRoute = r
                if (handle != 0L) NativeEngine.rtSetRoute(handle, r.ordinal)
            }
            pendingRoute = null
        }

        if (handle != 0L && !bypass) {
            runCatching { NativeEngine.rtProcess(handle, scratch, frames) }
                .onFailure { bypass = true }   // fallback: nunca silenciar a música
        }

        val outBytes = samples * 4
        if (buffer.capacity() < outBytes) {
            buffer = ByteBuffer.allocateDirect(outBytes).order(ByteOrder.nativeOrder())
        }
        buffer.clear()
        buffer.asFloatBuffer().put(scratch, 0, samples)
        buffer.limit(outBytes)
        outputBuffer = buffer
    }

    override fun getOutput(): ByteBuffer {
        val out = outputBuffer
        outputBuffer = AudioProcessor.EMPTY_BUFFER
        return out
    }

    override fun queueEndOfStream() { inputEnded = true }
    override fun isEnded(): Boolean = inputEnded && outputBuffer === AudioProcessor.EMPTY_BUFFER

    override fun flush() {
        outputBuffer = AudioProcessor.EMPTY_BUFFER
        inputEnded = false
        recreate()
    }

    override fun reset() {
        flush()
        releaseHandle()
        buffer = AudioProcessor.EMPTY_BUFFER
        inputFormat = AudioFormat.NOT_SET
        outputFormat = AudioFormat.NOT_SET
    }
}

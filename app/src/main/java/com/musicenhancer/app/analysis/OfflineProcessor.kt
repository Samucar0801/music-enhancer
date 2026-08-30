package com.musicenhancer.app.analysis

import android.content.Context
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import com.musicenhancer.app.dsp.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.coroutines.coroutineContext

data class DecodedAudio(val pcm: FloatArray, val sampleRate: Int, val channels: Int) {
    val frames get() = pcm.size / channels
    override fun equals(other: Any?) = this === other
    override fun hashCode() = System.identityHashCode(this)
}

sealed interface EnhanceProgress {
    data class Stage(val label: String, val fraction: Float) : EnhanceProgress
    data class Done(val result: OfflineResult) : EnhanceProgress
    data class Failed(val reason: String, val fallback: String) : EnhanceProgress
    /** Já processada antes com a MESMA versão de motor e os mesmos ajustes. */
    data class Cached(val entry: com.musicenhancer.app.cache.CacheEntry) : EnhanceProgress
}

/**
 * Decodifica o arquivo inteiro para PCM float e roda o motor offline.
 *
 * Cuidados de memória (regra 59): 1 min de estéreo 44,1 kHz em float = ~21 MB.
 * Uma faixa de 6 min ≈ 127 MB. Recusamos faixas absurdamente longas em vez de
 * estourar a heap, e nesse caso o app cai para o modo tempo real.
 */
object OfflineProcessor {

    private const val MAX_FRAMES = 44100L * 60 * 12      // 12 min

    suspend fun decode(ctx: Context, uri: Uri): Result<DecodedAudio> =
        withContext(Dispatchers.IO) {
            val extractor = MediaExtractor()
            var codec: MediaCodec? = null
            try {
                extractor.setDataSource(ctx, uri, null)
                var trackIndex = -1
                var format: MediaFormat? = null
                for (i in 0 until extractor.trackCount) {
                    val f = extractor.getTrackFormat(i)
                    if (f.getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true) {
                        trackIndex = i; format = f; break
                    }
                }
                if (trackIndex < 0 || format == null)
                    return@withContext Result.failure(IllegalStateException("Nenhuma trilha de áudio"))

                val durUs = runCatching { format.getLong(MediaFormat.KEY_DURATION) }.getOrDefault(0L)
                val sr = format.getInteger(MediaFormat.KEY_SAMPLE_RATE)
                if (durUs > 0 && (durUs / 1_000_000.0 * sr) > MAX_FRAMES)
                    return@withContext Result.failure(
                        IllegalStateException("Faixa longa demais para o modo qualidade máxima"))

                extractor.selectTrack(trackIndex)
                val mime = format.getString(MediaFormat.KEY_MIME)!!
                codec = MediaCodec.createDecoderByType(mime)
                codec.configure(format, null, null, 0)
                codec.start()

                val out = ArrayList<FloatArray>(256)
                var totalSamples = 0
                var channels = format.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
                var sampleRate = sr
                var sawEos = false
                val info = MediaCodec.BufferInfo()

                while (!sawEos) {
                    coroutineContext.ensureActive()
                    val inIdx = codec.dequeueInputBuffer(10_000)
                    if (inIdx >= 0) {
                        val buf = codec.getInputBuffer(inIdx)!!
                        val n = extractor.readSampleData(buf, 0)
                        if (n < 0) {
                            codec.queueInputBuffer(inIdx, 0, 0, 0,
                                MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                        } else {
                            codec.queueInputBuffer(inIdx, 0, n, extractor.sampleTime, 0)
                            extractor.advance()
                        }
                    }
                    val outIdx = codec.dequeueOutputBuffer(info, 10_000)
                    when {
                        outIdx >= 0 -> {
                            val buf = codec.getOutputBuffer(outIdx)!!
                            if (info.size > 0) {
                                val chunk = toFloat(buf, info, codec.outputFormat)
                                out.add(chunk); totalSamples += chunk.size
                            }
                            codec.releaseOutputBuffer(outIdx, false)
                            if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) sawEos = true
                        }
                        outIdx == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                            channels = codec.outputFormat.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
                            sampleRate = codec.outputFormat.getInteger(MediaFormat.KEY_SAMPLE_RATE)
                        }
                    }
                    if (totalSamples > MAX_FRAMES * 2)
                        return@withContext Result.failure(OutOfMemoryError("Faixa longa demais"))
                }

                val pcm = FloatArray(totalSamples)
                var p = 0
                for (c in out) { c.copyInto(pcm, p); p += c.size }
                Result.success(DecodedAudio(pcm, sampleRate, channels))
            } catch (t: Throwable) {
                Result.failure(t)
            } finally {
                runCatching { codec?.stop(); codec?.release() }
                runCatching { extractor.release() }
            }
        }

    private fun toFloat(buf: ByteBuffer, info: MediaCodec.BufferInfo, fmt: MediaFormat): FloatArray {
        buf.position(info.offset); buf.limit(info.offset + info.size)
        val enc = runCatching { fmt.getInteger(MediaFormat.KEY_PCM_ENCODING) }
            .getOrDefault(2 /* ENCODING_PCM_16BIT */)
        buf.order(ByteOrder.nativeOrder())
        return when (enc) {
            4 -> {                                        // ENCODING_PCM_FLOAT
                val fb = buf.asFloatBuffer(); FloatArray(fb.remaining()).also { fb.get(it) }
            }
            else -> {
                val sb = buf.asShortBuffer()
                FloatArray(sb.remaining()) { sb.get(it) / 32768.0f }
            }
        }
    }
}

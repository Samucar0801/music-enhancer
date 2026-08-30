package com.musicenhancer.app.cache

import android.content.Context
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import java.io.File
import java.security.MessageDigest

/** Versões que invalidam o cache quando o processamento muda. (Regras 80/81) */
object Versions {
    const val APP = "1.0.0"
    const val DSP = "1.0"
    const val ANALYSIS = "1.0"
    const val AI_MODEL = "nenhum"
}

@Serializable
data class CacheEntry(
    val cacheKey: String, val appVersion: String, val dspVersion: String,
    val preset: String, val route: String, val loudnessMode: String, val targetLufs: Double,
    val sampleRate: Int, val channels: Int, val frames: Long,
    val lufsBefore: Double, val lufsAfter: Double,
    val truePeakBefore: Double, val truePeakAfter: Double,
    val crestBefore: Double, val crestAfter: Double,
    val revertedToOriginal: Boolean, val guardIterations: Int,
    val processingSeconds: Double, val createdAt: Long
) {
    fun matches(key: String, preset: String, route: String, lm: String, lufs: Double) =
        cacheKey == key && appVersion == Versions.APP && dspVersion == Versions.DSP &&
        this.preset == preset && this.route == route && loudnessMode == lm &&
        kotlin.math.abs(targetLufs - lufs) < 0.01
}

/**
 * Cache de áudio já aprimorado. O ARQUIVO ORIGINAL NUNCA É TOCADO (regra 19):
 * tudo vive em cacheDir, que o Android pode limpar sozinho se faltar espaço.
 */
class CacheManager(private val ctx: Context) {

    private val json = Json { ignoreUnknownKeys = true; prettyPrint = true }
    private val root = File(ctx.cacheDir, "music_cache").apply { mkdirs() }

    private fun hash(key: String): String =
        MessageDigest.getInstance("SHA-256").digest(key.toByteArray())
            .joinToString("") { "%02x".format(it) }.take(32)

    private fun dirFor(key: String) = File(root, hash(key))

    fun lookup(key: String, preset: String, route: String, lm: String, lufs: Double): CacheEntry? {
        val meta = File(dirFor(key), "meta.json")
        if (!meta.exists()) return null
        val e = runCatching { json.decodeFromString<CacheEntry>(meta.readText()) }.getOrNull()
            ?: return null
        if (!e.matches(key, preset, route, lm, lufs)) return null
        if (!File(dirFor(key), "audio.pcm").exists()) return null
        return e
    }

    fun audioFile(key: String) = File(dirFor(key), "audio.pcm")

    /**
     * Grava atomicamente: escrevemos em .tmp e só renomeamos no fim. Se o app
     * for fechado no meio, o cache não fica corrompido. (Regras 98/99)
     */
    fun store(key: String, entry: CacheEntry, pcm: FloatArray): Boolean = runCatching {
        val dir = dirFor(key).apply { mkdirs() }
        val tmp = File(dir, "audio.pcm.tmp")
        java.io.DataOutputStream(tmp.outputStream().buffered(1 shl 16)).use { o ->
            for (v in pcm) o.writeFloat(v)
        }
        if (!tmp.renameTo(File(dir, "audio.pcm"))) return false
        File(dir, "meta.json").writeText(json.encodeToString(entry))
        true
    }.getOrDefault(false)

    fun readPcm(key: String, frames: Long, channels: Int): FloatArray? = runCatching {
        val f = audioFile(key)
        val n = (frames * channels).toInt()
        val out = FloatArray(n)
        java.io.DataInputStream(f.inputStream().buffered(1 shl 16)).use { i ->
            for (k in 0 until n) out[k] = i.readFloat()
        }
        out
    }.getOrNull()

    fun sizeBytes(): Long = root.walkTopDown().filter { it.isFile }.sumOf { it.length() }

    fun clearAll() { root.deleteRecursively(); root.mkdirs() }

    fun remove(key: String) { dirFor(key).deleteRecursively() }

    /** Remove as entradas mais antigas até caber no limite escolhido. */
    fun enforceLimit(limitBytes: Long) {
        if (limitBytes <= 0) return                         // "sem limite"
        var total = sizeBytes()
        if (total <= limitBytes) return
        root.listFiles()?.sortedBy { it.lastModified() }?.forEach { dir ->
            if (total <= limitBytes) return
            total -= dir.walkTopDown().filter { it.isFile }.sumOf { it.length() }
            dir.deleteRecursively()
        }
    }
}

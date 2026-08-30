package com.musicenhancer.app.online

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder

enum class Confidence { HIGH, MEDIUM, LOW }

data class OnlineContext(
    val title: String?, val artist: String?, val album: String?,
    val year: String?, val tags: List<String>, val confidence: Confidence,
    val source: String = "MusicBrainz"
)

/**
 * Contexto opcional da música via MusicBrainz.
 *
 * Escolhido porque é gratuito, sem chave de API, sem cadastro e com dados
 * sob CC0. Só exige um User-Agent identificável e no máximo 1 req/s.
 *
 * PRIVACIDADE (regra 8): enviamos APENAS título/artista/álbum em texto.
 * O arquivo de áudio nunca sai do aparelho. Se a rede falhar, a análise
 * local continua e é ela que manda de qualquer forma. (Regra 7)
 */
object MusicBrainzClient {

    private const val UA = "MusicEnhancer/1.0.0 (aplicativo local de reprodução; sem servidor)"
    private const val BASE = "https://musicbrainz.org/ws/2/recording"
    private var lastCallMs = 0L

    suspend fun lookup(title: String?, artist: String?, album: String?): OnlineContext? =
        withContext(Dispatchers.IO) {
            if (title.isNullOrBlank()) return@withContext null
            // Respeita o limite de 1 requisição por segundo do serviço.
            val since = System.currentTimeMillis() - lastCallMs
            if (since < 1100) kotlinx.coroutines.delay(1100 - since)
            lastCallMs = System.currentTimeMillis()

            val q = buildString {
                append("recording:\"").append(title.replace("\"", "")).append("\"")
                if (!artist.isNullOrBlank()) append(" AND artist:\"")
                    .append(artist.replace("\"", "")).append("\"")
                if (!album.isNullOrBlank()) append(" AND release:\"")
                    .append(album.replace("\"", "")).append("\"")
            }
            val url = "$BASE?query=${URLEncoder.encode(q, "UTF-8")}&fmt=json&limit=3"

            runCatching {
                val c = (URL(url).openConnection() as HttpURLConnection).apply {
                    setRequestProperty("User-Agent", UA)
                    setRequestProperty("Accept", "application/json")
                    connectTimeout = 6000; readTimeout = 8000
                }
                if (c.responseCode != 200) return@runCatching null
                val body = c.inputStream.bufferedReader().use { it.readText() }
                c.disconnect()
                parse(body)
            }.getOrNull()
        }

    private fun parse(body: String): OnlineContext? {
        val recs = JSONObject(body).optJSONArray("recordings") ?: return null
        if (recs.length() == 0) return null
        val top = recs.getJSONObject(0)
        val score = top.optInt("score", 0)
        // Nunca tratamos uma única fonte como verdade absoluta. (Regra 86)
        val conf = when {
            score >= 92 -> Confidence.HIGH
            score >= 70 -> Confidence.MEDIUM
            else -> Confidence.LOW
        }
        val artist = top.optJSONArray("artist-credit")
            ?.optJSONObject(0)?.optString("name")?.takeIf { it.isNotBlank() }
        val release = top.optJSONArray("releases")?.optJSONObject(0)
        val tags = mutableListOf<String>()
        top.optJSONArray("tags")?.let { arr ->
            for (i in 0 until arr.length()) arr.optJSONObject(i)?.optString("name")
                ?.takeIf { it.isNotBlank() }?.let(tags::add)
        }
        return OnlineContext(
            title = top.optString("title").takeIf { it.isNotBlank() },
            artist = artist,
            album = release?.optString("title")?.takeIf { it.isNotBlank() },
            year = top.optString("first-release-date").takeIf { it.isNotBlank() }?.take(4),
            tags = tags, confidence = conf
        )
    }
}

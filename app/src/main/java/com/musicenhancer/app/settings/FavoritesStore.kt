package com.musicenhancer.app.settings

import android.content.Context
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.withContext
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import java.io.File

@Serializable data class PlayHistoryEntry(val uri: String, val positionMs: Long, val playedAt: Long)
@Serializable private data class Persisted(
    val favorites: Set<String> = emptySet(),
    val history: List<PlayHistoryEntry> = emptyList()
)

/**
 * Favoritos e histórico ficam SÓ no aparelho, em arquivo JSON local.
 * Nada é enviado para servidor algum. (Regras 22/64/90)
 */
class FavoritesStore(ctx: Context) {
    private val file = File(ctx.filesDir, "favorites.json")
    private val json = Json { ignoreUnknownKeys = true }

    private val _favorites = MutableStateFlow<Set<String>>(emptySet())
    val favorites: StateFlow<Set<String>> = _favorites
    private val _history = MutableStateFlow<List<PlayHistoryEntry>>(emptyList())
    val history: StateFlow<List<PlayHistoryEntry>> = _history

    init { load() }

    private fun load() {
        val p = runCatching { json.decodeFromString<Persisted>(file.readText()) }
            .getOrDefault(Persisted())
        _favorites.value = p.favorites
        _history.value = p.history
    }

    private suspend fun persist() = withContext(Dispatchers.IO) {
        runCatching {
            val tmp = File(file.parentFile, "favorites.json.tmp")
            tmp.writeText(json.encodeToString(Persisted(_favorites.value, _history.value)))
            tmp.renameTo(file)
        }
    }

    fun isFavorite(uri: String) = uri in _favorites.value

    suspend fun toggleFavorite(uri: String) {
        _favorites.value = _favorites.value.toMutableSet().apply {
            if (!add(uri)) remove(uri)
        }
        persist()
    }

    /** Guarda a posição para oferecer "continuar de 02:31?". (Regra 91) */
    suspend fun recordPosition(uri: String, positionMs: Long) {
        val now = System.currentTimeMillis()
        _history.value = (listOf(PlayHistoryEntry(uri, positionMs, now)) +
            _history.value.filterNot { it.uri == uri }).take(200)
        persist()
    }

    fun resumePositionFor(uri: String): Long? =
        _history.value.firstOrNull { it.uri == uri }
            ?.takeIf { it.positionMs > 15_000 }?.positionMs
}

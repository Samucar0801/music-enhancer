package com.musicenhancer.app.library

import android.content.Context
import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import com.musicenhancer.app.metadata.TrackMetadata
import com.musicenhancer.app.metadata.MetadataReader

data class Track(
    val uri: Uri, val displayName: String, val sizeBytes: Long, val lastModified: Long,
    val meta: TrackMetadata? = null
) {
    val title get() = meta?.title?.takeIf { it.isNotBlank() } ?: displayName.substringBeforeLast('.')
    val artist get() = meta?.artist?.takeIf { it.isNotBlank() }
    val album get() = meta?.album?.takeIf { it.isNotBlank() }
    /** Identidade estável para o cache: muda se o arquivo mudar. (Regra 18) */
    val cacheKey get() = "${uri}|$sizeBytes|$lastModified"
}

enum class SortOrder { NAME, TRACK_NUMBER, ARTIST, ALBUM, ORIGINAL }

/**
 * Uma pasta = uma playlist. O usuário já organiza a música dele; não vamos
 * obrigá-lo a recriar isso dentro do app. (Regra 21)
 */
object FolderScanner {

    private val AUDIO_EXT = setOf("mp3", "wav", "flac", "m4a", "aac", "ogg", "opus", "wma")

    fun scan(ctx: Context, treeUri: Uri, recursive: Boolean = true): List<Track> {
        val root = DocumentFile.fromTreeUri(ctx, treeUri) ?: return emptyList()
        val out = mutableListOf<Track>()
        collect(root, out, recursive, 0)
        return out
    }

    private fun collect(dir: DocumentFile, out: MutableList<Track>, recursive: Boolean, depth: Int) {
        if (depth > 6) return                                  // proteção contra ciclos
        for (f in dir.listFiles()) {
            when {
                f.isDirectory && recursive -> collect(f, out, true, depth + 1)
                f.isFile -> {
                    val name = f.name ?: continue
                    if (name.substringAfterLast('.', "").lowercase() in AUDIO_EXT) {
                        out.add(Track(f.uri, name, f.length(), f.lastModified()))
                    }
                }
            }
        }
    }

    /** Lê metadados sob demanda: abrir 500 arquivos de uma vez trava a UI. */
    fun withMetadata(ctx: Context, tracks: List<Track>): List<Track> =
        tracks.map { it.copy(meta = MetadataReader.read(ctx, it.uri)) }

    fun sort(tracks: List<Track>, order: SortOrder): List<Track> = when (order) {
        SortOrder.NAME -> tracks.sortedWith(compareBy(NaturalOrder) { it.displayName })
        SortOrder.TRACK_NUMBER -> tracks.sortedWith(
            compareBy<Track> { it.meta?.trackNumber ?: Int.MAX_VALUE }
                .thenBy(NaturalOrder) { it.displayName })
        SortOrder.ARTIST -> tracks.sortedWith(
            compareBy<Track> { it.artist ?: "\uFFFF" }.thenBy(NaturalOrder) { it.displayName })
        SortOrder.ALBUM -> tracks.sortedWith(
            compareBy<Track> { it.album ?: "\uFFFF" }
                .thenBy { it.meta?.trackNumber ?: Int.MAX_VALUE })
        SortOrder.ORIGINAL -> tracks
    }

    /** "hino2.mp3" antes de "hino10.mp3" — ordenação alfabética pura erra isso. */
    private object NaturalOrder : Comparator<String> {
        override fun compare(a: String, b: String): Int {
            var i = 0; var j = 0
            while (i < a.length && j < b.length) {
                val ca = a[i]; val cb = b[j]
                if (ca.isDigit() && cb.isDigit()) {
                    var si = i; var sj = j
                    while (i < a.length && a[i].isDigit()) i++
                    while (j < b.length && b[j].isDigit()) j++
                    val na = a.substring(si, i).trimStart('0').ifEmpty { "0" }
                    val nb = b.substring(sj, j).trimStart('0').ifEmpty { "0" }
                    if (na.length != nb.length) return na.length - nb.length
                    val c = na.compareTo(nb); if (c != 0) return c
                } else {
                    val c = ca.lowercaseChar().compareTo(cb.lowercaseChar())
                    if (c != 0) return c
                    i++; j++
                }
            }
            return (a.length - i) - (b.length - j)
        }
    }
}

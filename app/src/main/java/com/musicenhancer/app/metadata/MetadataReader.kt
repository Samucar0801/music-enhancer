package com.musicenhancer.app.metadata

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.MediaMetadataRetriever
import android.net.Uri

data class TrackMetadata(
    val title: String?, val artist: String?, val album: String?, val albumArtist: String?,
    val year: String?, val genre: String?, val trackNumber: Int?, val composer: String?,
    val durationMs: Long?, val bitrateBps: Int?, val mimeType: String?,
    val sampleRate: Int?, val channels: Int?, val hasArtwork: Boolean
) {
    /** Descreve a fonte sem exagerar o que sabemos. (Regra 32) */
    fun qualityLabel(): String {
        val kbps = bitrateBps?.let { it / 1000 }
        val fmt = mimeType?.substringAfterLast('/')?.uppercase() ?: "?"
        return buildString {
            append(fmt)
            kbps?.let { append(" · $it kbps") }
            sampleRate?.let { append(" · ${it / 1000f} kHz".replace(".0 ", " ")) }
            channels?.let { append(if (it == 1) " · mono" else " · estéreo") }
        }
    }
}

object MetadataReader {

    /** Prioridade: metadados do arquivo > nome do arquivo > pesquisa online. */
    fun read(ctx: Context, uri: Uri): TrackMetadata? {
        val r = MediaMetadataRetriever()
        return try {
            r.setDataSource(ctx, uri)
            fun g(k: Int) = r.extractMetadata(k)?.takeIf { it.isNotBlank() }
            TrackMetadata(
                title = g(MediaMetadataRetriever.METADATA_KEY_TITLE),
                artist = g(MediaMetadataRetriever.METADATA_KEY_ARTIST),
                album = g(MediaMetadataRetriever.METADATA_KEY_ALBUM),
                albumArtist = g(MediaMetadataRetriever.METADATA_KEY_ALBUMARTIST),
                year = g(MediaMetadataRetriever.METADATA_KEY_YEAR),
                genre = g(MediaMetadataRetriever.METADATA_KEY_GENRE),
                trackNumber = g(MediaMetadataRetriever.METADATA_KEY_CD_TRACK_NUMBER)
                    ?.substringBefore('/')?.trim()?.toIntOrNull(),
                composer = g(MediaMetadataRetriever.METADATA_KEY_COMPOSER),
                durationMs = g(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull(),
                bitrateBps = g(MediaMetadataRetriever.METADATA_KEY_BITRATE)?.toIntOrNull(),
                mimeType = g(MediaMetadataRetriever.METADATA_KEY_MIMETYPE),
                sampleRate = g(MediaMetadataRetriever.METADATA_KEY_SAMPLERATE)?.toIntOrNull(),
                channels = g(31)?.toIntOrNull(),   // METADATA_KEY_NUM_TRACKS varia por versão
                hasArtwork = r.embeddedPicture != null
            )
        } catch (t: Throwable) {
            null                                   // arquivo corrompido: nunca derruba o app
        } finally {
            runCatching { r.release() }
        }
    }

    fun artwork(ctx: Context, uri: Uri, maxPx: Int = 512): Bitmap? {
        val r = MediaMetadataRetriever()
        return try {
            r.setDataSource(ctx, uri)
            val bytes = r.embeddedPicture ?: return null
            val opts = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeByteArray(bytes, 0, bytes.size, opts)
            var s = 1
            while (opts.outWidth / s > maxPx || opts.outHeight / s > maxPx) s *= 2
            BitmapFactory.decodeByteArray(bytes, 0, bytes.size,
                BitmapFactory.Options().apply { inSampleSize = s })
        } catch (t: Throwable) { null } finally { runCatching { r.release() } }
    }
}

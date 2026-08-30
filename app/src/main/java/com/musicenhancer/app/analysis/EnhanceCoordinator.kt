package com.musicenhancer.app.analysis

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import com.musicenhancer.app.MusicEnhancerApp
import com.musicenhancer.app.cache.CacheEntry
import com.musicenhancer.app.cache.Versions
import com.musicenhancer.app.dsp.*
import com.musicenhancer.app.library.Track
import com.musicenhancer.app.settings.Settings
import com.musicenhancer.app.util.Logger
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.flowOn
import kotlinx.coroutines.Dispatchers

/**
 * Liga decodificação → DSP → cache. O MP3 original NUNCA é tocado:
 * a saída vai para o diretório de cache do app, em arquivo separado.
 * (Regras 47/48/49)
 */
object EnhanceCoordinator {

    fun run(ctx: Context, track: Track, route: Route, s: Settings): Flow<EnhanceProgress> = flow {
        val app = ctx.applicationContext as MusicEnhancerApp
        val key = track.cacheKey
        val presetName = s.preset.name
        val routeName = route.name

        emit(EnhanceProgress.Stage("Verificando cache", 0.02f))
        val hit = app.cache.lookup(key, presetName, routeName, s.loudnessMode.name, s.targetLufs)
        if (hit != null) {
            Logger.i("Cache válido reaproveitado (versão DSP ${hit.dspVersion})")
            emit(EnhanceProgress.Cached(hit))
            return@flow
        }

        if (!NativeEngine.available) {
            emit(EnhanceProgress.Failed(
                NativeEngine.loadError ?: "Motor nativo indisponível",
                "Reproduzindo o arquivo original sem alteração."))
            return@flow
        }

        notify(ctx, "Decodificando", 5)
        emit(EnhanceProgress.Stage("Decodificando o arquivo", 0.10f))
        val decoded = OfflineProcessor.decode(ctx, track.uri).getOrElse { e ->
            emit(EnhanceProgress.Failed("Falha ao decodificar: ${e.message}",
                "Reproduzindo o arquivo original sem alteração."))
            return@flow
        }

        notify(ctx, "Analisando", 30)
        emit(EnhanceProgress.Stage("Analisando o que a música precisa", 0.35f))

        notify(ctx, "Aprimorando", 55)
        emit(EnhanceProgress.Stage("Aplicando correções e verificando", 0.60f))
        val result = NativeEngine.processOffline(
            decoded.pcm, decoded.sampleRate, decoded.channels,
            route, s.preset, s.loudnessMode, s.targetLufs)
        if (result == null) {
            emit(EnhanceProgress.Failed("O motor recusou o material",
                "Reproduzindo o arquivo original sem alteração."))
            return@flow
        }

        emit(EnhanceProgress.Stage("Guardando resultado", 0.90f))
        val entry = CacheEntry(
            cacheKey = key, appVersion = Versions.APP, dspVersion = Versions.DSP,
            preset = presetName, route = routeName, loudnessMode = s.loudnessMode.name,
            targetLufs = s.targetLufs,
            sampleRate = decoded.sampleRate, channels = decoded.channels,
            frames = (result.pcm.size / decoded.channels).toLong(),
            lufsBefore = result.before.integratedLufs, lufsAfter = result.after.integratedLufs,
            truePeakBefore = result.before.truePeakDbtp, truePeakAfter = result.after.truePeakDbtp,
            crestBefore = result.before.crestFactorDb, crestAfter = result.after.crestFactorDb,
            revertedToOriginal = result.revertedToOriginal,
            guardIterations = result.guardIterations,
            processingSeconds = result.processingSeconds,
            createdAt = System.currentTimeMillis())
        val stored = app.cache.store(key, entry, result.pcm)
        app.cache.enforceLimit(s.cacheLimitMb * 1024 * 1024)
        if (!stored) Logger.w("Não foi possível gravar o cache (espaço?)")
        stop(ctx)
        emit(EnhanceProgress.Done(result))
    }.flowOn(Dispatchers.Default)

    private fun notify(ctx: Context, label: String, pct: Int) {
        val i = Intent(ctx, EnhanceService::class.java)
            .putExtra(EnhanceService.EXTRA_LABEL, label)
            .putExtra(EnhanceService.EXTRA_PROGRESS, pct)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) ctx.startForegroundService(i)
        else ctx.startService(i)
    }

    private fun stop(ctx: Context) {
        runCatching { ctx.stopService(Intent(ctx, EnhanceService::class.java)) }
    }

    fun cancel() = runCatching { NativeEngine.cancelOffline() }
}

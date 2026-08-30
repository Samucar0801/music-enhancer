package com.musicenhancer.app.playback

import android.app.PendingIntent
import android.content.Intent
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.exoplayer.audio.DefaultAudioSink
import androidx.media3.exoplayer.audio.MediaCodecAudioRenderer
import androidx.media3.exoplayer.DefaultRenderersFactory
import androidx.media3.exoplayer.Renderer
import androidx.media3.exoplayer.mediacodec.MediaCodecSelector
import androidx.media3.common.AudioAttributes
import androidx.media3.common.C
import androidx.media3.session.MediaSession
import androidx.media3.session.MediaSessionService
import android.os.Handler
import android.media.audiofx.LoudnessEnhancer
import com.musicenhancer.app.MainActivity
import com.musicenhancer.app.audio.OutputRouteMonitor
import com.musicenhancer.app.dsp.*
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch

/**
 * Reprodução em segundo plano com MediaSession. Isso dá de graça:
 * controles na notificação, botões do fone, tela desligada, Android Auto.
 */
@UnstableApi
class PlaybackService : MediaSessionService() {

    private var session: MediaSession? = null
    private lateinit var player: ExoPlayer
    private lateinit var routeMonitor: OutputRouteMonitor
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    companion object {
        /** Compartilhado com a UI para A/B, bypass e macros em tempo real. */
        @Volatile var processor: EnhancerAudioProcessor? = null
            private set
    }

    override fun onCreate() {
        super.onCreate()
        val proc = EnhancerAudioProcessor()
        processor = proc

        // Injeta o processor no sink de áudio do ExoPlayer.
        val renderers = object : DefaultRenderersFactory(this) {
            override fun buildAudioRenderers(
                context: android.content.Context, extensionRendererMode: Int,
                mediaCodecSelector: MediaCodecSelector, enableDecoderFallback: Boolean,
                audioSink: androidx.media3.exoplayer.audio.AudioSink,
                eventHandler: Handler,
                eventListener: androidx.media3.exoplayer.audio.AudioRendererEventListener,
                out: ArrayList<Renderer>
            ) {
                val sink = DefaultAudioSink.Builder(context)
                    .setAudioProcessors(arrayOf(proc))
                    .setEnableFloatOutput(true)
                    .build()
                out.add(MediaCodecAudioRenderer(
                    context, mediaCodecSelector, eventHandler, eventListener, sink))
            }
        }.setExtensionRendererMode(DefaultRenderersFactory.EXTENSION_RENDERER_MODE_OFF)

        player = ExoPlayer.Builder(this, renderers)
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setContentType(C.AUDIO_CONTENT_TYPE_MUSIC)
                    .setUsage(C.USAGE_MEDIA).build(),
                /* handleAudioFocus = */ true)
            .setHandleAudioBecomingNoisy(true)     // pausa ao desconectar o fone
            .build()

        val openApp = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)

        session = MediaSession.Builder(this, player)
            .setSessionActivity(openApp)
            .build()

        routeMonitor = OutputRouteMonitor(this).also { it.start() }
        scope.launch {
            routeMonitor.state.collectLatest { st -> proc.setRoute(st.route) }
        }
    }

    override fun onGetSession(controllerInfo: MediaSession.ControllerInfo): MediaSession? = session

    override fun onTaskRemoved(rootIntent: Intent?) {
        // Se o usuário fecha o app e nada está tocando, encerramos o serviço
        // em vez de deixar uma notificação órfã.
        if (!player.playWhenReady || player.mediaItemCount == 0) {
            stopSelf()
        }
    }

    override fun onDestroy() {
        routeMonitor.stop()
        session?.run { player.release(); release() }
        session = null
        processor = null
        super.onDestroy()
    }
}

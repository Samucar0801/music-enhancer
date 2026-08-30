package com.musicenhancer.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.session.MediaController
import com.musicenhancer.app.MusicEnhancerApp
import com.musicenhancer.app.audio.OutputRouteMonitor
import com.musicenhancer.app.audio.OutputState
import com.musicenhancer.app.dsp.NativeEngine
import com.musicenhancer.app.dsp.Route
import com.musicenhancer.app.playback.PlaybackService
import com.musicenhancer.app.ui.theme.*
import kotlinx.coroutines.delay

@UnstableApi
@Composable
fun PlayerScreen(app: MusicEnhancerApp, controller: MediaController?) {
    val ctx = LocalContext.current
    val monitor = remember { OutputRouteMonitor(ctx) }
    val out by monitor.state.collectAsState()
    DisposableEffect(Unit) { monitor.start(); onDispose { monitor.stop() } }

    val proc = PlaybackService.processor
    var mix by remember { mutableFloatStateOf(1f) }
    var bypass by remember { mutableStateOf(false) }
    var title by remember { mutableStateOf("—") }
    var artist by remember { mutableStateOf<String?>(null) }
    var playing by remember { mutableStateOf(false) }
    var positionMs by remember { mutableLongStateOf(0L) }
    var durationMs by remember { mutableLongStateOf(0L) }
    var queueSize by remember { mutableIntStateOf(0) }
    var queueIndex by remember { mutableIntStateOf(0) }

    // Rota muda -> o motor precisa saber. Fone e alto-falante pedem decisões
    // diferentes; aplicar a mesma curva nos dois é o erro clássico.
    LaunchedEffect(out.route) { proc?.setRoute(out.route) }

    LaunchedEffect(controller) {
        while (true) {
            controller?.let { c ->
                title = c.mediaMetadata.title?.toString() ?: "—"
                artist = c.mediaMetadata.artist?.toString()
                playing = c.isPlaying
                positionMs = c.currentPosition.coerceAtLeast(0)
                durationMs = c.duration.takeIf { it > 0 } ?: 0
                queueSize = c.mediaItemCount
                queueIndex = c.currentMediaItemIndex
            }
            delay(500)
        }
    }

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())
        .padding(horizontal = 20.dp)) {

        Spacer(Modifier.height(24.dp))
        Box(
            Modifier.fillMaxWidth().aspectRatio(1f).clip(RoundedCornerShape(20.dp))
                .background(Surface1), contentAlignment = Alignment.Center
        ) {
            Text("♪", style = MaterialTheme.typography.displayLarge, color = AccentDim)
        }

        Spacer(Modifier.height(20.dp))
        Text(title, style = MaterialTheme.typography.headlineSmall,
            fontWeight = FontWeight.SemiBold, maxLines = 2)
        Text(artist ?: "Artista não informado no arquivo",
            style = MaterialTheme.typography.bodyMedium, color = TextMid)

        if (queueSize > 0) {
            Spacer(Modifier.height(4.dp))
            Text("Faixa ${queueIndex + 1} de $queueSize na fila da pasta",
                style = MaterialTheme.typography.bodySmall, color = TextMid)
        }

        Spacer(Modifier.height(16.dp))
        if (durationMs > 0) {
            Slider(
                value = positionMs.toFloat().coerceIn(0f, durationMs.toFloat()),
                onValueChange = { controller?.seekTo(it.toLong()) },
                valueRange = 0f..durationMs.toFloat())
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text(fmtTime(positionMs), style = MaterialTheme.typography.bodySmall, color = TextMid)
                Text(fmtTime(durationMs), style = MaterialTheme.typography.bodySmall, color = TextMid)
            }
        }

        Spacer(Modifier.height(8.dp))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = { controller?.seekToPreviousMediaItem() }) { Text("◀◀") }
            Spacer(Modifier.width(12.dp))
            FilledIconButton(
                onClick = { if (playing) controller?.pause() else controller?.play() },
                modifier = Modifier.size(64.dp)
            ) { Text(if (playing) "❚❚" else "▶", style = MaterialTheme.typography.titleLarge) }
            Spacer(Modifier.width(12.dp))
            TextButton(onClick = { controller?.seekToNextMediaItem() }) { Text("▶▶") }
        }

        // -------------------------------------------------- A/B
        SectionTitle("Comparação A/B")
        InfoCard {
            Text(
                if (bypass) "Bypass ligado: o sinal sai exatamente como está no arquivo."
                else "Arraste para comparar. Em 0% a saída é o original atrasado " +
                     "em 2,5 ms para alinhar com o processado — sem filtro pente.",
                style = MaterialTheme.typography.bodySmall, color = TextMid)
            Spacer(Modifier.height(12.dp))
            Slider(
                value = mix, enabled = !bypass,
                onValueChange = { mix = it; proc?.setMix(it.toDouble()) },
                valueRange = 0f..1f)
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text("Original", style = MaterialTheme.typography.bodySmall,
                    color = if (mix < 0.5f) Accent else TextMid)
                Text("${(mix * 100).toInt()}%", fontFamily = FontFamily.Monospace,
                    style = MaterialTheme.typography.bodySmall, color = TextMid)
                Text("Aprimorado", style = MaterialTheme.typography.bodySmall,
                    color = if (mix > 0.5f) Accent else TextMid)
            }
            Spacer(Modifier.height(8.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Switch(checked = bypass, onCheckedChange = {
                    bypass = it; proc?.setBypass(it)
                })
                Spacer(Modifier.width(12.dp))
                Column {
                    Text("Bypass total", style = MaterialTheme.typography.bodyMedium)
                    Text("Caminho bit-transparente, sem atraso",
                        style = MaterialTheme.typography.bodySmall, color = TextMid)
                }
            }
        }

        // -------------------------------------------------- saída
        SectionTitle("Saída de áudio")
        RouteCard(out, proc?.latencyMs())

        // -------------------------------------------------- motor
        SectionTitle("Motor")
        InfoCard {
            if (NativeEngine.available) {
                MetricRow("Núcleo DSP", NativeEngine.safeVersion())
                MetricRow("Modo", "Tempo real")
                MetricRow("Perfil aplicado", routeLabel(out.route))
            } else {
                Text("Motor nativo não carregou neste aparelho.",
                    style = MaterialTheme.typography.bodyMedium, color = Warn)
                UnknownNote(NativeEngine.loadError ?: "Motivo não informado pelo sistema.")
                UnknownNote("O áudio está tocando SEM processamento — o arquivo " +
                    "original não foi alterado.")
            }
        }
        Spacer(Modifier.height(32.dp))
    }
}

@Composable
private fun RouteCard(out: OutputState, latencyMs: Double?) {
    InfoCard {
        MetricRow("Rota detectada", routeLabel(out.route))
        MetricRow("Dispositivo", out.deviceName)
        if (out.route == Route.BLUETOOTH) {
            if (out.bluetoothCodec != null) {
                MetricRow("Codec Bluetooth", out.bluetoothCodec!!)
            } else {
                UnknownNote("Codec Bluetooth: não informado pelo Android neste " +
                    "aparelho. O app não adivinha — nenhuma decisão foi tomada " +
                    "com base em codec.")
            }
        }
        out.sampleRate?.let { MetricRow("Taxa de saída", "$it Hz") }
        latencyMs?.let { MetricRow("Latência do limiter", "%.1f ms".format(it)) }
        out.systemEffectWarning?.let {
            Spacer(Modifier.height(8.dp))
            UnknownNote(it)
        }
    }
}

private fun routeLabel(r: Route) = when (r) {
    Route.HEADPHONE -> "Fone com fio"
    Route.BLUETOOTH -> "Bluetooth"
    Route.SPEAKER -> "Alto-falante do aparelho"
    Route.CAR -> "Som do carro"
}

private fun fmtTime(ms: Long): String {
    val s = ms / 1000; return "%d:%02d".format(s / 60, s % 60)
}

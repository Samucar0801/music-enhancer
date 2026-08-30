package com.musicenhancer.app.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.media3.common.util.UnstableApi
import com.musicenhancer.app.MusicEnhancerApp
import com.musicenhancer.app.analysis.EnhanceCoordinator
import com.musicenhancer.app.analysis.EnhanceProgress
import com.musicenhancer.app.audio.OutputRouteMonitor
import com.musicenhancer.app.cache.CacheEntry
import com.musicenhancer.app.dsp.OfflineResult
import com.musicenhancer.app.library.Track
import com.musicenhancer.app.ui.theme.*
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch

@UnstableApi
@Composable
fun LibraryScreen(
    tracks: List<Track>,
    loading: Boolean,
    folderName: String?,
    app: MusicEnhancerApp,
    onPickFolder: () -> Unit,
    onPlay: (Int) -> Unit
) {
    var sheetFor by remember { mutableStateOf<Track?>(null) }

    Column(Modifier.fillMaxSize()) {
        Row(
            Modifier.fillMaxWidth().padding(20.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(Modifier.weight(1f)) {
                Text(folderName ?: "Nenhuma pasta",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold, maxLines = 1)
                Text(
                    if (tracks.isEmpty()) "Escolha a pasta com suas músicas"
                    else "${tracks.size} faixas — toca em sequência",
                    style = MaterialTheme.typography.bodySmall, color = TextMid)
            }
            Button(onClick = onPickFolder) { Text("Pasta") }
        }

        if (loading) {
            LinearProgressIndicator(Modifier.fillMaxWidth())
            Text("Lendo a pasta…", Modifier.padding(20.dp), color = TextMid)
        }

        if (tracks.isEmpty() && !loading) {
            Column(Modifier.fillMaxSize().padding(32.dp),
                verticalArrangement = Arrangement.Center,
                horizontalAlignment = Alignment.CenterHorizontally) {
                Text("♪", style = MaterialTheme.typography.displayMedium, color = AccentDim)
                Spacer(Modifier.height(16.dp))
                Text("Selecione uma pasta inteira",
                    style = MaterialTheme.typography.titleMedium)
                Spacer(Modifier.height(8.dp))
                Text("Toda a pasta vira uma fila. A próxima música entra sozinha — " +
                     "você não precisa escolher uma a uma.",
                    style = MaterialTheme.typography.bodyMedium, color = TextMid)
                Spacer(Modifier.height(8.dp))
                Text("Nenhum arquivo é modificado. O app só lê.",
                    style = MaterialTheme.typography.bodySmall, color = TextMid)
            }
        }

        LazyColumn(Modifier.weight(1f)) {
            items(tracks, key = { it.uri.toString() }) { t ->
                val idx = tracks.indexOf(t)
                Row(
                    Modifier.fillMaxWidth().clickable { onPlay(idx) }
                        .padding(horizontal = 20.dp, vertical = 12.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text("%02d".format(idx + 1), fontFamily = FontFamily.Monospace,
                        color = TextMid, style = MaterialTheme.typography.bodySmall)
                    Spacer(Modifier.width(14.dp))
                    Column(Modifier.weight(1f)) {
                        Text(t.title, style = MaterialTheme.typography.bodyLarge, maxLines = 1)
                        Text(t.artist ?: t.displayName,
                            style = MaterialTheme.typography.bodySmall,
                            color = TextMid, maxLines = 1)
                    }
                    TextButton(onClick = { sheetFor = t }) { Text("Analisar") }
                }
                HorizontalDivider(color = Surface2)
            }
        }
    }

    sheetFor?.let { track ->
        EnhanceSheet(track, app) { sheetFor = null }
    }
}

@Composable
private fun EnhanceSheet(track: Track, app: MusicEnhancerApp, onClose: () -> Unit) {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    val monitor = remember { OutputRouteMonitor(ctx) }
    val out by monitor.state.collectAsState()
    DisposableEffect(Unit) { monitor.refresh(); onDispose { } }

    var stage by remember { mutableStateOf<String?>(null) }
    var fraction by remember { mutableFloatStateOf(0f) }
    var result by remember { mutableStateOf<OfflineResult?>(null) }
    var cached by remember { mutableStateOf<CacheEntry?>(null) }
    var failure by remember { mutableStateOf<Pair<String, String>?>(null) }
    var running by remember { mutableStateOf(false) }

    AlertDialog(
        onDismissRequest = { if (!running) onClose() },
        title = { Text(track.title, maxLines = 2) },
        confirmButton = {
            if (running) TextButton(onClick = { EnhanceCoordinator.cancel() }) { Text("Cancelar") }
            else TextButton(onClick = onClose) { Text("Fechar") }
        },
        text = {
            Column {
                when {
                    result != null -> ResultPanel(result!!)
                    cached != null -> CachedPanel(cached!!)
                    failure != null -> {
                        Text(failure!!.first, color = Warn)
                        Spacer(Modifier.height(8.dp))
                        Text(failure!!.second, style = MaterialTheme.typography.bodySmall,
                            color = TextMid)
                    }
                    running -> {
                        Text(stage ?: "…", style = MaterialTheme.typography.bodyMedium)
                        Spacer(Modifier.height(12.dp))
                        LinearProgressIndicator(progress = { fraction },
                            modifier = Modifier.fillMaxWidth())
                    }
                    else -> {
                        Text("Analisa a faixa inteira e calcula o que ela precisa. " +
                             "O resultado vai para o cache do app — o arquivo original " +
                             "não é tocado.",
                            style = MaterialTheme.typography.bodyMedium, color = TextMid)
                        Spacer(Modifier.height(16.dp))
                        Button(onClick = {
                            running = true
                            scope.launch {
                                val s = app.settings.flow.first()
                                EnhanceCoordinator.run(ctx, track, out.route, s).collect { p ->
                                    when (p) {
                                        is EnhanceProgress.Stage -> {
                                            stage = p.label; fraction = p.fraction
                                        }
                                        is EnhanceProgress.Done -> {
                                            result = p.result; running = false
                                        }
                                        is EnhanceProgress.Cached -> {
                                            cached = p.entry; running = false
                                        }
                                        is EnhanceProgress.Failed -> {
                                            failure = p.reason to p.fallback; running = false
                                        }
                                    }
                                }
                                running = false
                            }
                        }, modifier = Modifier.fillMaxWidth()) { Text("Analisar e aprimorar") }
                    }
                }
            }
        }
    )
}

@Composable
private fun ResultPanel(r: OfflineResult) {
    Column {
        if (r.revertedToOriginal) {
            Text("O verificador de qualidade reverteu o processamento.", color = Warn,
                style = MaterialTheme.typography.bodyMedium)
            Spacer(Modifier.height(4.dp))
            Text("O resultado ficaria pior que o original, então esta faixa fica " +
                 "como está. Isso é proteção, não falha.",
                style = MaterialTheme.typography.bodySmall, color = TextMid)
            Spacer(Modifier.height(12.dp))
        }

        Text("MEDIDO NESTA FAIXA", style = MaterialTheme.typography.labelSmall, color = TextMid)
        Spacer(Modifier.height(8.dp))
        DeltaRow("Volume integrado", r.before.integratedLufs, r.after.integratedLufs, "LUFS", null)
        DeltaRow("Pico real", r.before.truePeakDbtp, r.after.truePeakDbtp, "dBTP", false)
        DeltaRow("Fator de crista", r.before.crestFactorDb, r.after.crestFactorDb, "dB", true)
        DeltaRow("Presença (voz)", r.before.presence, r.after.presence, "dB", true)
        DeltaRow("Grave", r.before.bass, r.after.bass, "dB", null)

        val improvements = r.measuredImprovements()
        if (improvements.isNotEmpty()) {
            Spacer(Modifier.height(12.dp))
            improvements.forEach {
                Text("• $it", style = MaterialTheme.typography.bodySmall)
            }
        }

        r.before.sourceLimitationOrNull()?.let {
            Spacer(Modifier.height(12.dp))
            UnknownNote(it)
        }

        Spacer(Modifier.height(12.dp))
        Text("Processado em %.1f s · %d passagem(ns) do verificador"
            .format(r.processingSeconds, r.guardIterations),
            style = MaterialTheme.typography.bodySmall, color = TextMid)
    }
}

@Composable
private fun CachedPanel(e: CacheEntry) {
    Column {
        Text("Esta faixa já foi aprimorada com esta mesma versão do motor e " +
             "os mesmos ajustes. Nada foi reprocessado.",
            style = MaterialTheme.typography.bodySmall, color = TextMid)
        Spacer(Modifier.height(12.dp))
        Text("MEDIDO NA VEZ ANTERIOR", style = MaterialTheme.typography.labelSmall,
            color = TextMid)
        Spacer(Modifier.height(8.dp))
        DeltaRow("Volume integrado", e.lufsBefore, e.lufsAfter, "LUFS", null)
        DeltaRow("Pico real", e.truePeakBefore, e.truePeakAfter, "dBTP", false)
        DeltaRow("Fator de crista", e.crestBefore, e.crestAfter, "dB", true)
        if (e.revertedToOriginal) {
            Spacer(Modifier.height(8.dp))
            UnknownNote("O verificador reverteu o processamento nesta faixa.")
        }
    }
}

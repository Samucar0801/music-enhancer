package com.musicenhancer.app.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import com.musicenhancer.app.MusicEnhancerApp
import com.musicenhancer.app.ai.AiStatus
import com.musicenhancer.app.ai.SeparationEngine
import com.musicenhancer.app.dsp.LoudnessMode
import com.musicenhancer.app.dsp.NativeEngine
import com.musicenhancer.app.dsp.Preset
import com.musicenhancer.app.settings.PowerMode
import com.musicenhancer.app.settings.Settings
import com.musicenhancer.app.ui.theme.TextMid
import com.musicenhancer.app.ui.theme.Warn
import com.musicenhancer.app.util.Logger
import kotlinx.coroutines.launch

private val PRESETS = listOf(
    Preset.AUTO to "Automático",
    Preset.HYMNS to "Hinos",
    Preset.VOICE_GUITAR to "Voz e violão",
    Preset.HEADPHONE to "Fone",
    Preset.MAX_QUALITY_AI to "Qualidade máxima"
)

@Composable
fun SettingsScreen(app: MusicEnhancerApp) {
    val scope = rememberCoroutineScope()
    val s by app.settings.flow.collectAsState(initial = Settings())
    var cacheMb by remember { mutableStateOf<Long?>(null) }
    var showLog by remember { mutableStateOf(false) }

    LaunchedEffect(Unit) { cacheMb = app.cache.sizeBytes() / (1024 * 1024) }

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())
        .padding(horizontal = 20.dp)) {

        SectionTitle("Som")
        ChoiceRow("Perfil", PRESETS.map { it.second },
            PRESETS.indexOfFirst { it.first == s.preset }.coerceAtLeast(0)) { i ->
            scope.launch { app.settings.setPreset(PRESETS[i].first) }
        }
        Text("Automático não é um preset cego: o motor mede a faixa e decide. " +
             "Os outros só mudam os alvos, nunca ignoram a medição.",
            style = MaterialTheme.typography.bodySmall, color = TextMid)

        ChoiceRow("Volume",
            listOf("Original", "Normalizado", "Máximo seguro"),
            s.loudnessMode.ordinal) { i ->
            scope.launch { app.settings.setLoudnessMode(LoudnessMode.entries[i]) }
        }

        if (s.loudnessMode == LoudnessMode.NORMALIZED) {
            InfoCard {
                Text("Alvo: %.0f LUFS".format(s.targetLufs),
                    fontFamily = FontFamily.Monospace)
                Slider(value = s.targetLufs.toFloat(), valueRange = -23f..-9f,
                    steps = 13,
                    onValueChange = { v ->
                        scope.launch { app.settings.setTargetLufs(v.toDouble()) }
                    })
                Text("−14 LUFS é o alvo comum de streaming. Mais alto que isso " +
                     "custa dinâmica.", style = MaterialTheme.typography.bodySmall,
                    color = TextMid)
            }
        }

        ChoiceRow("Processamento",
            listOf("Econômico", "Equilibrado", "Qualidade máxima", "Tempo real"),
            s.powerMode.ordinal) { i ->
            scope.launch { app.settings.setPowerMode(PowerMode.entries[i]) }
        }

        SectionTitle("Privacidade")
        Row(Modifier.fillMaxWidth().padding(vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Switch(checked = s.onlineLookup, onCheckedChange = { v ->
                scope.launch { app.settings.setOnlineLookup(v) }
            })
            Spacer(Modifier.width(12.dp))
            Column(Modifier.weight(1f)) {
                Text("Buscar capa e dados na internet")
                Text("Desligado por padrão. Se ligar, só o nome e o artista saem " +
                     "daqui, para o MusicBrainz. O áudio NUNCA é enviado.",
                    style = MaterialTheme.typography.bodySmall, color = TextMid)
            }
        }

        SectionTitle("Armazenamento")
        InfoCard {
            MetricRow("Cache de áudio aprimorado",
                cacheMb?.let { "$it MB" } ?: "calculando…")
            MetricRow("Limite", "${s.cacheLimitMb} MB")
            Spacer(Modifier.height(8.dp))
            Text("O cache guarda a versão aprimorada em separado. Seus MP3 " +
                 "originais não são alterados nem apagados por este app.",
                style = MaterialTheme.typography.bodySmall, color = TextMid)
            Spacer(Modifier.height(12.dp))
            OutlinedButton(onClick = {
                app.cache.clearAll(); cacheMb = 0
            }) { Text("Limpar cache") }
        }

        SectionTitle("Separação por IA")
        InfoCard {
            val status = remember { SeparationEngine.status(app, app.device) }
            Text(SeparationEngine.userFacingMessage(status),
                style = MaterialTheme.typography.bodyMedium,
                color = if (status is AiStatus.Unsupported) Warn else TextMid)
            if (status is AiStatus.NotImplemented) {
                Spacer(Modifier.height(8.dp))
                UnknownNote("Nenhum tempo de processamento é prometido aqui porque " +
                    "nada foi medido neste aparelho.")
            }
        }

        SectionTitle("Este aparelho")
        InfoCard {
            Text(app.device.summary(), fontFamily = FontFamily.Monospace,
                style = MaterialTheme.typography.bodySmall)
            Spacer(Modifier.height(8.dp))
            MetricRow("Modo recomendado", app.device.recommendedMode())
            MetricRow("Núcleo DSP", NativeEngine.safeVersion())
        }

        SectionTitle("Diagnóstico")
        OutlinedButton(onClick = { showLog = !showLog }) {
            Text(if (showLog) "Ocultar log técnico" else "Ver log técnico")
        }
        if (showLog) {
            InfoCard {
                Text(Logger.readAll().ifBlank { "Log vazio." },
                    fontFamily = FontFamily.Monospace,
                    style = MaterialTheme.typography.bodySmall)
                Spacer(Modifier.height(8.dp))
                TextButton(onClick = { Logger.clear() }) { Text("Limpar") }
            }
            Text("O log fica só no aparelho. Nada é enviado — este app não tem " +
                 "telemetria.", style = MaterialTheme.typography.bodySmall, color = TextMid)
        }

        Spacer(Modifier.height(40.dp))
    }
}

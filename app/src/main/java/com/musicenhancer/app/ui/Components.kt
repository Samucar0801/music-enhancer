package com.musicenhancer.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.musicenhancer.app.ui.theme.Accent
import com.musicenhancer.app.ui.theme.Surface2
import com.musicenhancer.app.ui.theme.TextMid
import com.musicenhancer.app.ui.theme.Warn
import kotlin.math.abs

@Composable
fun SectionTitle(text: String, modifier: Modifier = Modifier) {
    Text(text.uppercase(), modifier = modifier.padding(top = 20.dp, bottom = 8.dp),
        style = MaterialTheme.typography.labelMedium,
        color = TextMid, letterSpacing = 1.5.sp)
}

/** Valor técnico. Sempre monoespaçado: números precisam alinhar para comparar. */
@Composable
fun MetricRow(label: String, value: String, hint: String? = null) {
    Column(Modifier.fillMaxWidth().padding(vertical = 5.dp)) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(label, style = MaterialTheme.typography.bodyMedium)
            Text(value, fontFamily = FontFamily.Monospace,
                style = MaterialTheme.typography.bodyMedium, color = Accent)
        }
        if (hint != null) Text(hint, style = MaterialTheme.typography.bodySmall, color = TextMid)
    }
}

/** Antes → depois. Mostra a seta só quando a mudança é real (>0.05). */
@Composable
fun DeltaRow(label: String, before: Double, after: Double, unit: String, betterIsHigher: Boolean?) {
    val d = after - before
    val moved = abs(d) > 0.05
    val good = when { !moved || betterIsHigher == null -> null; betterIsHigher -> d > 0; else -> d < 0 }
    Row(Modifier.fillMaxWidth().padding(vertical = 5.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically) {
        Text(label, style = MaterialTheme.typography.bodyMedium)
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("%.2f".format(before), fontFamily = FontFamily.Monospace,
                style = MaterialTheme.typography.bodySmall, color = TextMid)
            Text(if (moved) "  →  " else "  =  ", color = TextMid,
                style = MaterialTheme.typography.bodySmall)
            Text("%.2f %s".format(after, unit), fontFamily = FontFamily.Monospace,
                style = MaterialTheme.typography.bodyMedium,
                color = when (good) { true -> Accent; false -> Warn; null -> MaterialTheme.colorScheme.onSurface })
        }
    }
}

@Composable
fun InfoCard(
    title: String? = null,
    tone: Color = Surface2,
    content: @Composable ColumnScope.() -> Unit
) {
    Column(
        Modifier.fillMaxWidth().padding(vertical = 6.dp)
            .clip(RoundedCornerShape(14.dp)).background(tone).padding(16.dp)
    ) {
        if (title != null) {
            Text(title, style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(8.dp))
        }
        content()
    }
}

/** Aviso honesto: usado quando o app NÃO sabe algo, em vez de inventar. */
@Composable
fun UnknownNote(text: String) {
    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
        Box(Modifier.padding(top = 6.dp).size(6.dp).clip(RoundedCornerShape(3.dp)).background(Warn))
        Spacer(Modifier.width(10.dp))
        Text(text, style = MaterialTheme.typography.bodySmall, color = TextMid)
    }
}

@Composable
fun ChoiceRow(label: String, options: List<String>, selectedIndex: Int, onSelect: (Int) -> Unit) {
    Column(Modifier.fillMaxWidth().padding(vertical = 8.dp)) {
        Text(label, style = MaterialTheme.typography.bodyMedium)
        Spacer(Modifier.height(8.dp))
        Row(
            Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            options.forEachIndexed { i, o ->
                FilterChip(selected = i == selectedIndex, onClick = { onSelect(i) },
                    label = { Text(o) })
            }
        }
    }
}

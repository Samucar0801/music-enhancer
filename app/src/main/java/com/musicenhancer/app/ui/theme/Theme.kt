package com.musicenhancer.app.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

val Ink       = Color(0xFF0B0D10)
val Surface1  = Color(0xFF14181D)
val Surface2  = Color(0xFF1C2229)
val Accent    = Color(0xFF5EE0C0)
val AccentDim = Color(0xFF2F7D6C)
val Warn      = Color(0xFFE0A85E)
val TextHigh  = Color(0xFFEDF1F5)
val TextMid   = Color(0xFF9AA6B2)

private val Scheme = darkColorScheme(
    primary = Accent, onPrimary = Ink,
    secondary = AccentDim, onSecondary = TextHigh,
    background = Ink, onBackground = TextHigh,
    surface = Surface1, onSurface = TextHigh,
    surfaceVariant = Surface2, onSurfaceVariant = TextMid,
    error = Warn, onError = Ink
)

@Composable
fun MusicEnhancerTheme(content: @Composable () -> Unit) {
    // Sempre escuro: é um player de música, não um app de escritório.
    @Suppress("UNUSED_EXPRESSION") isSystemInDarkTheme()
    MaterialTheme(colorScheme = Scheme, content = content)
}

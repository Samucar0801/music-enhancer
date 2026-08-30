package com.musicenhancer.app.settings

import android.content.Context
import androidx.datastore.preferences.core.*
import androidx.datastore.preferences.preferencesDataStore
import com.musicenhancer.app.dsp.LoudnessMode
import com.musicenhancer.app.dsp.Preset
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map

private val Context.dataStore by preferencesDataStore("music_enhancer_settings")

enum class PowerMode { ECONOMICO, EQUILIBRADO, QUALIDADE_MAXIMA, TEMPO_REAL }

data class Settings(
    val preset: Preset = Preset.AUTO,
    val loudnessMode: LoudnessMode = LoudnessMode.NORMALIZED,
    val targetLufs: Double = -14.0,
    val powerMode: PowerMode = PowerMode.EQUILIBRADO,
    val onlineLookup: Boolean = false,          // desligado por padrão: privacidade
    val cacheLimitMb: Long = 1024,
    val autoEnhanceOnPlay: Boolean = false,
    val lastFolderUri: String? = null,
    val sortOrder: String = "NAME",
    val advancedControls: Boolean = false
)

class SettingsStore(private val ctx: Context) {
    private object K {
        val preset = stringPreferencesKey("preset")
        val loudness = stringPreferencesKey("loudness_mode")
        val targetLufs = doublePreferencesKey("target_lufs")
        val power = stringPreferencesKey("power_mode")
        val online = booleanPreferencesKey("online_lookup")
        val cacheMb = longPreferencesKey("cache_limit_mb")
        val autoEnhance = booleanPreferencesKey("auto_enhance")
        val folder = stringPreferencesKey("last_folder")
        val sort = stringPreferencesKey("sort_order")
        val advanced = booleanPreferencesKey("advanced")
    }

    val flow: Flow<Settings> = ctx.dataStore.data.map { p ->
        Settings(
            preset = runCatching { Preset.valueOf(p[K.preset] ?: "AUTO") }.getOrDefault(Preset.AUTO),
            loudnessMode = runCatching { LoudnessMode.valueOf(p[K.loudness] ?: "NORMALIZED") }
                .getOrDefault(LoudnessMode.NORMALIZED),
            targetLufs = p[K.targetLufs] ?: -14.0,
            powerMode = runCatching { PowerMode.valueOf(p[K.power] ?: "EQUILIBRADO") }
                .getOrDefault(PowerMode.EQUILIBRADO),
            onlineLookup = p[K.online] ?: false,
            cacheLimitMb = p[K.cacheMb] ?: 1024,
            autoEnhanceOnPlay = p[K.autoEnhance] ?: false,
            lastFolderUri = p[K.folder],
            sortOrder = p[K.sort] ?: "NAME",
            advancedControls = p[K.advanced] ?: false
        )
    }

    suspend fun update(block: (MutablePreferences) -> Unit) { ctx.dataStore.edit(block) }
    suspend fun setPreset(v: Preset) = update { it[K.preset] = v.name }
    suspend fun setLoudnessMode(v: LoudnessMode) = update { it[K.loudness] = v.name }
    suspend fun setTargetLufs(v: Double) = update { it[K.targetLufs] = v }
    suspend fun setPowerMode(v: PowerMode) = update { it[K.power] = v.name }
    suspend fun setOnlineLookup(v: Boolean) = update { it[K.online] = v }
    suspend fun setCacheLimitMb(v: Long) = update { it[K.cacheMb] = v }
    suspend fun setFolder(v: String?) = update { p -> if (v == null) p.remove(K.folder) else p[K.folder] = v }
    suspend fun setSortOrder(v: String) = update { it[K.sort] = v }
    suspend fun setAdvanced(v: Boolean) = update { it[K.advanced] = v }
}

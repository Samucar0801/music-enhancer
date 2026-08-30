package com.musicenhancer.app.hardware

import android.app.ActivityManager
import android.content.Context
import android.media.AudioManager
import android.os.Build
import java.io.File

/**
 * Perfil do aparelho medido em tempo de execução. NADA aqui é presumido a
 * partir do nome do modelo: se não deu para ler, o campo fica nulo e a UI
 * mostra "não informado". (Regras 1, 58, 85)
 */
data class DeviceProfile(
    val manufacturer: String, val model: String, val device: String,
    val soc: String?, val cpuCores: Int, val cpuMaxMhz: Int?,
    val abis: List<String>, val androidRelease: String, val apiLevel: Int,
    val totalRamMb: Long, val availRamMb: Long, val lowRamDevice: Boolean,
    val outputSampleRate: Int?, val outputFramesPerBuffer: Int?,
    val supportsFloatOutput: Boolean
) {
    val is64Bit get() = abis.any { it.contains("64") }

    /** Escolhe o modo padrão a partir do que foi MEDIDO, não do nome do chip. */
    fun recommendedMode(): String = when {
        lowRamDevice || totalRamMb < 3072 -> "DSP em tempo real (memória limitada)"
        !is64Bit -> "DSP em tempo real (ABI 32 bits: IA local indisponível)"
        totalRamMb >= 5632 && cpuCores >= 6 -> "DSP + IA local sob demanda"
        else -> "DSP em tempo real, IA opcional com cache"
    }

    /** Requisitos mínimos para tentar separação de fontes local. */
    fun canAttemptLocalAi(): Boolean = is64Bit && !lowRamDevice && totalRamMb >= 5632

    fun summary(): String = buildString {
        appendLine("Fabricante: $manufacturer")
        appendLine("Modelo: $model ($device)")
        appendLine("SoC: ${soc ?: "não informado pelo Android"}")
        appendLine("CPU: $cpuCores núcleos" + (cpuMaxMhz?.let { ", até $it MHz" } ?: ""))
        appendLine("ABI: ${abis.joinToString(", ")}")
        appendLine("Android: $androidRelease (API $apiLevel)")
        appendLine("RAM: ${totalRamMb} MB total, ${availRamMb} MB livre")
        appendLine("Saída: ${outputSampleRate?.let { "$it Hz" } ?: "não informado"}" +
                   ", buffer ${outputFramesPerBuffer ?: "?"} amostras")
        appendLine("Float PCM: ${if (supportsFloatOutput) "suportado" else "não (usando 16 bits)"}")
        append("Modo recomendado: ${recommendedMode()}")
    }

    companion object {
        fun read(ctx: Context): DeviceProfile {
            val am = ctx.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
            val mi = ActivityManager.MemoryInfo().also { am.getMemoryInfo(it) }
            val audio = ctx.getSystemService(Context.AUDIO_SERVICE) as AudioManager

            val soc = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                listOfNotNull(Build.SOC_MANUFACTURER, Build.SOC_MODEL)
                    .filter { it.isNotBlank() && it != Build.UNKNOWN }
                    .joinToString(" ").ifBlank { null }
            } else readCpuInfoHardware()

            return DeviceProfile(
                manufacturer = Build.MANUFACTURER,
                model = Build.MODEL,
                device = Build.DEVICE,
                soc = soc,
                cpuCores = Runtime.getRuntime().availableProcessors(),
                cpuMaxMhz = readMaxCpuFreqMhz(),
                abis = Build.SUPPORTED_ABIS.toList(),
                androidRelease = Build.VERSION.RELEASE,
                apiLevel = Build.VERSION.SDK_INT,
                totalRamMb = mi.totalMem / (1024 * 1024),
                availRamMb = mi.availMem / (1024 * 1024),
                lowRamDevice = am.isLowRamDevice,
                outputSampleRate = audio
                    .getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)?.toIntOrNull(),
                outputFramesPerBuffer = audio
                    .getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER)?.toIntOrNull(),
                supportsFloatOutput = Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP
            )
        }

        private fun readCpuInfoHardware(): String? = runCatching {
            File("/proc/cpuinfo").readLines()
                .firstOrNull { it.startsWith("Hardware", true) }
                ?.substringAfter(':')?.trim()?.ifBlank { null }
        }.getOrNull()

        private fun readMaxCpuFreqMhz(): Int? = runCatching {
            (0 until Runtime.getRuntime().availableProcessors()).mapNotNull { i ->
                File("/sys/devices/system/cpu/cpu$i/cpufreq/cpuinfo_max_freq")
                    .takeIf { it.canRead() }?.readText()?.trim()?.toLongOrNull()
            }.maxOrNull()?.let { (it / 1000).toInt() }
        }.getOrNull()
    }
}

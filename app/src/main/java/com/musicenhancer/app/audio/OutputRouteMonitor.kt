package com.musicenhancer.app.audio

import android.Manifest
import android.bluetooth.BluetoothA2dp
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothProfile
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import androidx.core.content.ContextCompat
import com.musicenhancer.app.dsp.Route
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

data class OutputState(
    val route: Route = Route.SPEAKER,
    val deviceName: String = "Alto-falante do aparelho",
    /** null = o Android não informou. NUNCA inventar um codec. (Regra 23) */
    val bluetoothCodec: String? = null,
    val sampleRate: Int? = null,
    val systemEffectWarning: String? = null
)

/**
 * Detecta a saída ativa e, quando o Android disponibiliza, o codec Bluetooth.
 *
 * O codec vem de BluetoothA2dp.getCodecStatus(), que em várias versões do
 * Android é @hide/@SystemApi. Acessamos por reflexão e, se falhar, dizemos
 * honestamente que a informação não está disponível.
 */
class OutputRouteMonitor(private val ctx: Context) {

    private val am = ctx.getSystemService(Context.AUDIO_SERVICE) as AudioManager
    private val _state = MutableStateFlow(OutputState())
    val state: StateFlow<OutputState> = _state

    private var a2dp: BluetoothA2dp? = null
    private val handler = Handler(Looper.getMainLooper())

    private val callback = object : AudioDeviceCallback() {
        override fun onAudioDevicesAdded(added: Array<out AudioDeviceInfo>?) = refresh()
        override fun onAudioDevicesRemoved(removed: Array<out AudioDeviceInfo>?) = refresh()
    }

    fun start() {
        am.registerAudioDeviceCallback(callback, handler)
        connectA2dp()
        refresh()
    }

    fun stop() {
        am.unregisterAudioDeviceCallback(callback)
        runCatching {
            BluetoothAdapter.getDefaultAdapter()
                ?.closeProfileProxy(BluetoothProfile.A2DP, a2dp)
        }
        a2dp = null
    }

    private fun hasBtPermission(): Boolean =
        Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
        ContextCompat.checkSelfPermission(ctx, Manifest.permission.BLUETOOTH_CONNECT) ==
            PackageManager.PERMISSION_GRANTED

    private fun connectA2dp() {
        if (!hasBtPermission()) return
        runCatching {
            BluetoothAdapter.getDefaultAdapter()?.getProfileProxy(
                ctx, object : BluetoothProfile.ServiceListener {
                    override fun onServiceConnected(p: Int, proxy: BluetoothProfile) {
                        if (p == BluetoothProfile.A2DP) { a2dp = proxy as BluetoothA2dp; refresh() }
                    }
                    override fun onServiceDisconnected(p: Int) { if (p == BluetoothProfile.A2DP) a2dp = null }
                }, BluetoothProfile.A2DP)
        }
    }

    fun refresh() {
        val outs = am.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
        var route = Route.SPEAKER
        var name = "Alto-falante do aparelho"
        var btDevice: AudioDeviceInfo? = null

        // Prioridade: Bluetooth > fio > alto-falante (espelha o roteamento do Android)
        for (d in outs) {
            when (d.type) {
                AudioDeviceInfo.TYPE_BLUETOOTH_A2DP -> {
                    route = Route.BLUETOOTH; name = d.productName?.toString() ?: "Fone Bluetooth"
                    btDevice = d
                }
                AudioDeviceInfo.TYPE_WIRED_HEADSET,
                AudioDeviceInfo.TYPE_WIRED_HEADPHONES,
                AudioDeviceInfo.TYPE_USB_HEADSET -> if (route != Route.BLUETOOTH) {
                    route = Route.HEADPHONE; name = "Fone com fio"
                }
                else -> {}
            }
        }
        val codec = if (route == Route.BLUETOOTH) readBluetoothCodec() else null
        _state.value = OutputState(
            route = route,
            deviceName = name,
            bluetoothCodec = codec,
            sampleRate = btDevice?.sampleRates?.maxOrNull(),
            systemEffectWarning = systemEffectWarning()
        )
    }

    /** Retorna o nome do codec, ou null se o Android não expuser a informação. */
    private fun readBluetoothCodec(): String? {
        val proxy = a2dp ?: return null
        if (!hasBtPermission()) return null
        return runCatching {
            val adapter = BluetoothAdapter.getDefaultAdapter() ?: return null
            @Suppress("MissingPermission")
            val connected: List<BluetoothDevice> = proxy.connectedDevices
            val dev = connected.firstOrNull() ?: return null
            val getCodecStatus = proxy.javaClass
                .getMethod("getCodecStatus", BluetoothDevice::class.java)
            val status = getCodecStatus.invoke(proxy, dev) ?: return null
            val getSelected = status.javaClass.getMethod("getCodecConfig")
            val config = getSelected.invoke(status) ?: return null
            val getType = config.javaClass.getMethod("getCodecType")
            when ((getType.invoke(config) as? Int) ?: -1) {
                0 -> "SBC"; 1 -> "AAC"; 2 -> "aptX"; 3 -> "aptX HD"
                4 -> "LDAC"; 5 -> "LC3"; 6 -> "Opus"
                else -> null
            }
        }.getOrNull()
    }

    /**
     * MIUI/HyperOS e Dolby Atmos aplicam pós-processamento do sistema. Empilhar
     * o nosso DSP sobre o deles é exatamente a causa da distorção que os
     * usuários relatam. Avisamos em vez de tentar desligar à força.
     */
    private fun systemEffectWarning(): String? {
        val pm = ctx.packageManager
        val suspects = listOf(
            "com.miui.audioeffect" to "Efeitos de som da Xiaomi",
            "com.dolby.daxappui" to "Dolby Atmos",
            "com.dolby.dax3.service" to "Dolby Atmos"
        )
        for ((pkg, label) in suspects) {
            val installed = runCatching { pm.getPackageInfo(pkg, 0); true }.getOrDefault(false)
            if (installed) return "$label está ativo no sistema. Empilhar dois " +
                "processadores costuma causar distorção — considere desativá-lo " +
                "em Configurações > Som para ouvir só o Music Enhancer."
        }
        return null
    }
}

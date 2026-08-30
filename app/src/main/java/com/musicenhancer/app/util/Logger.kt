package com.musicenhancer.app.util

import android.content.Context
import android.util.Log
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Log técnico local, para diagnóstico. NUNCA registra: nome de música,
 * caminho de arquivo, conteúdo de áudio ou dado pessoal. (Regras 63/64)
 * Nada é enviado para lugar nenhum — não há telemetria neste app.
 */
object Logger {
    private const val TAG = "MusicEnhancer"
    private const val MAX_BYTES = 256 * 1024
    private var file: File? = null
    private val fmt = SimpleDateFormat("MM-dd HH:mm:ss.SSS", Locale.US)

    fun init(ctx: Context) {
        file = File(ctx.filesDir, "diagnostic.log").also { f ->
            if (f.exists() && f.length() > MAX_BYTES) f.delete()
        }
    }

    fun i(msg: String) = write("I", msg)
    fun w(msg: String) = write("W", msg)
    fun e(msg: String, t: Throwable? = null) =
        write("E", msg + (t?.let { " :: ${it.javaClass.simpleName}: ${it.message}" } ?: ""))

    private fun write(level: String, msg: String) {
        Log.println(when (level) { "E" -> Log.ERROR; "W" -> Log.WARN; else -> Log.INFO }, TAG, msg)
        runCatching { file?.appendText("${fmt.format(Date())} $level $msg\n") }
    }

    fun readAll(): String = runCatching { file?.readText() ?: "" }.getOrDefault("")
    fun clear() { runCatching { file?.writeText("") } }
}

package com.musicenhancer.app.analysis

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import com.musicenhancer.app.R

/**
 * Serviço de primeiro plano para o processamento pesado. O Android 15 impõe
 * limite de tempo ao tipo mediaProcessing — o que é adequado aqui: aprimorar
 * uma faixa leva minutos, não horas.
 */
class EnhanceService : Service() {

    companion object {
        const val CHANNEL = "enhance"
        const val NOTIF_ID = 4201
        const val EXTRA_LABEL = "label"
        const val EXTRA_PROGRESS = "progress"
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        val nm = getSystemService(NotificationManager::class.java)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            nm.createNotificationChannel(NotificationChannel(
                CHANNEL, getString(R.string.channel_enhance),
                NotificationManager.IMPORTANCE_LOW))
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val label = intent?.getStringExtra(EXTRA_LABEL) ?: getString(R.string.processing)
        val progress = intent?.getIntExtra(EXTRA_PROGRESS, -1) ?: -1
        val n = NotificationCompat.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.stat_sys_download)
            .setContentTitle(getString(R.string.app_name))
            .setContentText(label)
            .setOngoing(true)
            .apply { if (progress in 0..100) setProgress(100, progress, false)
                     else setProgress(0, 0, true) }
            .build()
        startForeground(NOTIF_ID, n)
        return START_NOT_STICKY
    }
}

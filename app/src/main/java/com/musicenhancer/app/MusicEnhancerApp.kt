package com.musicenhancer.app

import android.app.Application
import com.musicenhancer.app.cache.CacheManager
import com.musicenhancer.app.hardware.DeviceProfile
import com.musicenhancer.app.settings.FavoritesStore
import com.musicenhancer.app.settings.SettingsStore
import com.musicenhancer.app.util.Logger

class MusicEnhancerApp : Application() {
    lateinit var settings: SettingsStore; private set
    lateinit var favorites: FavoritesStore; private set
    lateinit var cache: CacheManager; private set
    lateinit var device: DeviceProfile; private set

    override fun onCreate() {
        super.onCreate()
        settings = SettingsStore(this)
        favorites = FavoritesStore(this)
        cache = CacheManager(this)
        device = DeviceProfile.read(this)
        Logger.init(this)
        Logger.i("Primeira execução — perfil do aparelho:\n${device.summary()}")
    }
}

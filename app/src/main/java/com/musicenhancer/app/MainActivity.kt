package com.musicenhancer.app

import android.Manifest
import android.content.ComponentName
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.core.view.WindowCompat
import androidx.media3.common.MediaItem
import androidx.media3.common.util.UnstableApi
import androidx.media3.session.MediaController
import androidx.media3.session.SessionToken
import com.google.common.util.concurrent.MoreExecutors
import com.musicenhancer.app.library.FolderScanner
import com.musicenhancer.app.library.SortOrder
import com.musicenhancer.app.library.Track
import com.musicenhancer.app.playback.PlaybackService
import com.musicenhancer.app.ui.*
import com.musicenhancer.app.ui.theme.MusicEnhancerTheme
import com.musicenhancer.app.util.Logger
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@UnstableApi
class MainActivity : ComponentActivity() {

    private val controllerState = mutableStateOf<MediaController?>(null)
    private val controller get() = controllerState.value

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        WindowCompat.setDecorFitsSystemWindows(window, false)
        val app = application as MusicEnhancerApp

        setContent {
            MusicEnhancerTheme {
                val scope = rememberCoroutineScope()
                var tracks by remember { mutableStateOf<List<Track>>(emptyList()) }
                var tab by remember { mutableIntStateOf(0) }
                var loading by remember { mutableStateOf(false) }
                var folderName by remember { mutableStateOf<String?>(null) }

                val notifPerm = rememberLauncherForActivityResult(
                    ActivityResultContracts.RequestPermission()) { }
                val btPerm = rememberLauncherForActivityResult(
                    ActivityResultContracts.RequestPermission()) { }

                // Retoma a última pasta: o app não deve exigir escolher tudo
                // de novo a cada abertura (queixa comum de players).
                LaunchedEffect(Unit) {
                    val saved = app.settings.flow.first().lastFolderUri
                    if (saved != null && tracks.isEmpty()) {
                        loading = true
                        val uri = Uri.parse(saved)
                        val ok = runCatching {
                            withContext(Dispatchers.IO) {
                                FolderScanner.sort(FolderScanner.withMetadata(
                                    this@MainActivity,
                                    FolderScanner.scan(this@MainActivity, uri)), SortOrder.NAME)
                            }
                        }.getOrNull()
                        if (ok != null && ok.isNotEmpty()) {
                            tracks = ok
                            folderName = uri.lastPathSegment?.substringAfterLast('/')
                        } else {
                            Logger.w("Pasta salva não pôde ser lida (permissão revogada?)")
                        }
                        loading = false
                    }
                }

                LaunchedEffect(Unit) {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU)
                        notifPerm.launch(Manifest.permission.POST_NOTIFICATIONS)
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
                        btPerm.launch(Manifest.permission.BLUETOOTH_CONNECT)
                    connectController()
                }

                val pickFolder = rememberLauncherForActivityResult(
                    ActivityResultContracts.OpenDocumentTree()) { uri: Uri? ->
                    if (uri == null) return@rememberLauncherForActivityResult
                    contentResolver.takePersistableUriPermission(uri,
                        Intent.FLAG_GRANT_READ_URI_PERMISSION)
                    loading = true
                    scope.launch {
                        val found = withContext(Dispatchers.IO) {
                            val raw = FolderScanner.scan(this@MainActivity, uri)
                            FolderScanner.sort(
                                FolderScanner.withMetadata(this@MainActivity, raw), SortOrder.NAME)
                        }
                        tracks = found
                        folderName = uri.lastPathSegment?.substringAfterLast('/')
                        loading = false
                        app.settings.setFolder(uri.toString())
                        Logger.i("Pasta carregada: ${found.size} faixas")
                    }
                }

                Scaffold(
                    bottomBar = {
                        NavigationBar {
                            listOf("Tocando", "Biblioteca", "Ajustes")
                                .forEachIndexed { i, label ->
                                    NavigationBarItem(
                                        selected = tab == i,
                                        onClick = { tab = i },
                                        label = { Text(label) },
                                        icon = {})
                                }
                        }
                    }
                ) { pad ->
                    Box(Modifier.padding(pad).fillMaxSize()) {
                        when (tab) {
                            0 -> PlayerScreen(app, controllerState.value)
                            1 -> LibraryScreen(
                                tracks = tracks, loading = loading, folderName = folderName,
                                app = app,
                                onPickFolder = { pickFolder.launch(null) },
                                onPlay = { index -> playQueue(tracks, index) })
                            else -> SettingsScreen(app)
                        }
                    }
                }
            }
        }
    }

    private fun connectController() {
        val token = SessionToken(this, ComponentName(this, PlaybackService::class.java))
        val future = MediaController.Builder(this, token).buildAsync()
        future.addListener({ controllerState.value = runCatching { future.get() }.getOrNull() },
            MoreExecutors.directExecutor())
    }

    /** Uma pasta vira uma fila: a próxima música toca sozinha. (Regras 2/20) */
    private fun playQueue(tracks: List<Track>, startIndex: Int) {
        val c = controller ?: return
        c.setMediaItems(tracks.map { t ->
            MediaItem.Builder()
                .setUri(t.uri)
                .setMediaMetadata(
                    androidx.media3.common.MediaMetadata.Builder()
                        .setTitle(t.title)
                        .setArtist(t.artist)
                        .setAlbumTitle(t.album)
                        .build())
                .build()
        }, startIndex, 0L)
        c.prepare()
        c.play()
    }

    override fun onDestroy() {
        controller?.release(); controllerState.value = null
        super.onDestroy()
    }
}

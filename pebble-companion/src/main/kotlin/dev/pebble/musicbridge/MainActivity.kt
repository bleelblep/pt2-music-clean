package dev.pebble.musicbridge

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.media.AudioManager
import android.os.Build
import android.os.Bundle
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.enableEdgeToEdge
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import dev.pebble.musicbridge.ui.DreamwaveApp
import kotlinx.coroutines.flow.MutableStateFlow

class MainActivity : ComponentActivity() {

    /**
     * Set to true when the activity is launched/resumed from the media notification's
     * session activity intent. Collected by [DreamwaveApp] to auto-expand the player
     * sheet so the user lands on Now Playing instead of wherever the UI was left.
     */
    private val openPlayerRequest = MutableStateFlow(false)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // The vendored player lays itself out against WindowInsets.safeDrawing and
        // navigationBars, and PixelPlayTheme paints the system bars from the colour
        // scheme. Both assume an edge-to-edge window.
        enableEdgeToEdge()
        // Hardware volume keys should adjust the media stream, like a real music app.
        volumeControlStream = AudioManager.STREAM_MUSIC

        handleIntent(intent)

        setContent {
            NotificationPermissionEffect()
            DreamwaveApp(openPlayerRequest = openPlayerRequest)
        }
        // Starting the service from a visible activity marks it as a *started* service,
        // which lets Media3 promote it to the foreground later even when playback is
        // triggered by the watch while the app itself is in the background.
        runCatching { startService(Intent(this, PebblePlaybackService::class.java)) }
            .onFailure { Log.w("PT2Music", "Could not pre-start playback service", it) }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleIntent(intent)
    }

    private fun handleIntent(intent: Intent?) {
        if (intent?.getBooleanExtra(EXTRA_OPEN_PLAYER, false) == true) {
            openPlayerRequest.value = true
            // Clear the extra so a configuration change (which re-delivers the same
            // intent) does not re-expand the sheet after the user dismissed it.
            intent.removeExtra(EXTRA_OPEN_PLAYER)
        }
    }

    @Composable
    private fun NotificationPermissionEffect() {
        val launcher = rememberLauncherForActivityResult(
            ActivityResultContracts.RequestPermission(),
        ) { }
        LaunchedEffect(Unit) {
            if (
                Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
                checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
            ) {
                launcher.launch(Manifest.permission.POST_NOTIFICATIONS)
            }
        }
    }

    companion object {
        const val EXTRA_OPEN_PLAYER = "dev.pebble.musicbridge.OPEN_PLAYER"
    }
}

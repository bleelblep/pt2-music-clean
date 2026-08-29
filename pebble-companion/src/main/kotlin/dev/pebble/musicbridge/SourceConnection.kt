package dev.pebble.musicbridge

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.IBinder
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Binds [PebbleMusicService] so the UI can read and change the music source.
 *
 * The same [PlaybackConnection] pattern, pointed at a different service: the source is a
 * choice *between* the two playback backends, so it is owned by the one thing that holds
 * both of them rather than by either one.
 *
 * Binding here also starts that service (and, through it, both backends) when the app is
 * opened with no watch connected. That is the intended trade: the alternative is a source
 * switch that silently does nothing until the watch next says something.
 */
class SourceConnection(private val context: Context) {

    private var bound = false

    private val _service = MutableStateFlow<PebbleMusicService?>(null)
    val service: StateFlow<PebbleMusicService?> = _service.asStateFlow()

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName, binder: IBinder) {
            _service.value = (binder as PebbleMusicService.LocalBinder).service
        }

        override fun onServiceDisconnected(name: ComponentName) {
            _service.value = null
        }
    }

    fun bind() {
        if (bound) return
        bound = context.bindService(
            Intent(context, PebbleMusicService::class.java)
                .setAction(PebbleMusicService.ACTION_LOCAL_BIND),
            connection,
            Context.BIND_AUTO_CREATE,
        )
    }

    fun unbind() {
        if (!bound) return
        context.unbindService(connection)
        bound = false
        _service.value = null
    }
}

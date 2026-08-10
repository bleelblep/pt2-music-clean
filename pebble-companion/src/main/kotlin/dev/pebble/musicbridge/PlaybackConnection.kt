package dev.pebble.musicbridge

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.IBinder
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Binds to [PebblePlaybackService] via the same [PebblePlaybackService.LocalBinder]
 * pattern that [PebbleMusicService] uses. Exposes the service as a StateFlow so
 * Compose can observe the connection state.
 *
 * The UI creates one instance in [androidx.lifecycle.ViewModel.onCleared] scope and
 * calls [bind] / [unbind] from lifecycle events.
 */
class PlaybackConnection(private val context: Context) {

    private var serviceDeferred = CompletableDeferred<PebblePlaybackService>()
    private var bound = false

    private val _service = MutableStateFlow<PebblePlaybackService?>(null)
    val service: StateFlow<PebblePlaybackService?> = _service.asStateFlow()

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName, binder: IBinder) {
            val svc = (binder as PebblePlaybackService.LocalBinder).service
            _service.value = svc
            if (!serviceDeferred.isCompleted) serviceDeferred.complete(svc)
        }

        override fun onServiceDisconnected(name: ComponentName) {
            _service.value = null
            serviceDeferred = CompletableDeferred()
        }
    }

    fun bind() {
        if (bound) return
        bound = context.bindService(
            Intent(context, PebblePlaybackService::class.java)
                .setAction(PebblePlaybackService.ACTION_LOCAL_BIND),
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

    /** Suspend until the service is connected. Returns null on timeout. */
    suspend fun awaitService(timeoutMs: Long = 5_000): PebblePlaybackService? =
        kotlinx.coroutines.withTimeoutOrNull(timeoutMs) { serviceDeferred.await() }
}

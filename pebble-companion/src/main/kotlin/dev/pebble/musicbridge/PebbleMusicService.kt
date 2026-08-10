package dev.pebble.musicbridge

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.IBinder
import io.rebble.pebblekit2.client.BasePebbleListenerService
import io.rebble.pebblekit2.common.model.PebbleDictionary
import io.rebble.pebblekit2.common.model.ReceiveResult
import io.rebble.pebblekit2.common.model.WatchIdentifier
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.withTimeoutOrNull
import java.util.UUID

class PebbleMusicService : BasePebbleListenerService() {
    private var playbackService = CompletableDeferred<PebblePlaybackService>()
    private var symfoniumService = CompletableDeferred<SymfoniumPlaybackService>()
    private var boundPlayback = false
    private var boundSymfonium = false

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName, binder: IBinder) {
            val service = (binder as PebblePlaybackService.LocalBinder).service
            if (!playbackService.isCompleted) playbackService.complete(service)
        }

        override fun onServiceDisconnected(name: ComponentName) {
            playbackService = CompletableDeferred()
        }
    }

    private val symfoniumConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName, binder: IBinder) {
            val service = (binder as SymfoniumPlaybackService.LocalBinder).service
            if (!symfoniumService.isCompleted) symfoniumService.complete(service)
        }

        override fun onServiceDisconnected(name: ComponentName) {
            symfoniumService = CompletableDeferred()
        }
    }

    override fun onCreate() {
        super.onCreate()
        boundPlayback = bindService(
            Intent(this, PebblePlaybackService::class.java)
                .setAction(PebblePlaybackService.ACTION_LOCAL_BIND),
            connection,
            Context.BIND_AUTO_CREATE,
        )
        boundSymfonium = bindService(
            Intent(this, SymfoniumPlaybackService::class.java)
                .setAction(SymfoniumPlaybackService.ACTION_LOCAL_BIND),
            symfoniumConnection,
            Context.BIND_AUTO_CREATE,
        )
    }

    override suspend fun onMessageReceived(
        watchappUUID: UUID,
        data: PebbleDictionary,
        watch: WatchIdentifier,
    ): ReceiveResult = when (watchappUUID) {
        Protocol.appUuid -> {
            val service = withTimeoutOrNull(5_000) { playbackService.await() }
                ?: return ReceiveResult.Nack
            service.onWatchMessage(data)
        }
        Protocol.symfoniumAppUuid -> {
            val service = withTimeoutOrNull(5_000) { symfoniumService.await() }
                ?: return ReceiveResult.Nack
            service.onWatchMessage(data)
        }
        else -> ReceiveResult.Nack
    }

    override fun onDestroy() {
        if (boundPlayback) unbindService(connection)
        if (boundSymfonium) unbindService(symfoniumConnection)
        boundPlayback = false
        boundSymfonium = false
        super.onDestroy()
    }
}

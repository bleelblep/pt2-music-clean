package dev.pebble.musicbridge

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.Binder
import android.os.IBinder
import io.rebble.pebblekit2.client.BasePebbleListenerService
import io.rebble.pebblekit2.common.model.PebbleDictionary
import io.rebble.pebblekit2.common.model.PebbleDictionaryItem
import io.rebble.pebblekit2.common.model.ReceiveResult
import io.rebble.pebblekit2.common.model.WatchIdentifier
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull
import java.util.UUID

/**
 * The watch's single entry point on the phone, and the owner of the music-source setting.
 *
 * There is one watch app and two backends behind it - [PebblePlaybackService] (YouTube
 * Music) and [SymfoniumPlaybackService] (mirrors Symfonium's own player). Which one answers
 * is a runtime choice stored in [MusicSourcePrefs], settable from either end. This service
 * owns that choice because it is the only thing holding both backends; neither backend can
 * own a setting that selects between them.
 */
class PebbleMusicService : BasePebbleListenerService() {
    inner class LocalBinder : Binder() {
        val service: PebbleMusicService
            get() = this@PebbleMusicService
    }

    private var playbackService = CompletableDeferred<PebblePlaybackService>()
    private var symfoniumService = CompletableDeferred<SymfoniumPlaybackService>()
    private var boundPlayback = false
    private var boundSymfonium = false
    private val localBinder = LocalBinder()

    private val _musicSource = MutableStateFlow(Protocol.sourceYouTube)

    /** The active source, for the phone UI. Writes go through [setMusicSource]. */
    val musicSource: StateFlow<Int> = _musicSource.asStateFlow()

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
        _musicSource.value = MusicSourcePrefs.source(this)
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

    /**
     * PebbleKit binds this service with its own action to deliver watch data; that path
     * must keep getting the base class's binder. Only our explicit local-bind action gets
     * [LocalBinder].
     */
    override fun onBind(intent: Intent?): IBinder? =
        if (intent?.action == ACTION_LOCAL_BIND) localBinder else super.onBind(intent)

    /**
     * Phone-initiated source change (from the UI). Bumps the epoch so the watch's next
     * commandSyncSettings - which it re-sends on every state snapshot - cannot revert it,
     * then hands over and tells the watch.
     */
    fun setMusicSource(source: Int) {
        if (source == MusicSourcePrefs.source(this)) return
        val epoch = MusicSourcePrefs.setFromPhone(this, source)
        _musicSource.value = source
        coroutineScope.launch {
            handOver(to = source)
            backendFor(source)?.announceSourceChange(source, epoch)
        }
    }

    /**
     * Quiets the backend being switched away from. The incoming backend is not primed here:
     * the watch re-handshakes when it adopts the new source, and that handshake pulls a
     * fresh snapshot through the normal path.
     */
    private suspend fun handOver(to: Int) {
        if (to != Protocol.sourceYouTube) {
            withTimeoutOrNull(HANDOVER_TIMEOUT_MS) { playbackService.await() }?.onSourceDeactivated()
        }
        if (to != Protocol.sourceSymfonium) {
            withTimeoutOrNull(HANDOVER_TIMEOUT_MS) { symfoniumService.await() }?.onSourceDeactivated()
        }
    }

    /**
     * Symfonium's live playback state, for the phone UI to mirror while that source is
     * active. Read-only there - Symfonium's own app owns its transport controls.
     */
    suspend fun symfoniumState(): StateFlow<PlaybackUiState>? =
        withTimeoutOrNull(BIND_TIMEOUT_MS) { symfoniumService.await() }?.uiState

    /**
     * The Symfonium backend itself, for phone-UI searches and library browsing while
     * that source is active. The same instance the watch's commands land on, so both
     * ends share its item cache and wire ids.
     */
    suspend fun symfoniumBackend(): SymfoniumPlaybackService? =
        withTimeoutOrNull(BIND_TIMEOUT_MS) { symfoniumService.await() }

    private suspend fun backendFor(source: Int): MusicBackend? = when (source) {
        Protocol.sourceSymfonium -> withTimeoutOrNull(BIND_TIMEOUT_MS) { symfoniumService.await() }
        else -> withTimeoutOrNull(BIND_TIMEOUT_MS) { playbackService.await() }
    }

    /**
     * Every message arrives under the one app UUID, so the source - not the sender - picks
     * the backend. The source keys ride along on commandSyncSettings and are applied before
     * dispatch, so a switch made on the watch takes effect on the very message that carries
     * it rather than the one after.
     */
    override suspend fun onMessageReceived(
        watchappUUID: UUID,
        data: PebbleDictionary,
        watch: WatchIdentifier,
    ): ReceiveResult {
        if (watchappUUID != Protocol.appUuid) return ReceiveResult.Nack
        applySourceFromWatch(data)
        val service = backendFor(MusicSourcePrefs.source(this)) ?: return ReceiveResult.Nack
        return service.onWatchMessage(data)
    }

    private suspend fun applySourceFromWatch(data: PebbleDictionary) {
        val source = (data[Protocol.keyConfigMusicSource] as? PebbleDictionaryItem.Int32)?.value
            ?: return
        val epoch = (data[Protocol.keySourceEpoch] as? PebbleDictionaryItem.Int32)?.value ?: 0
        if (!MusicSourcePrefs.adoptFromWatch(this, source, epoch)) return
        _musicSource.value = source
        handOver(to = source)
    }

    override fun onDestroy() {
        if (boundPlayback) unbindService(connection)
        if (boundSymfonium) unbindService(symfoniumConnection)
        boundPlayback = false
        boundSymfonium = false
        super.onDestroy()
    }

    companion object {
        const val ACTION_LOCAL_BIND = "dev.pebble.musicbridge.SOURCE_LOCAL_BIND"
        private const val BIND_TIMEOUT_MS = 5_000L
        private const val HANDOVER_TIMEOUT_MS = 2_000L
    }
}

/**
 * What [PebbleMusicService] needs from a backend to route to it and hand over between them.
 * Deliberately narrow: the two implementations share no state and no base class, and the
 * point here is the routing contract, not a merge of two very different services.
 */
interface MusicBackend {
    suspend fun onWatchMessage(data: PebbleDictionary): ReceiveResult

    /** Stop playing and clear anything of ours still on the watch's screen. */
    suspend fun onSourceDeactivated()

    /** Tell the watch the source changed; it re-handshakes and we answer as usual. */
    suspend fun announceSourceChange(source: Int, epoch: Int)
}

package dev.pebble.musicbridge

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import android.app.PendingIntent
import android.content.Intent
import android.content.pm.ServiceInfo
import android.graphics.BitmapFactory
import android.media.AudioManager
import android.net.Uri
import android.os.Binder
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.media3.common.AudioAttributes
import androidx.media3.common.C
import androidx.media3.common.MediaItem
import androidx.media3.common.MediaMetadata
import androidx.media3.common.Player
import androidx.media3.common.audio.AudioProcessor
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.DefaultRenderersFactory
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.exoplayer.source.DefaultMediaSourceFactory
import androidx.media3.exoplayer.audio.AudioSink
import androidx.media3.exoplayer.audio.DefaultAudioSink
import androidx.media3.database.StandaloneDatabaseProvider
import androidx.media3.datasource.DataSpec
import androidx.media3.datasource.DefaultDataSource
import androidx.media3.datasource.cache.CacheDataSource
import androidx.media3.datasource.cache.CacheWriter
import androidx.media3.datasource.cache.ContentMetadata
import androidx.media3.datasource.cache.LeastRecentlyUsedCacheEvictor
import androidx.media3.datasource.cache.SimpleCache
import androidx.media3.session.CommandButton
import androidx.media3.session.DefaultMediaNotificationProvider
import androidx.media3.session.MediaNotification
import androidx.media3.session.MediaSession
import androidx.media3.session.MediaSessionService
import com.google.common.collect.ImmutableList
import com.metrolist.innertube.YouTube
import com.metrolist.innertube.NewPipeExtractor
import com.metrolist.innertube.models.ArtistItem
import com.metrolist.innertube.models.SongItem
import com.metrolist.innertube.models.WatchEndpoint
import com.metrolist.innertube.models.YouTubeClient
import com.metrolist.innertube.models.YouTubeLocale
import dev.pebble.watchimagebridge.watchimagebridge.Watchimagebridge
import io.rebble.pebblekit2.common.model.PebbleDictionary
import io.rebble.pebblekit2.common.model.PebbleDictionaryItem
import io.rebble.pebblekit2.common.model.ReceiveResult
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

@androidx.annotation.OptIn(UnstableApi::class)
class PebblePlaybackService : MediaSessionService() {
    inner class LocalBinder : Binder() {
        val service: PebblePlaybackService
            get() = this@PebblePlaybackService
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private val localBinder = LocalBinder()
    private val audioManager by lazy { getSystemService(Context.AUDIO_SERVICE) as AudioManager }
    private lateinit var transport: PebbleAudioTransport
    private lateinit var player: ExoPlayer
    private lateinit var mediaSession: MediaSession
    private lateinit var mediaCache: SimpleCache
    private lateinit var cacheDataSourceFactory: CacheDataSource.Factory
    // Used for tracks that must not touch the cache at all (caching off, or a radio
    // track with "Cache radio" off). The player's default factory is cache-backed, so
    // bypassing it means building the media source explicitly - see play().
    private lateinit var uncachedMediaSourceFactory: DefaultMediaSourceFactory
    private var cacheDownloadJob: Job? = null
    private var coverArtJob: Job? = null
    private var qualitySwapJob: Job? = null
    private val streamValidator = OkHttpClient()
    private val coverCacheDir by lazy { filesDir.resolve(COVER_CACHE_DIR).apply { mkdirs() } }
    private var activeGeneration = 0
    private var phoneAudio = false
    // Monotonic epoch for the audio route, bumped by whichever side initiates a route
    // change and carried in every route-bearing message. A route arriving with an older
    // epoch is a stale echo from before a newer change and must not be applied â€” see
    // setAudioRoute(). Ties (both sides changed simultaneously) go to the watch.
    private var routeEpoch = 0
    private var phoneVolume = DEFAULT_PHONE_VOLUME
    private var activeVideoId: String? = null
    private var activeTitle: String? = null
    private var activeArtist: String? = null
    private var resolvingPlayback = false
    private var transportGeneration: Int? = null
    private var latestSearchRequestId = 0
    private val searchMetadata = mutableMapOf<String, Pair<String, String>>()
    private var lastRecordedGeneration = -1
    private var cacheEnabled = true
    private var cacheSizeMb = DEFAULT_CACHE_SIZE_MB
    private var cacheRadioEnabled = true
    private var shuffleEnabled = false
    private val shuffleOrder = mutableListOf<String>()
    private var shuffleIndex = -1
    private var loopMode = LoopModeOff
    // Stable snapshot of the cache-based loop-all order. Loop All must NOT recompute
    // the order live on every advance: recordPlayback() moves each played track to the
    // front of the recency-ordered list, so a live recompute makes "next" point back at
    // the just-previous track, ping-ponging between the two most-recent songs. Captured
    // once and only rebuilt when the set of cached tracks actually changes.
    private var loopAllOrder: List<String> = emptyList()
    // When set, Shuffle/Loop-All/rebuild draw from radioQueueSongs (an artist's catalog
    // or a song-seeded radio queue) instead of the on-device cache pool. See
    // playArtistRadio() and playSongRadio().
    private var radioQueueActive = false
    private var radioQueueName: String? = null
    private var radioQueueSongs: List<String> = emptyList()
    private var coverArtBackground = false
    private var watchTheme = ThemeDefault
    // Both default to the "highest quality" tier so anyone who never touches the new
    // Advanced setting keeps the pre-existing always-highest-bitrate behavior.
    private var watchAudioQuality = true   // false = Efficient, true = Balanced
    private var phoneAudioQuality = true   // false = Data Saver, true = High

    // -- Phone UI state exposure ---------------------------------------------------

    /** Memoised cover-art URL resolver. Both maybeSendCoverArt and the phone UI use it. */
    private val artworkUrlCache = mutableMapOf<String, String?>()

    suspend fun artworkUrlFor(videoId: String): String? {
        if (artworkUrlCache.containsKey(videoId)) return artworkUrlCache[videoId]
        val url = resolveCoverArtUrl(videoId)
        artworkUrlCache[videoId] = url
        return url
    }

    private val _uiState = MutableStateFlow(PlaybackUiState())
    val uiState: StateFlow<PlaybackUiState> = _uiState.asStateFlow()

    private val _errors = MutableSharedFlow<String>(extraBufferCapacity = 8)
    val errors: SharedFlow<String> = _errors.asSharedFlow()

    override fun onCreate() {
        super.onCreate()
        Log.i("PT2Music", "dreamwave playback service started")
        YouTube.locale = YouTubeLocale(gl = "US", hl = "en")
        getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE).run {
            phoneAudio = getBoolean(KEY_PHONE_AUDIO, false)
            routeEpoch = getInt(KEY_ROUTE_EPOCH, 0)
            phoneVolume = getInt(KEY_PHONE_VOLUME, DEFAULT_PHONE_VOLUME).coerceIn(0, 100)
            cacheEnabled = getBoolean(KEY_CACHE_ENABLED, true)
            cacheSizeMb = getInt(KEY_CACHE_SIZE_MB, DEFAULT_CACHE_SIZE_MB).coerceIn(MIN_CACHE_SIZE_MB, MAX_CACHE_SIZE_MB)
            loopMode = getInt(KEY_LOOP_MODE, LoopModeOff).coerceIn(LoopModeOff, LoopModeAll)
            coverArtBackground = getBoolean(KEY_COVER_ART_BG, false)
            watchTheme = getInt(KEY_THEME, ThemeDefault).coerceIn(ThemeTeal, ThemeMono)
            watchAudioQuality = getBoolean(KEY_WATCH_AUDIO_QUALITY, true)
            phoneAudioQuality = getBoolean(KEY_PHONE_AUDIO_QUALITY, true)
            cacheRadioEnabled = getBoolean(KEY_CACHE_RADIO, true)
        }
        transport = PebbleAudioTransport(applicationContext, scope)
        // When the watch stops acking audio chunks the transport gives up silently;
        // surface that to the watch and the phone UI instead of leaving both showing
        // a playing state over a dead stream.
        transport.onStreamDied = {
            scope.launch {
                transport.sendEvent(Protocol.eventError, "Watch audio link lost", activeGeneration)
                _errors.tryEmit("Watch audio link lost â€” playback is silent")
                sendStateSnapshot()
            }
        }
        val pcmProcessor = PebblePcmAudioProcessor(transport::offer)
        mediaCache = openMediaCache()
        cacheDataSourceFactory = CacheDataSource.Factory()
            .setCache(mediaCache)
            .setUpstreamDataSourceFactory(DefaultDataSource.Factory(this))
            .setFlags(CacheDataSource.FLAG_IGNORE_CACHE_ON_ERROR)
        uncachedMediaSourceFactory = DefaultMediaSourceFactory(DefaultDataSource.Factory(this))
        player = ExoPlayer.Builder(this)
            .setRenderersFactory(PcmRenderersFactory(this, pcmProcessor))
            .setMediaSourceFactory(DefaultMediaSourceFactory(cacheDataSourceFactory))
            .setWakeMode(C.WAKE_MODE_NETWORK)
            // Handle audio focus so the app pauses for calls and ducks like a real
            // music player. On the watch route the player is muted (volume 0) and we
            // rely on the watch speaker, but focus handling stays harmless there.
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(C.USAGE_MEDIA)
                    .setContentType(C.AUDIO_CONTENT_TYPE_MUSIC)
                    .build(),
                /* handleAudioFocus = */ true,
            )
            .build()
            .apply {
                repeatMode = if (loopMode == LoopModeOne) Player.REPEAT_MODE_ONE else Player.REPEAT_MODE_OFF
                // On the phone route loudness is controlled by the system media stream,
                // so the player runs at full volume; on the watch route it is silenced.
                volume = if (phoneAudio) 1f else 0f
                addListener(object : Player.Listener {
                    override fun onPlaybackStateChanged(playbackState: Int) {
                        if (playbackState == Player.STATE_READY && player.duration > 0) {
                            scope.launch { transport.sendPlaybackInfo(player.duration, activeGeneration) }
                            publishState()
                        }
                        if (playbackState == Player.STATE_ENDED) {
                            // Mirrors skipTrack()'s precedence (loop-all, then shuffle) so a
                            // track that finishes on its own continues the same session a
                            // manual Next would - including Artist Radio, which runs on
                            // shuffleEnabled rather than loopMode.
                            val nextVideoId = when {
                                loopMode == LoopModeAll -> nextLoopAllVideoId()
                                shuffleEnabled -> nextShuffleVideoId()
                                else -> null
                            }
                            if (nextVideoId != null) {
                                scope.launch { play(nextVideoId, activeGeneration) }
                                return
                            }
                            // A finished track should not appear in Continue Listening.
                            activeVideoId?.let(::clearResumePosition)
                            scope.launch {
                                stopTransport()
                                transport.sendEvent(
                                    Protocol.eventAudioEnd,
                                    generation = activeGeneration,
                                )
                            }
                        }
                        if (playbackState == Player.STATE_IDLE && !resolvingPlayback) {
                            scope.launch {
                                stopTransport()
                                activeVideoId = null
                                activeTitle = null
                                activeArtist = null
                                sendStateSnapshot()
                            }
                            // Nothing is playing: drop the foreground notification so it
                            // does not linger while idle. It is re-created on next play.
                            leaveForeground()
                        }
                    }

                    override fun onIsPlayingChanged(isPlaying: Boolean) {
                        if (activeVideoId == null) return
                        if (isPlaying) {
                            // Ensure the service is foregrounded when playback starts.
                            // This covers the case where the service was pre-started by
                            // MainActivity (so ensureStartedForPlayback returned early)
                            // but leaveForeground() ran when the player went idle —
                            // without this, the media notification never reappears.
                            promoteToForeground()
                        }
                        scope.launch {
                            if (isPlaying) {
                                recordPlayback()
                                if (phoneAudio) {
                                    stopTransport()
                                    transport.sendEvent(Protocol.eventAudioStart, generation = activeGeneration)
                                } else {
                                    startTransport(activeGeneration)
                                }
                            } else {
                                stopTransport()
                                // Remember where the user paused so it can be resumed later.
                                recordResumePosition()
                            }
                            sendStateSnapshot()
                        }
                    }

                    override fun onPlayerError(error: androidx.media3.common.PlaybackException) {
                        Log.e("PT2Music", "Media3 playback failed", error)
                        scope.launch {
                            stopTransport()
                            transport.sendEvent(
                                Protocol.eventError,
                                error.message,
                                activeGeneration,
                            )
                            _errors.tryEmit("Playback failed: ${error.message ?: "unknown error"}")
                        }
                    }

                    override fun onPositionDiscontinuity(
                        oldPosition: Player.PositionInfo,
                        newPosition: Player.PositionInfo,
                        reason: Int,
                    ) {
                        scope.launch {
                            transport.sendPlaybackPosition(newPosition.positionMs, activeGeneration)
                            publishState()
                        }
                    }
                })
            }
        // setSessionActivity is what makes the system media control tappable â€” without
        // it the control has nowhere to send you and some surfaces decline to offer it
        // at all. It is listed as required for the mobile media-control surface.
        // EXTRA_OPEN_PLAYER tells MainActivity to auto-expand the Now Playing sheet
        // when the user taps the system media notification.
        val sessionActivity = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP)
                .putExtra(MainActivity.EXTRA_OPEN_PLAYER, true),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        mediaSession = MediaSession.Builder(this, player)
            .setSessionActivity(sessionActivity)
            .build()
        // Full media notification on the phone route; a minimal status notice while
        // audio is streamed to the watch (phone is muted there).
        setMediaNotificationProvider(RouteAwareNotificationProvider(this) { phoneAudio })
    }

    private var serviceStarted = false

    /**
     * Opens the media cache, recovering from a corrupt index instead of taking the
     * service down. The constructor reads the index from disk, and a process killed
     * mid-write can leave it unreadable â€” without this guard every watch-initiated
     * playback attempt would crash at service creation, forever, until the user
     * cleared app data. Recovery deletes the directory once and retries; if the retry
     * also fails (e.g. the first attempt's folder lock leaked), playback falls back to
     * a scratch cache dir with user caching disabled rather than not playing at all.
     */
    private fun openMediaCache(): SimpleCache {
        fun create(dirName: String) = SimpleCache(
            filesDir.resolve(dirName),
            LeastRecentlyUsedCacheEvictor(cacheSizeMb.toLong() * 1024L * 1024L),
            StandaloneDatabaseProvider(this),
        )
        return runCatching { create(MEDIA_CACHE_DIR) }
            .getOrElse { firstError ->
                Log.e("PT2Music", "Media cache failed to open; resetting", firstError)
                runCatching { filesDir.resolve(MEDIA_CACHE_DIR).deleteRecursively() }
                runCatching { create(MEDIA_CACHE_DIR) }.getOrElse { secondError ->
                    Log.e("PT2Music", "Media cache unrecoverable; using scratch dir", secondError)
                    cacheEnabled = false
                    create(MEDIA_CACHE_SCRATCH_DIR)
                }
            }
    }

    /**
     * Ensures the service is a *started* foreground service (not just bound) so Media3's
     * MediaSessionService will post and maintain the now-playing notification. The watch
     * bridge only ever binds the service, so without this the media notification never
     * appears.
     *
     * startForegroundService is required because the play command often arrives while
     * the app is backgrounded (a plain startService would throw there). The
     * mediaPlayback foreground-service type permits this, and Media3 calls
     * startForeground() itself as soon as the player becomes active (right after the
     * player.play() call that follows this).
     */
    private fun ensureStartedForPlayback() {
        if (serviceStarted) return
        runCatching {
            val intent = Intent(this, PebblePlaybackService::class.java)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                startForegroundService(intent)
            } else {
                startService(intent)
            }
            serviceStarted = true
        }.onFailure {
            // Typically ForegroundServiceStartNotAllowedException on Android 12+ when the
            // app has no foreground context and no battery-optimization exemption. The
            // media notification cannot appear and the system may freeze playback soon.
            Log.e(
                "PT2Music",
                "Could not start playback service; open the Dreamwave app once and grant " +
                    "unrestricted battery use to allow watch-initiated playback",
                it,
            )
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        serviceStarted = true
        // Immediately satisfy the startForegroundService() 5-second contract by
        // promoting to the foreground ourselves. Media3 will replace this with its
        // own session-linked media notification via the notification provider, but by
        // calling startForeground() here first we guarantee the service is foreground
        // (and thus not killed mid-song) even on Android 16 where Media3's own timing
        // can miss the window for a background/bound-initiated start.
        promoteToForeground()
        return super.onStartCommand(intent, flags, startId)
    }

    private var foregrounded = false

    private fun ensureForegroundChannel() {
        val manager = getSystemService(NotificationManager::class.java)
        if (manager.getNotificationChannel(MEDIA_CHANNEL_ID) == null) {
            manager.createNotificationChannel(
                NotificationChannel(
                    MEDIA_CHANNEL_ID,
                    "Playback",
                    NotificationManager.IMPORTANCE_LOW,
                ).apply { setShowBadge(false) },
            )
        }
    }

    /**
     * Promotes the service to the foreground with a media notification.
     *
     * When the player is already active (BUFFERING or READY), Media3 has already
     * built a proper notification with title/artist/artwork — posting our own
     * placeholder now would replace it with a lesser one. Instead we delegate to
     * [onUpdateNotification] with startInForegroundRequired=true, which calls
     * startForeground() with Media3's own notification under the same ID.
     */
    private fun promoteToForeground() {
        if (foregrounded) return
        runCatching {
            if (player.playbackState == Player.STATE_READY ||
                player.playbackState == Player.STATE_BUFFERING
            ) {
                // Player is already active: let Media3's notification provider build
                // the real notification (with metadata and artwork) and use that for
                // the foreground promotion, rather than replacing it with a placeholder.
                onUpdateNotification(mediaSession, true)
                foregrounded = true
                return
            }
            ensureForegroundChannel()
            val metadata = player.mediaMetadata
            val title = metadata.title?.toString() ?: activeTitle ?: "dreamwave"
            val artist = metadata.artist?.toString() ?: activeArtist
            val notification = Notification.Builder(this, MEDIA_CHANNEL_ID)
                .setContentTitle(title)
                .setContentText(artist ?: if (phoneAudio) "Playing" else "Playing on watch")
                .setSmallIcon(android.R.drawable.stat_sys_headset)
                .setOngoing(true)
                // MediaStyle + the session token even on the placeholder. Media3 swaps
                // its own notification in a moment later under the same id, but until
                // it does this is the service's only posted notification — and a plain
                // one gives the system nothing to build a media control from, so the
                // app is simply absent from the control surface for that window.
                .setStyle(Notification.MediaStyle().setMediaSession(mediaSession.platformToken))
                .build()
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                startForeground(
                    MEDIA_NOTIFICATION_ID,
                    notification,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK,
                )
            } else {
                startForeground(MEDIA_NOTIFICATION_ID, notification)
            }
            foregrounded = true
        }.onFailure { Log.e("PT2Music", "startForeground failed", it) }
    }

    /** Drops the foreground state + placeholder notification when idle. */
    private fun leaveForeground() {
        if (!foregrounded) return
        runCatching {
            stopForeground(STOP_FOREGROUND_REMOVE)
            foregrounded = false
        }.onFailure { Log.w("PT2Music", "stopForeground failed", it) }
    }

    /** Nudges Media3 to rebuild the notification (e.g. after an audio-route change). */
    private fun refreshNotification() {
        if (!serviceStarted) return
        runCatching {
            if (player.playbackState != Player.STATE_IDLE) {
                onUpdateNotification(mediaSession, false)
            }
        }
    }

    override fun onGetSession(controllerInfo: MediaSession.ControllerInfo): MediaSession = mediaSession

    override fun onBind(intent: Intent?): IBinder? =
        if (intent?.action == ACTION_LOCAL_BIND) localBinder else super.onBind(intent)

    suspend fun onWatchMessage(data: PebbleDictionary): ReceiveResult {
        val command = (data[Protocol.keyCommand] as? PebbleDictionaryItem.Int32)?.value ?: return ReceiveResult.Nack
        Log.i("PT2Music", "Watch command received: $command")
        // For commands that (re)start playback, promote to a foreground service NOW,
        // synchronously inside the watch-message window. This is the allowed moment to
        // start an FGS; doing it here - before the slow stream resolution in play() -
        // keeps the service foreground (and unkillable) for the whole song. The later
        // background start from inside play() would be denied on Android 12+/16.
        if (command == Protocol.commandPlay || command == Protocol.commandResume) {
            ensureStartedForPlayback()
        }
        when (command) {
            Protocol.commandHello -> {
                transport.sendEvent(Protocol.eventReady, "Android bridge ready")
                // Pull the watch's settings blob: the watch treats an inbound
                // CommandSyncSettings with no config keys as a request to re-send them.
                // Settings convergence is then driven by an explicit request on every
                // connect instead of the watch re-pushing its blob after every snapshot.
                transport.sendEvent(Protocol.commandSyncSettings)
                // A freshly launched watchapp holds no cover art â€” its buffer is
                // allocated on demand and freed when the app exits. deliveredCover is
                // keyed on (track, dim, encoding, generation) and *none* of those
                // change when the app is merely reopened, so the idempotence guard
                // would otherwise conclude the art had already been sent and this
                // instance would never receive one. Hello is precisely the watch
                // telling us it has no state, so the record of what it has goes with it.
                deliveredCover = null
                sendStateSnapshot()
                if (!phoneAudio && player.isPlaying) startTransport(activeGeneration)
                // Push the cover explicitly rather than waiting for the settings sync
                // that usually follows: that ordering is the watch's to choose, and the
                // guard makes a duplicate send a no-op anyway.
                activeVideoId?.let { maybeSendCoverArt(it, activeGeneration) }
            }
            Protocol.commandRequestState -> sendStateSnapshot()
            Protocol.commandRequestLibrary -> {
                val libraryType = (data[Protocol.keyLibraryType] as? PebbleDictionaryItem.Int32)?.value
                    ?: return ReceiveResult.Nack
                val limit = (data[Protocol.keyLibraryLimit] as? PebbleDictionaryItem.Int32)?.value
                sendLibrary(libraryType, limit)
            }
            Protocol.commandSearch -> {
                val query = (data[Protocol.keyQuery] as? PebbleDictionaryItem.Text)?.value
                    ?: return ReceiveResult.Nack
                val requestId = (data[Protocol.keySearchRequestId] as? PebbleDictionaryItem.Int32)?.value ?: 0
                val limit = (data[Protocol.keySearchLimit] as? PebbleDictionaryItem.Int32)?.value
                    ?.coerceIn(1, SEARCH_LIMIT_MAX) ?: DEFAULT_SEARCH_LIMIT
                val searchMode = (data[Protocol.keySearchMode] as? PebbleDictionaryItem.Int32)?.value
                    ?: Protocol.searchModeSong
                search(query, requestId, limit, searchMode)
            }
            Protocol.commandPlay -> {
                val videoId = (data[Protocol.keyVideoId] as? PebbleDictionaryItem.Text)?.value
                    ?: return ReceiveResult.Nack
                val generation = (data[Protocol.keyGeneration] as? PebbleDictionaryItem.Int32)?.value
                    ?: return ReceiveResult.Nack
                dispatchPlay(videoId, generation)
            }
            Protocol.commandStop -> stopPlayback()
            Protocol.commandPause -> {
                val generation = (data[Protocol.keyGeneration] as? PebbleDictionaryItem.Int32)?.value
                    ?: return ReceiveResult.Nack
                pausePlayback(generation)
            }
            Protocol.commandResume -> {
                val generation = (data[Protocol.keyGeneration] as? PebbleDictionaryItem.Int32)?.value
                    ?: return ReceiveResult.Nack
                resumePlayback(generation)
            }
            Protocol.commandToggleLoop -> toggleLoop()
            Protocol.commandToggleShuffle -> {
                val generation = (data[Protocol.keyGeneration] as? PebbleDictionaryItem.Int32)?.value
                    ?: activeGeneration
                toggleShuffle(generation)
            }
            Protocol.commandPrevious -> {
                val generation = (data[Protocol.keyGeneration] as? PebbleDictionaryItem.Int32)?.value
                    ?: return ReceiveResult.Nack
                skipTrack(previous = true, generation = generation)
            }
            Protocol.commandNext -> {
                val generation = (data[Protocol.keyGeneration] as? PebbleDictionaryItem.Int32)?.value
                    ?: return ReceiveResult.Nack
                skipTrack(previous = false, generation = generation)
            }
            Protocol.commandToggleFavorite -> {
                val videoId = (data[Protocol.keyVideoId] as? PebbleDictionaryItem.Text)?.value
                    ?: activeVideoId ?: return ReceiveResult.Nack
                toggleFavorite(videoId)
            }
            Protocol.commandDeleteCached -> {
                val videoId = (data[Protocol.keyVideoId] as? PebbleDictionaryItem.Text)?.value
                    ?: return ReceiveResult.Nack
                deleteCachedSong(videoId)
            }
            Protocol.commandRequestQueue -> sendQueue()
            Protocol.commandQueueJump -> {
                val videoId = (data[Protocol.keyVideoId] as? PebbleDictionaryItem.Text)?.value
                    ?: return ReceiveResult.Nack
                val generation = (data[Protocol.keyGeneration] as? PebbleDictionaryItem.Int32)?.value
                    ?: return ReceiveResult.Nack
                jumpQueue(videoId, generation)
            }
            Protocol.commandSeek -> {
                val positionMs = (data[Protocol.keyPosition] as? PebbleDictionaryItem.Int32)?.value
                    ?: return ReceiveResult.Nack
                val generation = (data[Protocol.keyGeneration] as? PebbleDictionaryItem.Int32)?.value
                seekTo(positionMs.toLong(), generation)
            }
            Protocol.commandSetAudioRoute -> {
                val route = (data[Protocol.keyAudioRoute] as? PebbleDictionaryItem.Int32)?.value
                    ?: return ReceiveResult.Nack
                val generation = (data[Protocol.keyGeneration] as? PebbleDictionaryItem.Int32)?.value
                    ?: return ReceiveResult.Nack
                val routeEpochMsg = (data[Protocol.keyRouteEpoch] as? PebbleDictionaryItem.Int32)?.value
                setAudioRoute(route, generation, routeEpochMsg)
            }
            Protocol.commandSetVolume -> {
                val volume = (data[Protocol.keyVolume] as? PebbleDictionaryItem.Int32)?.value
                    ?: return ReceiveResult.Nack
                setPhoneVolume(volume)
            }
            Protocol.commandSyncSettings -> {
                val route = (data[Protocol.keyConfigAudioRoute] as? PebbleDictionaryItem.Int32)?.value
                val watchVolume = (data[Protocol.keyConfigWatchVolume] as? PebbleDictionaryItem.Int32)?.value
                val phoneVolumeConfig = (data[Protocol.keyConfigPhoneVolume] as? PebbleDictionaryItem.Int32)?.value
                val inputMode = (data[Protocol.keyConfigInputMode] as? PebbleDictionaryItem.Int32)?.value
                val showProgress = (data[Protocol.keyConfigShowProgress] as? PebbleDictionaryItem.Int32)?.value
                val cacheEnabledConfig = (data[Protocol.keyConfigCacheEnabled] as? PebbleDictionaryItem.Int32)?.value
                val cacheSizeConfig = (data[Protocol.keyConfigCacheSizeMb] as? PebbleDictionaryItem.Int32)?.value
                val coverArtBackgroundConfig =
                    (data[Protocol.keyConfigCoverArtBackground] as? PebbleDictionaryItem.Int32)?.value
                val themeConfig = (data[Protocol.keyTheme] as? PebbleDictionaryItem.Int32)?.value
                val watchQualityConfig =
                    (data[Protocol.keyConfigWatchAudioQuality] as? PebbleDictionaryItem.Int32)?.value
                val phoneQualityConfig =
                    (data[Protocol.keyConfigPhoneAudioQuality] as? PebbleDictionaryItem.Int32)?.value
                val cacheRadioConfig =
                    (data[Protocol.keyConfigCacheRadio] as? PebbleDictionaryItem.Int32)?.value
                val routeEpochConfig =
                    (data[Protocol.keyRouteEpoch] as? PebbleDictionaryItem.Int32)?.value
                applySyncedSettings(
                    route,
                    watchVolume,
                    phoneVolumeConfig,
                    inputMode,
                    showProgress,
                    cacheEnabledConfig,
                    cacheSizeConfig,
                    coverArtBackgroundConfig,
                    themeConfig,
                    watchQualityConfig,
                    phoneQualityConfig,
                    cacheRadioConfig,
                    routeEpochConfig,
                )
            }
            else -> return ReceiveResult.Nack
        }
        return ReceiveResult.Ack
    }

    /**
     * Single dispatch for every "play this id" request, watch or phone UI. The
     * synthetic radio prefixes are resolved here so both entry points behave
     * identically â€” previously the phone UI's UiCommand.Play went straight to play()
     * and tried to stream a literal "artist:..." id.
     */
    private suspend fun dispatchPlay(videoId: String, generation: Int) {
        if (videoId.startsWith(ARTIST_RADIO_PREFIX)) {
            playArtistRadio(videoId.removePrefix(ARTIST_RADIO_PREFIX), generation)
        } else if (videoId.startsWith(SONG_RADIO_PREFIX)) {
            playSongRadio(videoId.removePrefix(SONG_RADIO_PREFIX), generation)
        } else if (videoId.startsWith(PLAYLIST_PREFIX)) {
            playPlaylist(videoId.removePrefix(PLAYLIST_PREFIX), generation)
        } else {
            // A specific song was picked directly, ending any active radio
            // session so it doesn't keep steering Shuffle/Loop-All/Next.
            // Rebuild shuffleOrder from the normal cache pool right away rather
            // than leaving the foreign queue around for play() to append this
            // song onto below - otherwise the next shuffle/skip would mix this
            // song into someone else's queue.
            val wasRadioQueue = radioQueueActive
            radioQueueActive = false
            radioQueueName = null
            radioQueueSongs = emptyList()
            if (wasRadioQueue && shuffleEnabled) rebuildShuffleOrder()
            play(videoId, generation)
        }
    }

    /**
     * Artist mode: every result is a synthetic row whose videoId carries the
     * [ARTIST_RADIO_PREFIX] - the watch treats it like any other result row, and
     * [Protocol.commandPlay] recognizes the prefix to start an artist-scoped shuffle
     * queue instead of playing it as a track. No relevance heuristic needed here (unlike
     * the first attempt at this feature) since the user explicitly chose Artist Radio
     * mode before searching.
     */
    private suspend fun searchArtists(query: String, limit: Int): Result<List<PipePipeResolver.SearchResult>> =
        YouTube.search(query = query, filter = YouTube.SearchFilter.FILTER_ARTIST).mapCatching { result ->
            result.items
                .filterIsInstance<ArtistItem>()
                .distinctBy { it.id }
                .take(limit)
                .map { artist ->
                    PipePipeResolver.SearchResult(
                        videoId = "$ARTIST_RADIO_PREFIX${artist.id}",
                        title = "${artist.title} Radio",
                        artist = "Shuffle their songs",
                    )
                }
        }

    /**
     * Runs a search and returns the results. Extracted from [search] so the phone UI
     * can use the identical resolution path â€” YouTube Music first, PipePipe as the
     * fallback â€” instead of a parallel implementation that could drift from it.
     */
    private suspend fun fetchSearchResults(
        query: String,
        limit: Int,
        searchMode: Int,
    ): Result<List<PipePipeResolver.SearchResult>> = withContext(Dispatchers.IO) {
        if (searchMode == Protocol.searchModeArtist) {
            searchArtists(query, limit)
        } else {
            // Song Radio uses the same plain song search as Protocol.searchModeSong - the
            // user is picking a real track - but marks each result's videoId with
            // SONG_RADIO_PREFIX so Protocol.commandPlay starts a radio queue seeded
            // from that song (see playSongRadio()) instead of just playing it.
            val songRadio = searchMode == Protocol.searchModeSongRadio
            val musicResults = YouTube.search(
                query = query,
                filter = YouTube.SearchFilter.FILTER_SONG,
            ).mapCatching { result ->
                result.items
                    .filterIsInstance<SongItem>()
                    .filterNot { it.isEpisode || it.isVideoSong }
                    .distinctBy { it.id }
                    .take(limit)
                    .map { song ->
                        PipePipeResolver.SearchResult(
                            videoId = if (songRadio) "$SONG_RADIO_PREFIX${song.id}" else song.id,
                            title = song.title,
                            artist = song.artists.joinToString(", ") { it.name },
                        )
                    }
            }
            if (musicResults.isSuccess) {
                musicResults
            } else {
                Log.w("PT2Music", "YouTube Music search failed; using PipePipe", musicResults.exceptionOrNull())
                runCatching { PipePipeResolver.search(query, limit) }.map { results ->
                    if (songRadio) {
                        results.map { it.copy(videoId = "$SONG_RADIO_PREFIX${it.videoId}") }
                    } else {
                        results
                    }
                }
            }
        }
    }

    /**
     * Phone-facing search. Unlike [search] this returns results instead of pushing
     * them to the watch, and does not touch `latestSearchRequestId` â€” a phone search
     * must never cancel an in-flight watch search.
     *
     * Results are recorded in [searchMetadata] so that playing one still gives the
     * cache and queue screens a real title and artist to show.
     */
    suspend fun searchForUi(
        query: String,
        limit: Int = 25,
        searchMode: Int = Protocol.searchModeSong,
    ): Result<List<SearchResultItem>> {
        if (query.isBlank()) return Result.success(emptyList())
        recordRecentSearch(query)
        return fetchSearchResults(query, limit, searchMode)
            .onSuccess { results ->
                results.forEach { searchMetadata[it.videoId] = it.title to it.artist }
            }
            .map { results ->
                results.map { SearchResultItem(it.videoId, it.title, it.artist) }
            }
    }

    private suspend fun search(query: String, requestId: Int, limit: Int, searchMode: Int) {
        Log.i("PT2Music", "Searching YouTube Music for: $query (limit $limit, mode $searchMode)")
        latestSearchRequestId = requestId
        recordRecentSearch(query)
        val songs = fetchSearchResults(query, limit, searchMode)
        songs.onSuccess { results ->
                if (requestId != latestSearchRequestId) return@onSuccess
                Log.i("PT2Music", "Search returned ${results.size} songs")
                searchMetadata.clear()
                results.forEachIndexed { index, song ->
                    searchMetadata[song.videoId] = song.title to song.artist
                    transport.sendSearchResult(
                        index,
                        song.videoId,
                        song.title,
                        song.artist,
                        requestId,
                    )
                }
                transport.sendSearchComplete(requestId)
        }.onFailure {
            if (requestId != latestSearchRequestId) return@onFailure
            Log.e("PT2Music", "YouTube Music search failed", it)
            transport.sendSearchError(requestId, "Search request failed")
        }
    }

    /**
     * Serializes track starts. Play requests can overlap (watch command, phone UI tap,
     * track-end auto-advance, quality re-swap), and two interleaved play() bodies mix
     * stop/prepare/play with the wrong transport and metadata state. Last request wins,
     * in arrival order â€” simple and predictable for a one-track player.
     */
    private val playMutex = kotlinx.coroutines.sync.Mutex()

    private suspend fun play(videoId: String, generation: Int, startPositionMs: Long = 0) =
        playMutex.withLock { playLocked(videoId, generation, startPositionMs) }

    private suspend fun playLocked(videoId: String, generation: Int, startPositionMs: Long = 0) {
        Log.i("PT2Music", "Play requested for: $videoId")
        resolvingPlayback = true
        val fullyCached = isFullyCached(videoId)
        // Whether this track goes through the cache-backed data source at all. An
        // already-cached track always does - those bytes are the cheapest source we have,
        // and the placeholder stream URL below is only resolvable from the cache. New
        // tracks only do when caching is on and, for radio queues, "Cache radio" is on.
        // The flag has to gate the data source itself, not just the pre-download: the
        // player's own reads write through CacheDataSource as they stream, and a null
        // customCacheKey does not stop that - it just caches under the URL instead.
        val useCache = fullyCached || (cacheEnabled && (!radioQueueActive || cacheRadioEnabled))
        val streamUrl = if (fullyCached) {
            "https://music.youtube.com/watch?v=$videoId"
        } else {
            resolveStreamUrl(videoId, currentMaxBitrateKbps())
        }
        if (streamUrl == null) {
            resolvingPlayback = false
            transport.sendEvent(Protocol.eventError, "No playable audio source", generation)
            _errors.tryEmit("No playable audio source for this track")
            return
        }

        player.stop()
        player.clearMediaItems()
        activeGeneration = generation
        lastRecordedGeneration = -1
        activeVideoId = videoId
        if (shuffleEnabled) {
            if (videoId !in shuffleOrder) shuffleOrder.add(videoId)
            shuffleIndex = shuffleOrder.indexOf(videoId)
        }
        // The in-memory search metadata does not survive a service restart (the system
        // may kill the background service between browsing and playing), so fall back
        // to the persisted track metadata before giving up on a real title.
        val stored = storedMetadataFor(videoId)
        activeTitle = searchMetadata[videoId]?.first ?: stored?.first
        activeArtist = searchMetadata[videoId]?.second ?: stored?.second
        if (phoneAudio) {
            transport.sendEvent(Protocol.eventAudioStart, generation = generation)
        } else {
            // Hold the audio stream so the cover art transfers during the pre-playback
            // buffering window (no audio chunks flowing yet) rather than mid-stream.
            // maybeSendCoverArt() releases the hold when it finishes; the timeout is a
            // safety net so a slow/failed cover fetch can never stall audio for long.
            if (coverArtBackground) {
                transport.holdAudioForCover()
                scope.launch {
                    delay(COVER_HOLD_TIMEOUT_MS)
                    transport.releaseAudioForCover()
                }
            }
            startTransport(generation)
        }
        val metadata = MediaMetadata.Builder()
            .setTitle(activeTitle ?: videoId)
            .apply {
                activeArtist?.let(::setArtist)
                // Peeked from the memoised map rather than resolved here: artworkUrlFor
                // would put a network round trip in front of playback starting. The
                // phone UI resolves covers for the queue, so this is usually already
                // warm; when it is not, the control simply shows no art this once.
                artworkUrlCache[videoId]?.let { setArtworkUri(Uri.parse(it)) }
            }
            .build()
        val mediaItem = MediaItem.Builder()
            .setMediaId(videoId)
            .setUri(streamUrl)
            .setCustomCacheKey(if (useCache) videoId else null)
            .setMediaMetadata(metadata)
            .build()
        if (useCache) {
            player.setMediaItem(mediaItem)
        } else {
            player.setMediaSource(uncachedMediaSourceFactory.createMediaSource(mediaItem))
        }
        resolvingPlayback = false
        player.prepare()
        // Explicit user play requests start from the beginning; a quality-tier
        // re-swap of the currently playing track passes its captured position instead.
        player.seekTo(startPositionMs)
        // Promote to a started foreground service so Media3 posts the now-playing
        // notification (the service is otherwise only bound by the watch bridge).
        ensureStartedForPlayback()
        player.play()

        maybeSendCoverArt(videoId, generation)

        // Resolve the artwork URL asynchronously and update the media metadata so the
        // system media notification shows album art. The artworkUrlCache peek above only
        // hits when the phone UI previously resolved this track — for watch-initiated
        // playback the cache is cold and the notification has no artwork. Doing it here
        // (after play() has already started) doesn't block playback; Media3 re-reads
        // the player's metadata and updates the notification when we replace the item.
        if (artworkUrlCache[videoId] == null) {
            scope.launch {
                val artUrl = artworkUrlFor(videoId)
                if (artUrl != null && activeVideoId == videoId) {
                    val currentItem = player.currentMediaItem ?: return@launch
                    if (currentItem.mediaId != videoId) return@launch
                    val updatedMetadata = currentItem.mediaMetadata.buildUpon()
                        .setArtworkUri(Uri.parse(artUrl))
                        .build()
                    val updatedItem = currentItem.buildUpon()
                        .setMediaMetadata(updatedMetadata)
                        .build()
                    player.replaceMediaItem(player.currentMediaItemIndex, updatedItem)
                }
            }
        }

        // Explicitly download the full file so that playing a song reliably caches it,
        // even if the listener seeks around or leaves before the track ends. Skipped
        // for radio tracks (Artist Radio or Song Radio) when "Cache radio" is off, so
        // playing through a generated queue doesn't fill the cache with one-off plays.
        if (!fullyCached && useCache) startCacheDownload(videoId, streamUrl)
        publishState()
    }

    /**
     * Starts an artist-scoped radio session: fetches the artist's own YT Music
     * shuffle queue when available (a curated, artist-only shuffle - exactly what the
     * official app's "Shuffle" button on an artist page does), falling back to the
     * artist page's own song listings otherwise. While active, Shuffle/Loop-All/Next/
     * Previous draw from [radioQueueSongs] instead of the on-device cache pool (see
     * [rebuildShuffleOrder] and [loopAllPool]) until a specific song is picked directly
     * (see the `Protocol.commandPlay` handler) or shuffle is turned off.
     */
    private suspend fun playArtistRadio(browseId: String, generation: Int) {
        Log.i("PT2Music", "Artist radio requested for: $browseId")
        val page = withContext(Dispatchers.IO) { YouTube.artist(browseId).getOrNull() }
        if (page == null) {
            transport.sendEvent(Protocol.eventError, "No songs found for artist", generation)
            return
        }
        val shuffleQueue = withContext(Dispatchers.IO) {
            page.artist.shuffleEndpoint?.let { endpoint ->
                YouTube.next(endpoint).getOrNull()?.items
            }
        }
        val songItems = (
            shuffleQueue?.takeIf { it.isNotEmpty() }
                ?: page.sections.flatMap { it.items }.filterIsInstance<SongItem>()
            ).distinctBy { it.id }
        if (songItems.isEmpty()) {
            transport.sendEvent(Protocol.eventError, "No songs found for artist", generation)
            return
        }
        // play() looks up the title/artist to report from searchMetadata, which a
        // normal search populates but an artist's catalog never did - without this,
        // every artist-radio track fell back to a bare videoId with no artist, which
        // is what was showing up as unreliable title/artist/cover art.
        songItems.forEach { song ->
            searchMetadata[song.id] = song.title to song.artists.joinToString(", ") { it.name }
        }
        val songs = songItems.map { it.id }
        radioQueueActive = true
        radioQueueName = page.artist.title
        radioQueueSongs = songs
        shuffleEnabled = true
        shuffleOrder.clear()
        // shuffleEndpoint's queue is already YT-shuffled; the ArtistPage-flatten
        // fallback is in on-page order, so shuffle it ourselves.
        shuffleOrder.addAll(if (shuffleQueue != null) songs else songs.shuffled())
        shuffleIndex = 0
        play(shuffleOrder[shuffleIndex], generation)
    }

    /**
     * Starts a song-seeded radio session: calls YouTube Music's "watch next" queue
     * for the picked track - the same call the official app makes when you hit "Start
     * radio" on a song - and queues up the relevance-ordered results that follow it.
     * Unlike Artist Radio, this does not force shuffle on; it sets loopMode to
     * LoopModeAll instead, so Next/Previous walk [radioQueueSongs] in the order YT
     * Music generated rather than randomizing it (see [loopAllPool]/[skipInLoopAll]),
     * which better matches what "song radio" means in the official app. Turning
     * shuffle on afterward still works - [rebuildShuffleOrder]'s radioQueueActive
     * branch shuffles within radioQueueSongs same as it does for Artist Radio.
     */
    private suspend fun playSongRadio(seedVideoId: String, generation: Int) {
        Log.i("PT2Music", "Song radio requested for: $seedVideoId")
        val result = withContext(Dispatchers.IO) {
            YouTube.next(WatchEndpoint(videoId = seedVideoId)).getOrNull()
        }
        val songItems = result?.items.orEmpty().distinctBy { it.id }
        if (songItems.isEmpty()) {
            transport.sendEvent(Protocol.eventError, "No radio songs found", generation)
            return
        }
        songItems.forEach { song ->
            searchMetadata[song.id] = song.title to song.artists.joinToString(", ") { it.name }
        }
        val songs = songItems.map { it.id }
        radioQueueActive = true
        radioQueueName = null
        radioQueueSongs = songs
        loopMode = LoopModeAll
        val startIndex = songs.indexOf(seedVideoId).takeIf { it >= 0 } ?: 0
        play(songs[startIndex], generation)
    }

    private fun startCacheDownload(videoId: String, streamUrl: String) {
        if (!cacheEnabled) return
        cacheDownloadJob?.cancel()
        cacheDownloadJob = scope.launch(Dispatchers.IO) {
            runCatching {
                val dataSpec = DataSpec.Builder()
                    .setUri(Uri.parse(streamUrl))
                    .setKey(videoId)
                    .build()
                val writer = CacheWriter(
                    cacheDataSourceFactory.createDataSource(),
                    dataSpec,
                    null,
                ) { _, _, _ -> ensureActive() }
                writer.cache()
            }.onFailure {
                Log.w("PT2Music", "Cache download failed for $videoId", it)
            }.onSuccess {
                Log.i("PT2Music", "Cached full track: $videoId")
            }
        }
    }

    /**
     * Both routes default to the highest-bitrate tier (null ceiling = unchanged
     * behavior); Efficient/Data Saver cap what gets fetched, since the watch route
     * downsamples to ~8 KB/s mono ADPCM immediately after decode regardless of source
     * quality, making anything above a modest ceiling wasted phone bandwidth/battery.
     */
    private fun currentMaxBitrateKbps(): Int? = when {
        phoneAudio && !phoneAudioQuality -> PHONE_DATA_SAVER_KBPS
        !phoneAudio && !watchAudioQuality -> WATCH_EFFICIENT_KBPS
        else -> null
    }

    private suspend fun resolveStreamUrl(videoId: String, maxBitrateKbps: Int?): String? {
        Log.i("PT2Music", "Resolving $videoId with PipePipe (maxBitrateKbps=$maxBitrateKbps)")
        val pipePipe = withContext(Dispatchers.IO) {
            runCatching { PipePipeResolver.resolve(videoId, maxBitrateKbps) }
                .onFailure { Log.e("PT2Music", "PipePipe failed", it) }
                .getOrNull()
        }
        if (pipePipe != null && validateStream(pipePipe, "PipePipe")) {
            Log.i("PT2Music", "PipePipe returned a stream")
            return pipePipe
        }

        Log.i("PT2Music", "Resolving $videoId with BravePipe")
        val bravePipe = withContext(Dispatchers.IO) {
            runCatching { NewPipeExtractor.newPipePlayer(videoId).firstOrNull()?.second }
                .onFailure { Log.e("PT2Music", "BravePipe failed", it) }
                .getOrNull()
        }
        if (bravePipe != null && validateStream(bravePipe, "BravePipe")) {
            Log.i("PT2Music", "BravePipe returned a stream")
            return bravePipe
        }

        Log.w("PT2Music", "Extractors failed; trying Innertube clients")
        val fallbackClients = listOf(
            YouTubeClient.VISIONOS,
            YouTubeClient.WEB_CREATOR,
            YouTubeClient.TVHTML5,
            YouTubeClient.ANDROID_VR_NO_AUTH,
            YouTubeClient.WEB,
        )
        for (client in fallbackClients) {
            val result = YouTube.player(videoId = videoId, client = client)
            val response = result.getOrNull()
            if (response == null) {
                Log.w("PT2Music", "${client.friendlyName ?: client.clientName} request failed", result.exceptionOrNull())
                continue
            }
            Log.i(
                "PT2Music",
                "${client.friendlyName ?: client.clientName}: ${response.playabilityStatus.status} " +
                    "${response.playabilityStatus.reason.orEmpty()}",
            )
            // getStreamUrl deciphers the signature (for cipher-protected formats) and,
            // critically, descrambles the throttling "n" parameter. Without the latter
            // YouTube's CDN caps the transfer to a crawl and playback dies as soon as
            // the initial buffer (roughly 30-60 seconds) runs out.
            val audioFormats = response.streamingData?.adaptiveFormats
                ?.asSequence()
                ?.filter { it.isAudio }
                ?.sortedByDescending { it.bitrate }
                ?.toList()
                .orEmpty()
            val ordered = if (maxBitrateKbps != null) {
                val underCeiling = audioFormats.filter { it.bitrate <= maxBitrateKbps * 1000 }
                // Already sorted descending, so the front of underCeiling is the best
                // qualifying format; fall back to the full (highest-first) list when
                // every format exceeds the ceiling.
                underCeiling.ifEmpty { audioFormats }
            } else {
                audioFormats
            }
            val url = ordered
                .asSequence()
                .firstNotNullOfOrNull { format ->
                    withContext(Dispatchers.IO) { NewPipeExtractor.getStreamUrl(format, videoId) }
                }
            if (url != null && validateStream(url, client.friendlyName ?: client.clientName)) {
                Log.i("PT2Music", "${client.friendlyName ?: client.clientName} returned a stream")
                return url
            }
        }
        Log.e("PT2Music", "No resolver returned a playable stream for $videoId")
        return null
    }

    private suspend fun validateStream(url: String, source: String): Boolean = withContext(Dispatchers.IO) {
        runCatching {
            streamValidator.newCall(
                Request.Builder()
                    .url(url)
                    .header("Range", "bytes=0-1")
                    .header("User-Agent", "Mozilla/5.0 (Linux; Android 14) AppleWebKit/537.36 Chrome/131 Mobile Safari/537.36")
                    .get()
                    .build(),
            ).execute().use { response ->
                val valid = response.isSuccessful || response.code == 206
                Log.i("PT2Music", "$source stream check HTTP ${response.code}")
                valid
            }
        }.onFailure {
            Log.e("PT2Music", "$source stream check failed", it)
        }.getOrDefault(false)
    }

    private suspend fun stopPlayback() {
        // Capture the resume point before stopping resets the player position.
        recordResumePosition()
        player.stop()
        stopTransport()
        resolvingPlayback = false
        activeVideoId = null
        activeTitle = null
        activeArtist = null
        deliveredCover = null
        transport.sendCoverArtClear(activeGeneration)
        publishState()
    }

    private suspend fun pausePlayback(generation: Int) {
        if (generation != activeGeneration) return
        player.pause()
    }

    private suspend fun seekTo(positionMs: Long, generation: Int?) {
        if (generation != null && generation != activeGeneration) return
        if (player.playbackState == Player.STATE_IDLE) return
        val clamped = if (player.duration > 0) {
            positionMs.coerceIn(0, player.duration)
        } else {
            positionMs.coerceAtLeast(0)
        }
        player.seekTo(clamped)
        // Echo the new position back so the watch progress bar stays in sync.
        transport.sendPlaybackPosition(clamped, activeGeneration)
    }

    private suspend fun resumePlayback(generation: Int) {
        if (player.playbackState == Player.STATE_IDLE || player.playbackState == Player.STATE_ENDED) {
            transport.sendEvent(Protocol.eventError, "Playback cannot be resumed", generation)
            return
        }
        activeGeneration = generation
        if (phoneAudio) {
            stopTransport()
        } else {
            startTransport(generation)
        }
        player.play()
    }

    private suspend fun toggleLoop() {
        loopMode = when (loopMode) {
            LoopModeOff -> LoopModeOne
            LoopModeOne -> LoopModeAll
            else -> LoopModeOff
        }
        // Recapture the loop order from the current (recency) display order the next
        // time it's needed, so entering Loop All reflects the list as it looks now.
        loopAllOrder = emptyList()
        getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
            .edit()
            .putInt(KEY_LOOP_MODE, loopMode)
            .apply()
        player.repeatMode = if (loopMode == LoopModeOne) {
            Player.REPEAT_MODE_ONE
        } else {
            Player.REPEAT_MODE_OFF
        }
        sendStateSnapshot()
    }

    /**
     * Cached video IDs in the same order the "Cached Music" library list displays
     * them: most-recently-played first (per playback history), then any remaining
     * cached tracks not in that history, in whatever order the cache reports them.
     * loopAllPool() must reuse this exactly - it used to independently sort IDs
     * alphabetically, which bore no relation to the displayed (recency) order, so
     * Loop All appeared to play in a shuffled/wrong order relative to the list.
     */
    private fun cachedTrackIds(): List<String> {
        val preferences = getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
        val history = runCatching { JSONArray(preferences.getString(KEY_HISTORY, "[]") ?: "[]") }
            .getOrElse { JSONArray() }
        val cachedIds = mediaCache.keys.asSequence().filter(::isFullyCached).toMutableSet()
        return buildList {
            val emitted = mutableSetOf<String>()
            for (index in 0 until history.length()) {
                val videoId = history.optJSONObject(index)?.optString("videoId").orEmpty()
                if (videoId.isBlank() || videoId !in cachedIds || !emitted.add(videoId)) continue
                add(videoId)
            }
            for (videoId in cachedIds) {
                if (!emitted.add(videoId)) continue
                add(videoId)
            }
        }
    }

    private fun loopAllPool(): MutableList<String> {
        if (radioQueueActive) return radioQueueSongs.toMutableList()
        // Reuse the stable snapshot as long as it still covers exactly the currently
        // cached set; only rebuild (recapturing display/recency order) when tracks are
        // added or evicted. Rebuilding every advance would re-sort by recency, which
        // each play mutates - see loopAllOrder's declaration.
        val cachedNow = cachedTrackIds()
        if (loopAllOrder.toSet() != cachedNow.toSet()) {
            loopAllOrder = cachedNow
        }
        val pool = loopAllOrder.toMutableList()
        val current = activeVideoId
        if (pool.isEmpty() && current != null) pool.add(current)
        return pool
    }

    private fun nextLoopAllVideoId(): String? {
        val current = activeVideoId
        val pool = loopAllPool()
        if (pool.isEmpty()) return null
        if (current == null) return pool.first()
        val index = pool.indexOf(current)
        return if (index < 0) pool.first() else pool[(index + 1) % pool.size]
    }

    /**
     * Shuffle counterpart to nextLoopAllVideoId(), for auto-advancing when a track ends
     * on its own rather than via a manual Next press (see the STATE_ENDED listener).
     * Without this, a shuffled session (including Artist Radio, which turns shuffle on
     * rather than loop-all) simply stopped when a track finished playing through -
     * skipInShuffle() only ran on an explicit Next/Previous button press.
     */
    private fun nextShuffleVideoId(): String? {
        if (shuffleOrder.isEmpty()) rebuildShuffleOrder()
        if (shuffleOrder.isEmpty()) return null
        val current = activeVideoId
        val index = current?.let(shuffleOrder::indexOf) ?: -1
        return if (index < 0) shuffleOrder.first() else shuffleOrder[(index + 1) % shuffleOrder.size]
    }

    /**
     * The pool Next/Previous currently draw from, in play order - mirrors skipTrack()'s
     * own precedence (loop all, then shuffle, else there's no defined "next") so the
     * Queue screen shows exactly what pressing Next would actually do. Falls back to
     * just the current song when neither is active, same as Next/Previous erroring out
     * in that state.
     */
    private fun currentQueueList(): List<String> = when {
        loopMode == LoopModeAll -> loopAllPool()
        shuffleEnabled -> {
            if (shuffleOrder.isEmpty()) rebuildShuffleOrder()
            shuffleOrder
        }
        else -> activeVideoId?.let { listOf(it) } ?: emptyList()
    }

    /** Sends the current queue (current song first, then upcoming in play order). */
    private suspend fun sendQueue() {
        val pool = currentQueueList()
        if (pool.isEmpty()) {
            transport.sendQueueComplete()
            return
        }
        val current = activeVideoId
        val startIndex = current?.let(pool::indexOf)?.takeIf { it >= 0 } ?: 0
        val ordered = (pool.subList(startIndex, pool.size) + pool.subList(0, startIndex))
            .take(MAX_QUEUE_ITEMS)
        fun metadataFor(videoId: String): Pair<String, String> =
            searchMetadata[videoId] ?: storedMetadataFor(videoId) ?: ("Unknown title" to "")
        ordered.forEachIndexed { index, videoId ->
            val (title, artist) = metadataFor(videoId)
            transport.sendQueueItem(index, videoId, title, artist)
        }
        transport.sendQueueComplete()
    }

    /**
     * Plays a track picked from the Queue screen. Unlike the direct-play branch of the
     * commandPlay handler, this does not end an active radio session or rebuild
     * shuffleOrder - it is a jump within the same queue. play() sets activeVideoId,
     * which is all skipInShuffle/skipInLoopAll need to re-derive position on the
     * following Next/Previous, same as after a hardware skip.
     */
    private suspend fun jumpQueue(videoId: String, generation: Int) {
        if (videoId !in currentQueueList()) {
            transport.sendEvent(Protocol.eventError, "Song no longer in queue", generation)
            return
        }
        play(videoId, generation)
    }

    private suspend fun skipTrack(previous: Boolean, generation: Int) {
        if (loopMode == LoopModeAll) {
            skipInLoopAll(previous = previous, generation = generation)
            return
        }
        if (shuffleEnabled) {
            skipInShuffle(previous = previous, generation = generation)
            return
        }
        transport.sendEvent(Protocol.eventError, "Enable shuffle or loop all", generation)
    }

    private suspend fun skipInLoopAll(previous: Boolean, generation: Int) {
        val pool = loopAllPool()
        if (pool.isEmpty()) {
            transport.sendEvent(Protocol.eventError, "No cached songs to loop", generation)
            return
        }
        val current = activeVideoId
        val index = current?.let(pool::indexOf) ?: -1
        if (index < 0) {
            play(if (previous) pool.last() else pool.first(), generation)
            return
        }
        val nextIndex = if (previous) {
            (index + pool.size - 1) % pool.size
        } else {
            (index + 1) % pool.size
        }
        play(pool[nextIndex], generation)
    }

    private suspend fun toggleShuffle(generation: Int) {
        shuffleEnabled = !shuffleEnabled
        if (shuffleEnabled) {
            rebuildShuffleOrder()
            if (shuffleOrder.isEmpty()) {
                shuffleEnabled = false
            }
        } else {
            shuffleOrder.clear()
            shuffleIndex = -1
            // Shuffle off is how a user leaves an active radio session; fall back to
            // the normal cache-based pool for any subsequent shuffle/loop-all.
            radioQueueActive = false
            radioQueueName = null
            radioQueueSongs = emptyList()
        }
        activeGeneration = generation
        sendStateSnapshot()
    }

    private fun rebuildShuffleOrder() {
        if (radioQueueActive) {
            val current = activeVideoId
            val randomized = radioQueueSongs.shuffled()
            shuffleOrder.clear()
            shuffleOrder.addAll(randomized)
            shuffleIndex = current?.let { shuffleOrder.indexOf(it) }?.takeIf { it >= 0 } ?: 0
            return
        }
        val cached = mediaCache.keys.asSequence()
            .filter(::isFullyCached)
            .distinct()
            .toMutableList()
        val current = activeVideoId
        if (current != null && current !in cached) cached.add(current)
        if (cached.isEmpty()) {
            shuffleOrder.clear()
            shuffleIndex = -1
            return
        }
        val randomized = cached.shuffled()
        shuffleOrder.clear()
        shuffleOrder.addAll(randomized)
        shuffleIndex = current?.let { shuffleOrder.indexOf(it) } ?: 0
        if (shuffleIndex < 0) shuffleIndex = 0
    }

    private suspend fun skipInShuffle(previous: Boolean, generation: Int) {
        if (!shuffleEnabled) {
            transport.sendEvent(Protocol.eventError, "Enable shuffle first", generation)
            return
        }
        if (shuffleOrder.isEmpty()) rebuildShuffleOrder()
        if (shuffleOrder.isEmpty()) {
            transport.sendEvent(Protocol.eventError, "No cached songs to shuffle", generation)
            return
        }
        val current = activeVideoId
        if (current != null) {
            val at = shuffleOrder.indexOf(current)
            if (at >= 0) {
                shuffleIndex = at
            } else {
                shuffleOrder.add(current)
                shuffleIndex = shuffleOrder.lastIndex
            }
        }
        if (shuffleIndex < 0) shuffleIndex = 0
        val size = shuffleOrder.size
        shuffleIndex = if (previous) {
            (shuffleIndex + size - 1) % size
        } else {
            (shuffleIndex + 1) % size
        }
        play(shuffleOrder[shuffleIndex], generation)
    }

    /**
     * Computes the current playback state and publishes it to [_uiState] for the phone UI.
     * Does NOT send anything to the watch â€” callers that need watch traffic use
     * [sendStateSnapshot] which delegates to this first.
     */
    private fun publishState() {
        val playbackState = when {
            resolvingPlayback || player.playbackState == Player.STATE_BUFFERING -> PlaybackUiState.PlaybackState.BUFFERING
            player.isPlaying -> PlaybackUiState.PlaybackState.PLAYING
            activeVideoId != null && player.playbackState != Player.STATE_IDLE -> PlaybackUiState.PlaybackState.PAUSED
            else -> PlaybackUiState.PlaybackState.IDLE
        }
        if (phoneAudio) phoneVolume = systemMediaVolumePercent()
        _uiState.value = PlaybackUiState(
            playbackState = playbackState,
            generation = activeGeneration,
            videoId = activeVideoId,
            title = activeTitle,
            artist = activeArtist,
            durationMs = player.duration.coerceAtLeast(0),
            positionMs = player.currentPosition.coerceAtLeast(0),
            phoneAudio = phoneAudio,
            volume = phoneVolume,
            loopEnabled = player.repeatMode == Player.REPEAT_MODE_ONE,
            loopMode = loopMode,
            shuffleEnabled = shuffleEnabled,
            isFavorite = activeVideoId?.let(::isFavorite) ?: false,
            theme = watchTheme,
            queueName = if (radioQueueActive) radioQueueName else null,
            queueSize = if (radioQueueActive) radioQueueSongs.size else 0,
        )
    }

    private suspend fun sendStateSnapshot() {
        publishState()
        val state = _uiState.value
        transport.sendStateSnapshot(
            playbackState = when (state.playbackState) {
                PlaybackUiState.PlaybackState.BUFFERING -> Protocol.playbackBuffering
                PlaybackUiState.PlaybackState.PLAYING -> Protocol.playbackPlaying
                PlaybackUiState.PlaybackState.PAUSED -> Protocol.playbackPaused
                else -> Protocol.playbackIdle
            },
            generation = state.generation,
            videoId = state.videoId,
            title = state.title,
            artist = state.artist,
            durationMs = state.durationMs,
            positionMs = state.positionMs,
            phoneAudio = state.phoneAudio,
            volume = state.volume,
            loopEnabled = state.loopEnabled,
            loopMode = state.loopMode,
            shuffleEnabled = state.shuffleEnabled,
            isFavorite = state.isFavorite,
            theme = state.theme,
            routeEpoch = routeEpoch,
        )
    }

    /**
     * Prefers YT Music's own square album-art thumbnail over the video's thumbnail: video
     * thumbnails are 16:9 and, for official-audio uploads, are frequently just the square
     * cover art letterboxed onto a black canvas - the source of the black bars sent to the
     * watch. The queue endpoint resolves cover art straight from the videoId without needing
     * a prior search. Falls back to the video thumbnail for tracks YT Music doesn't recognize
     * (e.g. a PipePipe-only match).
     */
    private suspend fun resolveCoverArtUrl(
        videoId: String,
        sourceSize: Int = COVER_ART_SOURCE_SIZE,
    ): String? {
        val squareArt = runCatching {
            YouTube.queue(videoIds = listOf(videoId)).getOrNull()?.firstOrNull()?.thumbnail
        }.getOrNull()
        if (!squareArt.isNullOrBlank()) return squareArt.asSquareThumbnail(sourceSize)
        return runCatching {
            YouTube.player(videoId = videoId, client = YouTubeClient.WEB)
                .getOrNull()
                ?.videoDetails
                ?.thumbnail
                ?.thumbnails
                ?.maxByOrNull { (it.width ?: 0) * (it.height ?: 0) }
                ?.url
        }.getOrNull()
    }

    /**
     * Requests a square crop at [size] from Google's image CDN so later resizing never has
     * to letterbox. Uses -l100 (max JPEG quality, near-lossless) rather than the app's usual
     * -l90 so the source we downscale from carries as little compression damage as possible -
     * the whole point is to feed the watch encoder a clean, high-quality image.
     */
    private fun String.asSquareThumbnail(size: Int): String {
        if (!SQUARE_THUMBNAIL_HOST_PATTERN.matches(this)) return this
        return "${split("=w")[0]}=w$size-h$size-p-l100-rj"
    }

    /**
     * Identifies one cover transfer. Everything that changes the bytes on the wire is
     * in here: the track, the dimension (which follows the audio route), the encoding
     * (mono vs colour, which follows the theme) and the generation the watch is on.
     */
    private data class CoverTransfer(
        val videoId: String,
        val dim: Int,
        val mono: Boolean,
        val generation: Int,
    )

    private var inFlightCover: CoverTransfer? = null
    private var deliveredCover: CoverTransfer? = null

    /**
     * Sends the watch a cover, unless it is already sending or has already sent that
     * exact one.
     *
     * The guard is not an optimisation. The watch re-syncs its whole settings blob
     * (`commandSyncSettings`) on its own schedule, and `applySyncedSettings` ends by
     * pushing the cover again â€” so a track starting would fire this two or three times
     * inside half a second. Each call used to `cancel()` the previous job, and since a
     * 144x144 colour payload is 20736 bytes going out in 512-byte chunks, the transfer
     * was being killed and restarted from chunk zero long before it could finish. The
     * watch received a truncated stream every time and had nothing to draw, which is
     * why art only appeared after a pause/play: that is the one path where a single
     * uncontested send gets to run to completion.
     */
    private fun maybeSendCoverArt(videoId: String, generation: Int) {
        // Derived synchronously, because the decision to leave a running transfer
        // alone has to be made before anything is cancelled.
        val dim = if (phoneAudio) COVER_ART_HIGH_DIM else COVER_ART_LOW_DIM
        // Cover art is sent in colour for every theme, Mono included. The Mono theme
        // styles the watch's *UI* black-and-white; the artwork is the one element that
        // is meant to be the record sleeve, and 1bpp dithering made it unrecognisable.
        // The watch still accepts mono payloads, so this is purely which one we send.
        val mono = false
        val transfer = CoverTransfer(videoId, dim, mono, generation)
        if (coverArtBackground && (transfer == inFlightCover || transfer == deliveredCover)) {
            // A redundant re-sync. Release the play() hold in case this call came from
            // one â€” releaseAudioForCover just clears a flag, so it is safe either way.
            transport.releaseAudioForCover()
            return
        }

        coverArtJob?.cancel()
        inFlightCover = if (coverArtBackground) transfer else null
        coverArtJob = scope.launch {
            // Always release the pre-playback audio hold set by play() for the watch route,
            // whatever path we exit by (no art, fetch failure, or a completed transfer).
            try {
                if (!coverArtBackground) {
                    deliveredCover = null
                    transport.sendCoverArtClear(generation)
                    return@launch
                }
                // Small cover while streaming to the watch (BLE busy with audio), sharp on phone.
                val cached = withContext(Dispatchers.IO) { readCachedCover(videoId, dim, mono) }
                if (cached != null) {
                    Log.i(
                        "PT2Music",
                        "Cover art cache hit: bytes=${cached.size}, dim=$dim, theme=$watchTheme, video=$videoId",
                    )
                    if (sendCoverArtPayload(cached, dim, generation)) deliveredCover = transfer
                    return@launch
                }
                // The watch asks for a much smaller source than the phone UI does.
                // COVER_ART_SOURCE_SIZE exists to give Coil a sharp image on a 1080p
                // screen; feeding that same 720px JPEG into the watch pipeline means
                // decoding it, re-encoding it as a multi-megabyte PNG and handing that
                // across the gomobile boundary, all to produce 64 pixels. Asking the
                // CDN for a modest square instead keeps the oversampling the encoder
                // wants without the buffer that starves it.
                val artUrl = resolveCoverArtUrl(videoId, COVER_ART_WATCH_SOURCE_SIZE)
                if (artUrl.isNullOrBlank()) {
                    transport.sendCoverArtClear(generation)
                    return@launch
                }
                val raw = loadCoverBytes(artUrl)
                val watchimagePayload = if (raw != null) {
                    encodeCoverArtWithWatchimage(raw, dim, mono)
                } else {
                    null
                }
                if (watchimagePayload != null && isValidCoverPayload(watchimagePayload, dim)) {
                    Log.i(
                        "PT2Music",
                        "Cover art payload ready: bytes=${watchimagePayload.size}, dim=$dim, theme=$watchTheme, video=$videoId",
                    )
                    withContext(Dispatchers.IO) { writeCachedCover(videoId, dim, mono, watchimagePayload) }
                    if (sendCoverArtPayload(watchimagePayload, dim, generation)) {
                        deliveredCover = transfer
                    }
                    return@launch
                }
                deliveredCover = null
                transport.sendCoverArtClear(generation)
            } finally {
                // Only stand down as the in-flight transfer if this job is still the
                // current one â€” a newer call may already have claimed the slot.
                if (inFlightCover == transfer) inFlightCover = null
                transport.releaseAudioForCover()
            }
        }
    }

    /**
     * Streams an encoded cover payload to the watch in [COVER_ART_CHUNK_BYTES] chunks.
     * Returns whether every chunk went out â€” a partial stream leaves the watch with
     * nothing to draw, so it must not be recorded as delivered.
     */
    private suspend fun sendCoverArtPayload(payload: ByteArray, dim: Int, generation: Int): Boolean {
        transport.sendCoverArtStart(dim, dim, payload.size, generation)
        var sequence = 0
        var offset = 0
        while (offset < payload.size) {
            val end = minOf(offset + COVER_ART_CHUNK_BYTES, payload.size)
            val ok = transport.sendCoverArtChunk(payload.copyOfRange(offset, end), sequence, generation)
            if (!ok) return false
            offset = end
            sequence++
        }
        return true
    }

    /** The two payload shapes the watch accepts: 1bpp mono, or one byte per pixel in colour. */
    private fun isValidCoverPayload(payload: ByteArray, dim: Int): Boolean =
        payload.size == (dim / 8) * dim || payload.size == dim * dim

    /**
     * Cover art is cached on disk as the *encoded watch payload*, the same idea as the
     * media cache for audio: a bounded, LRU-evicted directory under filesDir. The file
     * name carries the three things a payload depends on - the track, the dimension
     * (which follows the audio route) and whether the encoder ran in mono - so a hit is
     * always byte-for-byte what re-encoding would have produced.
     *
     * A hit skips the YT Music lookup, the 720x720 download and the native encode, which
     * is what turns replaying a known track from "watch audio held while the cover is
     * fetched" into an immediate transfer. Unlike the audio cache this is deliberately
     * not gated on "Cache music" / "Cache radio": a payload is at most 20 KB, so the
     * entire cover cache is smaller than a single cached track, and those settings exist
     * to bound megabytes of audio.
     */
    private fun coverCacheFile(videoId: String, dim: Int, mono: Boolean): File {
        val safeId = videoId.replace(COVER_CACHE_UNSAFE_CHARS, "_")
        return File(coverCacheDir, "$safeId-$dim-${if (mono) "m" else "c"}.bin")
    }

    private fun readCachedCover(videoId: String, dim: Int, mono: Boolean): ByteArray? {
        val file = coverCacheFile(videoId, dim, mono)
        val bytes = runCatching { file.takeIf { it.isFile }?.readBytes() }.getOrNull() ?: return null
        // A truncated write (killed mid-save) would otherwise be sent to the watch forever.
        if (!isValidCoverPayload(bytes, dim)) {
            file.delete()
            return null
        }
        // Recency is the file's mtime, so touch it to keep the pruner honest.
        file.setLastModified(System.currentTimeMillis())
        return bytes
    }

    private fun writeCachedCover(videoId: String, dim: Int, mono: Boolean, payload: ByteArray) {
        runCatching {
            coverCacheDir.mkdirs()
            coverCacheFile(videoId, dim, mono).writeBytes(payload)
            pruneCoverCache()
        }.onFailure { Log.w("PT2Music", "Cover art cache write failed for $videoId", it) }
    }

    /** Drops least-recently-used payloads until the directory is back under its budget. */
    private fun pruneCoverCache() {
        val files = coverCacheDir.listFiles() ?: return
        var total = files.sumOf { it.length() }
        if (total <= COVER_CACHE_MAX_BYTES) return
        for (file in files.sortedBy { it.lastModified() }) {
            if (total <= COVER_CACHE_MAX_BYTES) break
            val size = file.length()
            if (file.delete()) total -= size
        }
    }

    private suspend fun loadCoverBytes(url: String): ByteArray? = withContext(Dispatchers.IO) {
        runCatching {
            streamValidator.newCall(
                Request.Builder()
                    .url(url)
                    .header("User-Agent", "Mozilla/5.0 (Linux; Android 14) AppleWebKit/537.36 Chrome/131 Mobile Safari/537.36")
                    .get()
                    .build(),
            ).execute().use { response ->
                if (!response.isSuccessful) return@use null
                response.body?.bytes()
            }
        }.getOrNull()
    }

    private suspend fun encodeCoverArtWithWatchimage(
        raw: ByteArray,
        dim: Int,
        mono: Boolean,
    ): ByteArray? = withContext(Dispatchers.Default) {
        val normalized = normalizeImageForWatchimage(raw, maxDim = dim * 4)
        val candidates = if (normalized != null && !normalized.contentEquals(raw)) {
            listOf(normalized, raw)
        } else {
            listOf(raw)
        }
        for (candidate in candidates) {
            val encoded = runCatching {
                if (mono) {
                    Watchimagebridge.encodeCoverMonoBytes(candidate, dim.toLong(), dim.toLong())
                } else {
                    Watchimagebridge.encodeCoverColorBytes(candidate, dim.toLong(), dim.toLong())
                }
            }.onFailure {
                Log.w("PT2Music", "watchimage encode failed", it)
            }.getOrNull()
            if (encoded != null) return@withContext encoded
        }
        null
    }

    /**
     * Re-encodes [raw] as a PNG the watchimage bridge can definitely read, capped at
     * [maxDim] on the long edge.
     *
     * The cap is the load-bearing part. This PNG is lossless, so its size scales with
     * pixel count: a 720x720 source normalises to well over a megabyte, which then has
     * to cross the gomobile boundary and be decoded again inside the Go runtime â€” for
     * an image that is about to be reduced to 64 pixels. Bounding it to a few times the
     * target keeps every downstream step cheap and never upscales a source that is
     * already small.
     */
    private fun normalizeImageForWatchimage(raw: ByteArray, maxDim: Int): ByteArray? {
        val bitmap = BitmapFactory.decodeByteArray(raw, 0, raw.size) ?: return null
        val longEdge = maxOf(bitmap.width, bitmap.height)
        val scaled = if (longEdge > maxDim && longEdge > 0) {
            val scale = maxDim.toFloat() / longEdge
            android.graphics.Bitmap.createScaledBitmap(
                bitmap,
                (bitmap.width * scale).toInt().coerceAtLeast(1),
                (bitmap.height * scale).toInt().coerceAtLeast(1),
                true,
            )
        } else {
            bitmap
        }
        return runCatching {
            java.io.ByteArrayOutputStream().use { out ->
                scaled.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, out)
                out.toByteArray()
            }
        }.getOrNull().also {
            if (scaled !== bitmap) scaled.recycle()
            bitmap.recycle()
        }
    }

    private fun recordPlayback() {
        if (lastRecordedGeneration == activeGeneration || activeVideoId == null) return
        val videoId = activeVideoId ?: return
        val preferences = getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
        val metadata = runCatching { JSONObject(preferences.getString(KEY_METADATA, "{}") ?: "{}") }
            .getOrElse { JSONObject() }
        // Never downgrade a previously stored real title to the videoId placeholder;
        // that would permanently corrupt the Recently Played / Cached Music lists.
        val storedTitle = metadata.optJSONObject(videoId)?.optString("title")
            ?.takeIf { it.isNotBlank() && it != videoId }
        val storedArtist = metadata.optJSONObject(videoId)?.optString("artist")
        val resolvedTitle = activeTitle ?: storedTitle
        val title = resolvedTitle ?: videoId
        val artist = activeArtist ?: storedArtist.orEmpty()
        val history = runCatching { JSONArray(preferences.getString(KEY_HISTORY, "[]") ?: "[]") }
            .getOrElse { JSONArray() }
        val updated = JSONArray().apply {
            put(JSONObject().put("videoId", videoId).put("title", title).put("artist", artist))
            var added = 1
            for (index in 0 until history.length()) {
                if (added >= HISTORY_LIMIT) break
                val previous = history.optJSONObject(index) ?: continue
                if (previous.optString("videoId") == videoId) continue
                put(previous)
                added++
            }
        }
        // Only persist a title into the durable metadata store when one was actually
        // resolved - writing the videoId placeholder here would permanently poison
        // every future lookup (Cached Music etc.) for this track, since there'd be
        // no way to tell "never resolved" apart from "genuinely titled this".
        if (resolvedTitle != null) {
            metadata.put(videoId, JSONObject().put("title", resolvedTitle).put("artist", artist))
        }
        preferences.edit()
            .putString(KEY_HISTORY, updated.toString())
            .putString(KEY_METADATA, metadata.toString())
            .apply()
        lastRecordedGeneration = activeGeneration
    }

    /** Persisted title/artist for a track, or null if none (or only a placeholder) is stored. */
    private fun storedMetadataFor(videoId: String): Pair<String, String>? {
        val preferences = getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
        val metadata = runCatching { JSONObject(preferences.getString(KEY_METADATA, "{}") ?: "{}") }
            .getOrElse { JSONObject() }
        val item = metadata.optJSONObject(videoId) ?: return null
        val title = item.optString("title")
        if (title.isBlank() || title == videoId) return null
        return title to item.optString("artist")
    }

    /**
     * Records a search query in the newest-first recent-searches list, de-duplicating
     * (case-insensitively) and capping stored history at RECENT_SEARCHES_STORE so the
     * user-configurable display length can grow later without losing entries.
     */
    private fun recordRecentSearch(rawQuery: String) {
        val query = rawQuery.trim()
        if (query.isEmpty()) return
        val preferences = getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
        val existing = runCatching { JSONArray(preferences.getString(KEY_RECENT_SEARCHES, "[]") ?: "[]") }
            .getOrElse { JSONArray() }
        val updated = JSONArray().apply {
            put(query)
            var added = 1
            for (index in 0 until existing.length()) {
                if (added >= RECENT_SEARCHES_STORE) break
                val previous = existing.optString(index)
                if (previous.isBlank() || previous.equals(query, ignoreCase = true)) continue
                put(previous)
                added++
            }
        }
        preferences.edit().putString(KEY_RECENT_SEARCHES, updated.toString()).apply()
    }

    /**
     * Records the current track's resume position for the Continue Listening list. Only
     * stores tracks that are meaningfully in-progress (past the start, not near the end).
     * The entry is upserted to the front (newest-first) and the list is capped.
     */
    private fun recordResumePosition() {
        val videoId = activeVideoId ?: return
        val positionMs = player.currentPosition
        val durationMs = player.duration
        if (positionMs < RESUME_MIN_POSITION_MS) return
        if (durationMs > 0 && positionMs > durationMs - RESUME_END_MARGIN_MS) {
            clearResumePosition(videoId)
            return
        }
        val preferences = getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
        val existing = runCatching { JSONArray(preferences.getString(KEY_RESUME, "[]") ?: "[]") }
            .getOrElse { JSONArray() }
        val updated = JSONArray().apply {
            put(
                JSONObject()
                    .put("videoId", videoId)
                    .put("title", activeTitle ?: videoId)
                    .put("artist", activeArtist.orEmpty())
                    .put("positionMs", positionMs)
                    .put("durationMs", durationMs.coerceAtLeast(0)),
            )
            var added = 1
            for (index in 0 until existing.length()) {
                if (added >= RESUME_LIMIT) break
                val previous = existing.optJSONObject(index) ?: continue
                if (previous.optString("videoId") == videoId) continue
                put(previous)
                added++
            }
        }
        preferences.edit().putString(KEY_RESUME, updated.toString()).apply()
    }

    /** Removes a track from the Continue Listening list (e.g. when it finishes). */
    private fun clearResumePosition(videoId: String) {
        val preferences = getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
        val existing = runCatching { JSONArray(preferences.getString(KEY_RESUME, "[]") ?: "[]") }
            .getOrElse { JSONArray() }
        val updated = JSONArray()
        for (index in 0 until existing.length()) {
            val item = existing.optJSONObject(index) ?: continue
            if (item.optString("videoId") == videoId) continue
            updated.put(item)
        }
        preferences.edit().putString(KEY_RESUME, updated.toString()).apply()
    }

    /** Resume position (ms) stored for a track, or 0 if none. */
    private fun resumePositionFor(videoId: String): Long {
        val preferences = getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
        val existing = runCatching { JSONArray(preferences.getString(KEY_RESUME, "[]") ?: "[]") }
            .getOrElse { JSONArray() }
        for (index in 0 until existing.length()) {
            val item = existing.optJSONObject(index) ?: continue
            if (item.optString("videoId") == videoId) return item.optLong("positionMs", 0L)
        }
        return 0L
    }

    private fun favoritesSet(): MutableSet<String> {
        val preferences = getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
        val array = runCatching { JSONArray(preferences.getString(KEY_FAVORITES, "[]") ?: "[]") }
            .getOrElse { JSONArray() }
        return buildSet {
            for (i in 0 until array.length()) array.optString(i).takeIf { it.isNotBlank() }?.let(::add)
        }.toMutableSet()
    }

    private fun isFavorite(videoId: String): Boolean = videoId in favoritesSet()

    private suspend fun toggleFavorite(videoId: String) {
        val favorites = favoritesSet()
        val nowFavorite = if (videoId in favorites) {
            favorites.remove(videoId); false
        } else {
            favorites.add(videoId); true
        }
        getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
            .edit()
            .putString(KEY_FAVORITES, JSONArray(favorites.toList()).toString())
            .apply()
        // Confirm the new state back to the watch so its heart indicator matches.
        transport.sendFavoriteState(videoId, nowFavorite, activeGeneration)
        publishState()
    }

    // ---- Playlists ---------------------------------------------------------------
    // Phone-owned, watch read/play-only: playlists are created and edited here, and
    // the watch lists and plays them through Protocol.libraryPlaylists and the
    // "playlist:" id prefix. Single-owner edits avoid sync conflicts by construction.
    // Storage follows the same SharedPreferences+JSON pattern as history/favorites.

    private data class StoredPlaylist(
        val id: String,
        val name: String,
        val videoIds: MutableList<String>,
    )

    private fun readPlaylists(): List<StoredPlaylist> {
        val preferences = getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
        val array = runCatching { JSONArray(preferences.getString(KEY_PLAYLISTS, "[]") ?: "[]") }
            .getOrElse { JSONArray() }
        return buildList {
            for (i in 0 until array.length()) {
                val obj = array.optJSONObject(i) ?: continue
                val id = obj.optString("id")
                val name = obj.optString("name").ifBlank { "Playlist" }
                if (id.isBlank()) continue
                val ids = mutableListOf<String>()
                val songs = obj.optJSONArray("songs")
                if (songs != null) {
                    for (j in 0 until songs.length()) {
                        songs.optString(j).takeIf { it.isNotBlank() }?.let(ids::add)
                    }
                }
                add(StoredPlaylist(id, name, ids))
            }
        }
    }

    private fun writePlaylists(playlists: List<StoredPlaylist>) {
        val array = JSONArray()
        for (playlist in playlists) {
            array.put(
                JSONObject()
                    .put("id", playlist.id)
                    .put("name", playlist.name)
                    .put("songs", JSONArray(playlist.videoIds.toList())),
            )
        }
        getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
            .edit()
            .putString(KEY_PLAYLISTS, array.toString())
            .apply()
    }

    /** Durably stores a track's title/artist so playlist rows render real names later. */
    private fun persistTrackMetadata(videoId: String, title: String, artist: String) {
        val preferences = getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
        val metadata = runCatching { JSONObject(preferences.getString(KEY_METADATA, "{}") ?: "{}") }
            .getOrElse { JSONObject() }
        metadata.put(videoId, JSONObject().put("title", title).put("artist", artist))
        preferences.edit().putString(KEY_METADATA, metadata.toString()).apply()
    }

    suspend fun playlistsList(): List<PlaylistInfo> = withContext(Dispatchers.IO) {
        readPlaylists().map { PlaylistInfo(it.id, it.name, it.videoIds.size, it.videoIds.firstOrNull()) }
    }

    suspend fun playlistSongs(playlistId: String): List<SearchResultItem> = withContext(Dispatchers.IO) {
        val playlist = readPlaylists().firstOrNull { it.id == playlistId }
            ?: return@withContext emptyList()
        playlist.videoIds.map { videoId ->
            val meta = storedMetadataFor(videoId)
            SearchResultItem(
                videoId = videoId,
                title = meta?.first ?: "Unknown title",
                artist = meta?.second.orEmpty(),
            )
        }
    }

    suspend fun createPlaylist(name: String): String = withContext(Dispatchers.IO) {
        val id = "p${System.currentTimeMillis()}"
        val playlists = readPlaylists().toMutableList()
        playlists.add(
            StoredPlaylist(
                id = id,
                name = name.trim().ifBlank { "Playlist" },
                videoIds = mutableListOf(),
            ),
        )
        writePlaylists(playlists)
        id
    }

    suspend fun deletePlaylist(playlistId: String) = withContext(Dispatchers.IO) {
        writePlaylists(readPlaylists().filterNot { it.id != playlistId })
    }

    suspend fun addToPlaylist(playlistId: String, videoId: String) {
        // searchMetadata is only touched on the Main dispatcher; read it before the IO hop.
        val meta = searchMetadata[videoId]
        withContext(Dispatchers.IO) {
            if (meta != null && storedMetadataFor(videoId) == null) {
                persistTrackMetadata(videoId, meta.first, meta.second)
            }
            val playlists = readPlaylists().toMutableList()
            val playlist = playlists.firstOrNull { it.id == playlistId } ?: return@withContext
            if (videoId !in playlist.videoIds) {
                playlist.videoIds.add(videoId)
                writePlaylists(playlists)
            }
        }
    }

    suspend fun removeFromPlaylist(playlistId: String, videoId: String) = withContext(Dispatchers.IO) {
        val playlists = readPlaylists().toMutableList()
        val playlist = playlists.firstOrNull { it.id == playlistId } ?: return@withContext
        if (playlist.videoIds.remove(videoId)) writePlaylists(playlists)
    }

    /**
     * Plays a playlist as an ordered queue — the same shape as Song Radio: the id list
     * becomes the active queue (so Next/Previous walk it in order and Shuffle
     * randomizes it), starting at [startVideoId] or the first song.
     */
    private suspend fun playPlaylist(playlistId: String, generation: Int, startVideoId: String? = null) {
        val playlist = readPlaylists().firstOrNull { it.id == playlistId }
        if (playlist == null || playlist.videoIds.isEmpty()) {
            transport.sendEvent(Protocol.eventError, "Playlist is empty", generation)
            _errors.tryEmit("Playlist is empty")
            return
        }
        radioQueueActive = true
        radioQueueName = playlist.name
        radioQueueSongs = playlist.videoIds.toList()
        loopMode = LoopModeAll
        val start = startVideoId?.takeIf { it in playlist.videoIds } ?: playlist.videoIds.first()
        play(start, generation)
    }

    private suspend fun sendLibrary(libraryType: Int, requestedLimit: Int?) {
        val preferences = getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
        // The watch dictates how many entries it can hold. Recently Played honors the
        // user-configurable history limit; Cached Music returns every fully-cached song
        // (still bounded by the watch's hard cap for safety).
        val limit = (requestedLimit ?: LIBRARY_HARD_CAP).coerceIn(1, LIBRARY_HARD_CAP)

        // Recent searches are query strings, not tracks. Ship the query in every field
        // so the watch can display it and re-run the search when it is selected.
        if (libraryType == Protocol.libraryRecentSearches) {
            val recent = runCatching { JSONArray(preferences.getString(KEY_RECENT_SEARCHES, "[]") ?: "[]") }
                .getOrElse { JSONArray() }
            var index = 0
            var i = 0
            while (i < recent.length() && index < limit) {
                val query = recent.optString(i)
                i++
                if (query.isBlank()) continue
                transport.sendLibraryItem(libraryType, index, query, query, "")
                index++
            }
            transport.sendLibraryComplete(libraryType)
            return
        }

        val metadata = runCatching { JSONObject(preferences.getString(KEY_METADATA, "{}") ?: "{}") }
            .getOrElse { JSONObject() }
        fun metadataFor(videoId: String): Triple<String, String, String> {
            val item = metadata.optJSONObject(videoId)
            // Entries written before the recordPlayback() fix above may still have the
            // videoId itself stored as "title" (a bare video ID reads as gibberish on
            // the watch); show a friendly placeholder instead rather than the raw id.
            // This is a display-time fallback only - it's never written back to
            // storage, so a later real title still overwrites it normally.
            val storedTitle = item?.optString("title")?.takeIf { it.isNotBlank() && it != videoId }
            return Triple(
                videoId,
                storedTitle ?: "Unknown title",
                item?.optString("artist").orEmpty(),
            )
        }

        val tracks: List<Triple<String, String, String>> = when (libraryType) {
            Protocol.libraryRecent -> {
                val history = runCatching { JSONArray(preferences.getString(KEY_HISTORY, "[]") ?: "[]") }
                    .getOrElse { JSONArray() }
                buildList {
                    val seenVideoIds = mutableSetOf<String>()
                    for (index in 0 until history.length()) {
                        if (size >= limit) break
                        val item = history.optJSONObject(index) ?: continue
                        val videoId = item.optString("videoId")
                        if (videoId.isBlank() || !seenVideoIds.add(videoId)) continue
                        add(Triple(item.getString("videoId"), item.optString("title"), item.optString("artist")))
                    }
                }
            }

            Protocol.libraryFavorites -> {
                // Favorites are stored newest-first; use saved metadata for titles/artists.
                val favorites = runCatching {
                    JSONArray(preferences.getString(KEY_FAVORITES, "[]") ?: "[]")
                }.getOrElse { JSONArray() }
                buildList {
                    for (index in 0 until favorites.length()) {
                        if (size >= limit) break
                        val videoId = favorites.optString(index)
                        if (videoId.isBlank()) continue
                        add(metadataFor(videoId))
                    }
                }
            }

            Protocol.libraryContinue -> {
                // Unfinished tracks (paused/left partway) with a stored resume position,
                // newest-first.
                val resume = runCatching { JSONArray(preferences.getString(KEY_RESUME, "[]") ?: "[]") }
                    .getOrElse { JSONArray() }
                buildList {
                    for (index in 0 until resume.length()) {
                        if (size >= limit) break
                        val item = resume.optJSONObject(index) ?: continue
                        val videoId = item.optString("videoId")
                        if (videoId.isBlank()) continue
                        add(
                            Triple(
                                videoId,
                                item.optString("title").ifBlank { videoId },
                                item.optString("artist"),
                            ),
                        )
                    }
                }
            }

            Protocol.libraryPlaylists -> {
                // Playlist rows carry a synthetic "playlist:<id>" videoId; the watch
                // plays one by selecting it, like the artist-radio rows.
                readPlaylists().take(limit).map { playlist ->
                    Triple(
                        "$PLAYLIST_PREFIX${playlist.id}",
                        playlist.name,
                        if (playlist.videoIds.size == 1) "1 song" else "${playlist.videoIds.size} songs",
                    )
                }
            }

            else -> {  // Protocol.libraryCached and any unknown type.
                // Same order Loop All plays in - see cachedTrackIds().
                cachedTrackIds().take(limit).map(::metadataFor)
            }
        }
        tracks.forEachIndexed { index, (videoId, title, artist) ->
            searchMetadata[videoId] = title to artist
            transport.sendLibraryItem(libraryType, index, videoId, title, artist)
        }
        transport.sendLibraryComplete(libraryType)
    }

    private fun isFullyCached(videoId: String): Boolean {
        val contentLength = mediaCache.getContentMetadata(videoId).get(
            ContentMetadata.KEY_CONTENT_LENGTH,
            C.LENGTH_UNSET.toLong(),
        )
        return contentLength > 0 && mediaCache.isCached(videoId, 0, contentLength)
    }

    /**
     * Deletes a song's cached audio from disk (from the "Cached Music" library screen's
     * long-press-to-delete gesture). Only removes the cache entry - title/artist metadata
     * and history/favorites entries are left alone, since those are shared with
     * Recently Played/Favorites regardless of whether the audio is on disk. The watch
     * removes the row from its list optimistically, so no reply is sent back;
     * cachedTrackIds()/loopAllPool() simply stop seeing this id on their next read since
     * they derive the cached set live from mediaCache.keys.
     */
    private fun deleteCachedSong(videoId: String) {
        Log.i("PT2Music", "Deleting cached song: $videoId")
        runCatching { mediaCache.removeResource(videoId) }
            .onFailure { Log.w("PT2Music", "Failed to delete cached song: $videoId", it) }
    }

    /**
     * Removes every cached song except the one currently playing — its spans are in
     * use by the player, and pulling them mid-track would cut the audio off. Any
     * in-flight pre-download is cancelled first so a removed song is not written
     * straight back. History/favorites entries are kept: those mean "played", not
     * "on disk", and they are shared with the watch's library lists.
     */
    private fun clearCache() {
        cacheDownloadJob?.cancel()
        val keep = activeVideoId
        var removed = 0
        for (key in mediaCache.keys.toList()) {
            if (key == keep) continue
            runCatching { mediaCache.removeResource(key) }
                .onSuccess { removed++ }
                .onFailure { Log.w("PT2Music", "Failed to remove cached song: $key", it) }
        }
        Log.i("PT2Music", "Cache cleared: $removed songs removed (kept current: ${keep != null})")
    }

    private suspend fun startTransport(generation: Int) {
        if (transportGeneration == generation) return
        transport.start(generation)
        transportGeneration = generation
    }

    private suspend fun stopTransport() {
        if (transportGeneration == null) return
        transport.stop()
        transportGeneration = null
    }

    /**
     * The single audio-route application path. Every route change â€” watch command,
     * phone UI toggle, or a route arriving inside a settings sync â€” funnels through
     * here so the value can never be set without its side effects (player volume,
     * transport start/stop, notification refresh), which is exactly what used to make
     * route switching unreliable: `applySyncedSettings` flipped `phoneAudio` and left
     * the audio path on the old route.
     *
     * [epoch] orders route changes across the two devices. A message carrying an older
     * epoch than the last one applied is a stale echo (e.g. a snapshot that was in
     * flight while the user toggled the route): it is not applied, and our state is
     * re-sent so the other side converges on the newer value. An equal epoch carrying a
     * *different* route means both sides changed simultaneously â€” the watch wins ties.
     * A null epoch (old firmware) is always applied, the pre-epoch behavior.
     *
     * Always ends with a state snapshot to the watch: a route change used to be applied
     * silently, leaving the watch showing its optimistic toggle regardless of whether
     * the switch actually took.
     */
    private suspend fun setAudioRoute(route: Int, generation: Int, epoch: Int? = null) {
        val wantsPhone = route == Protocol.audioRoutePhone
        if (epoch != null && epoch < routeEpoch) {
            Log.i("PT2Music", "Ignoring stale route (epoch $epoch < $routeEpoch)")
            sendStateSnapshot()
            return
        }
        if (epoch != null && epoch > routeEpoch) {
            routeEpoch = epoch
            getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
                .edit()
                .putInt(KEY_ROUTE_EPOCH, routeEpoch)
                .apply()
        }
        val changed = wantsPhone != phoneAudio
        phoneAudio = wantsPhone
        getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
            .edit()
            .putBoolean(KEY_PHONE_AUDIO, phoneAudio)
            .apply()
        if (changed) {
            // On the phone route the OS media stream sets loudness so the player runs at
            // full volume; on the watch route the phone is silenced and audio is streamed.
            player.volume = if (phoneAudio) 1f else 0f
            if (phoneAudio) {
                stopTransport()
                // Reflect the current system media volume back to the watch.
                phoneVolume = systemMediaVolumePercent()
            } else if (
                player.playWhenReady &&
                player.playbackState != Player.STATE_IDLE &&
                player.playbackState != Player.STATE_ENDED
            ) {
                activeGeneration = generation
                startTransport(generation)
            }
            // Notification visibility depends on the route, so refresh it.
            refreshNotification()
        }
        // sendStateSnapshot publishes locally first, so both ends are covered.
        sendStateSnapshot()
        // Deliberately do NOT re-send cover art on a route change: clearing the art that's
        // already showing and re-sending during the live audio stream fails and leaves the
        // screen blank. The existing art stays; the route-appropriate size is chosen on the
        // next track change instead.
    }

    /** Current Android media (STREAM_MUSIC) volume as a 0-100 percentage. */
    private fun systemMediaVolumePercent(): Int {
        val max = audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC).coerceAtLeast(1)
        val current = audioManager.getStreamVolume(AudioManager.STREAM_MUSIC)
        return (current * 100 / max).coerceIn(0, 100)
    }

    /** Sets the Android media stream volume from a 0-100 percentage. */
    private fun setSystemMediaVolumePercent(percent: Int) {
        val max = audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC).coerceAtLeast(1)
        val target = (percent.coerceIn(0, 100) * max + 50) / 100
        audioManager.setStreamVolume(AudioManager.STREAM_MUSIC, target.coerceIn(0, max), 0)
    }

    private fun setPhoneVolume(percent: Int) {
        phoneVolume = percent.coerceIn(0, 100)
        getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
            .edit()
            .putInt(KEY_PHONE_VOLUME, phoneVolume)
            .apply()
        // On the phone route, drive the actual system media stream so behaviour matches
        // the phone's own hardware volume keys (a real music player). The player itself
        // stays at full volume.
        if (phoneAudio) setSystemMediaVolumePercent(phoneVolume)
        publishState()
    }

    override fun onDestroy() {
        // Every step is independent: a throw in one must not skip the rest. In
        // particular, missing mediaCache.release() leaves the folder lock held, and
        // the next onCreate in this process then crashes with "Another SimpleCache
        // instance uses the folder" â€” an unrecoverable crash loop.
        runCatching { cacheDownloadJob?.cancel() }
        runCatching { coverArtJob?.cancel() }
        runCatching { qualitySwapJob?.cancel() }
        runCatching { transport.close() }
        runCatching { mediaSession.release() }
        runCatching { player.release() }
        runCatching { mediaCache.release() }
        runCatching { scope.cancel() }
        super.onDestroy()
    }

    private fun applySyncedSettings(
        route: Int?,
        watchVolume: Int?,
        phoneVolumeConfig: Int?,
        inputMode: Int?,
        showProgress: Int?,
        cacheEnabledConfig: Int?,
        cacheSizeConfig: Int?,
        coverArtBackgroundConfig: Int?,
        themeConfig: Int?,
        watchQualityConfig: Int?,
        phoneQualityConfig: Int?,
        cacheRadioConfig: Int?,
        routeEpochConfig: Int? = null,
    ) {
        // The route inside a settings blob used to be assigned bare â€” no player volume
        // change, no transport start/stop, no notification refresh â€” so a sync could
        // flip the route without moving the audio. Now it goes through setAudioRoute
        // like every other route change, and only when the blob's route is actually
        // newer/different: applying it unconditionally also made every sync clobber a
        // fresher phone-side change with whatever the watch had persisted.
        if (route != null) {
            val wantsPhone = route == Protocol.audioRoutePhone
            val shouldApply = when {
                routeEpochConfig != null && routeEpochConfig < routeEpoch -> false
                routeEpochConfig != null && routeEpochConfig == routeEpoch && wantsPhone == phoneAudio -> false
                else -> wantsPhone != phoneAudio || (routeEpochConfig != null && routeEpochConfig > routeEpoch)
            }
            if (shouldApply) {
                scope.launch { setAudioRoute(route, activeGeneration, routeEpochConfig) }
            }
        }
        // watchVolume and inputMode/showProgress are watch-local concerns; they are
        // applied on the watch itself and intentionally not stored here.
        phoneVolumeConfig?.let { phoneVolume = it.coerceIn(0, 100) }
        cacheEnabledConfig?.let { cacheEnabled = it != 0 }
        cacheRadioConfig?.let { cacheRadioEnabled = it != 0 }
        // Track whether the cover-art setting actually changed so we only re-send
        // when it did. Every settings sync used to trigger maybeSendCoverArt, which
        // could cancel an in-flight transfer (the dedup guard only matches identical
        // parameters) and leave the watch with no art — most visible when the user
        // was just switching themes, which fires a sync that has nothing to do with
        // cover art.
        val coverArtToggled = coverArtBackgroundConfig != null &&
            (coverArtBackgroundConfig != 0) != coverArtBackground
        coverArtBackgroundConfig?.let { coverArtBackground = it != 0 }
        themeConfig?.let { watchTheme = it.coerceIn(ThemeTeal, ThemeMono) }
        cacheSizeConfig?.let {
            var mb = it.coerceIn(MIN_CACHE_SIZE_MB, MAX_CACHE_SIZE_MB)
            mb = (mb / CACHE_SIZE_STEP_MB) * CACHE_SIZE_STEP_MB
            if (mb < MIN_CACHE_SIZE_MB) mb = MIN_CACHE_SIZE_MB
            cacheSizeMb = mb
        }
        // Only the tier for the *current* route matters for an in-progress stream;
        // capture whether it actually changed before overwriting, so toggling the
        // other route's tier (or the initial post-connect sync) never re-swaps.
        val activeQualityChanged = if (phoneAudio) {
            phoneQualityConfig != null && (phoneQualityConfig != 0) != phoneAudioQuality
        } else {
            watchQualityConfig != null && (watchQualityConfig != 0) != watchAudioQuality
        }
        watchQualityConfig?.let { watchAudioQuality = it != 0 }
        phoneQualityConfig?.let { phoneAudioQuality = it != 0 }
        getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
            .edit()
            .putBoolean(KEY_PHONE_AUDIO, phoneAudio)
            .putInt(KEY_PHONE_VOLUME, phoneVolume)
            .putBoolean(KEY_CACHE_ENABLED, cacheEnabled)
            .putInt(KEY_CACHE_SIZE_MB, cacheSizeMb)
            .putBoolean(KEY_COVER_ART_BG, coverArtBackground)
            .putInt(KEY_THEME, watchTheme)
            .putBoolean(KEY_WATCH_AUDIO_QUALITY, watchAudioQuality)
            .putBoolean(KEY_PHONE_AUDIO_QUALITY, phoneAudioQuality)
            .putBoolean(KEY_CACHE_RADIO, cacheRadioEnabled)
            .apply()
        // Only re-send or clear cover art when the setting actually toggled.
        // Previously every settings sync re-evaluated this, which could cancel an
        // in-flight transfer when only the theme had changed.
        val current = activeVideoId
        if (coverArtToggled) {
            if (coverArtBackground && current != null) {
                maybeSendCoverArt(current, activeGeneration)
            } else if (!coverArtBackground) {
                deliveredCover = null
                scope.launch { transport.sendCoverArtClear(activeGeneration) }
            }
        }
        if (activeQualityChanged && current != null) {
            // Re-resolve at the new bitrate ceiling and resume from where playback
            // currently is. Reuses activeGeneration (not a fresh one): the watch's
            // own generation counter doesn't advance for a phone-initiated re-swap,
            // so bumping it here would make the watch discard the new stream's
            // audio/cover-art chunks as stale.
            qualitySwapJob?.cancel()
            val resumeMs = player.currentPosition
            val generation = activeGeneration
            qualitySwapJob = scope.launch { play(current, generation, resumeMs) }
        }
        publishState()
    }

    // -- Public UI API ---------------------------------------------------------------

    /**
     * Cold flow that ticks the current playback position every [intervalMs] while
     * collected. Nothing ticks unless a collector is active (e.g. the Now Playing
     * screen is visible).
     */
    fun positionFlow(intervalMs: Long = 500L): Flow<Long> = flow {
        while (true) {
            emit(player.currentPosition.coerceAtLeast(0))
            delay(intervalMs)
        }
    }

    /**
     * Dispatch a UI command. Every variant delegates to the same private method the
     * watch commands hit, so watch and phone stay in lockstep. Phone commands reuse
     * [activeGeneration], never bump it â€” bumping would make the watch discard the
     * new stream's audio/cover chunks as stale.
     */
    fun onUiCommand(command: UiCommand) {
        when (command) {
            is UiCommand.Pause -> scope.launch { pausePlayback(activeGeneration) }
            is UiCommand.Resume -> scope.launch { resumePlayback(activeGeneration) }
            is UiCommand.Next -> scope.launch { skipTrack(previous = false, generation = activeGeneration) }
            is UiCommand.Previous -> scope.launch { skipTrack(previous = true, generation = activeGeneration) }
            is UiCommand.Stop -> scope.launch { stopPlayback(); publishState() }
            is UiCommand.SeekTo -> scope.launch { seekTo(command.positionMs, generation = null) }
            is UiCommand.ToggleShuffle -> scope.launch { toggleShuffle(activeGeneration) }
            is UiCommand.ToggleLoop -> scope.launch { toggleLoop() }
            is UiCommand.RefreshArtworkCache -> scope.launch {
                artworkUrlCache.clear()
                // Drop the delivered marker too: without it maybeSendCoverArt would
                // treat the re-send as redundant and this command would do nothing.
                deliveredCover = null
                // Re-resolve the playing track straight away so the player does not sit
                // on a blank cover waiting for the next track change, and push the fresh
                // art to the watch if it is showing cover-art backgrounds.
                activeVideoId?.let { videoId ->
                    artworkUrlFor(videoId)
                    maybeSendCoverArt(videoId, activeGeneration)
                }
                publishState()
            }
            is UiCommand.ToggleFavorite -> (command.videoId ?: activeVideoId)?.let { videoId ->
                scope.launch { toggleFavorite(videoId); publishState() }
            }
            is UiCommand.SetAudioRoute -> scope.launch {
                // Phone-initiated change: bump the epoch first so the watch applies this
                // as the newest route and a stale in-flight snapshot cannot revert it.
                routeEpoch++
                getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
                    .edit()
                    .putInt(KEY_ROUTE_EPOCH, routeEpoch)
                    .apply()
                // setAudioRoute applies side effects and confirms to the watch with a
                // state snapshot carrying the new epoch.
                setAudioRoute(
                    if (command.toPhone) Protocol.audioRoutePhone else Protocol.audioRouteWatch,
                    activeGeneration,
                    routeEpoch,
                )
            }
            // dispatchPlay, not play(): search results in Artist Radio / Song Radio
            // mode carry synthetic prefixed ids that start a radio queue instead of
            // playing a track.
            is UiCommand.Play -> scope.launch { dispatchPlay(command.videoId, activeGeneration) }
            is UiCommand.PlayPlaylist -> scope.launch {
                playPlaylist(command.playlistId, activeGeneration, command.startVideoId)
            }
            is UiCommand.JumpQueue -> scope.launch { jumpQueue(command.videoId, activeGeneration) }
            is UiCommand.DeleteCachedSong -> {
                deleteCachedSong(command.videoId)
            }
            is UiCommand.ClearCache -> {
                clearCache()
            }
            // No settings writes from the phone UI: shared settings are owned by the
            // watch (watch UI + Clay) and arrive via commandSyncSettings. The phone's
            // old settings screen wrote straight into applySyncedSettings and was then
            // clobbered by the watch's blob on the next sync, which is why it was
            // removed rather than mirrored.
        }
    }

    /**
     * Snapshot of the on-disk cache for the Cache screen. Must be called from a
     * coroutine (reads [searchMetadata] on the Main dispatcher, then cache I/O on IO).
     */
    /**
     * Playback history, newest first. Same `KEY_HISTORY` store the watch's
     * "Recently Played" library list reads, so the two screens agree.
     *
     * Unlike the watch path this does not filter to cached tracks â€” on the phone a
     * history entry that is no longer cached is still worth offering; playing it
     * just re-streams.
     */
    suspend fun recentlyPlayed(limit: Int = 50): List<SearchResultItem> =
        withContext(Dispatchers.IO) {
            val preferences = getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
            val history = runCatching { JSONArray(preferences.getString(KEY_HISTORY, "[]") ?: "[]") }
                .getOrElse { JSONArray() }
            buildList {
                val seen = mutableSetOf<String>()
                for (index in 0 until history.length()) {
                    if (size >= limit) break
                    val entry = history.optJSONObject(index) ?: continue
                    val videoId = entry.optString("videoId")
                    if (videoId.isBlank() || !seen.add(videoId)) continue
                    val title = entry.optString("title").takeIf { it.isNotBlank() && it != videoId }
                        ?: storedMetadataFor(videoId)?.first
                        ?: continue
                    add(
                        SearchResultItem(
                            videoId = videoId,
                            title = title,
                            artist = entry.optString("artist"),
                        )
                    )
                }
            }
        }

    /**
     * Favorited tracks, newest first â€” the same store and metadata mapping as the
     * watch's Favorites library list (`sendLibrary`/`Protocol.libraryFavorites`), so
     * the phone tab and the watch list agree.
     */
    suspend fun favoritesList(): List<SearchResultItem> = withContext(Dispatchers.IO) {
        val preferences = getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
        val favorites = runCatching { JSONArray(preferences.getString(KEY_FAVORITES, "[]") ?: "[]") }
            .getOrElse { JSONArray() }
        buildList {
            for (index in 0 until favorites.length()) {
                val videoId = favorites.optString(index)
                if (videoId.isBlank()) continue
                val meta = storedMetadataFor(videoId)
                add(
                    SearchResultItem(
                        videoId = videoId,
                        title = meta?.first ?: "Unknown title",
                        artist = meta?.second.orEmpty(),
                    ),
                )
            }
        }
    }

    /**
     * "Songs you might like", seeded from [seedVideoId].
     *
     * Uses the same `YouTube.next(WatchEndpoint(...))` radio call as [playSongRadio],
     * which is YouTube Music's own related-tracks feed â€” so recommendations here
     * match what starting a song radio on the watch would queue up. The seed itself
     * is dropped; recommending the track you just played is not a recommendation.
     */
    suspend fun recommendationsFor(
        seedVideoId: String,
        limit: Int = 20,
    ): List<SearchResultItem> = withContext(Dispatchers.IO) {
        val result = runCatching { YouTube.next(WatchEndpoint(videoId = seedVideoId)).getOrNull() }
            .getOrNull()
        result?.items.orEmpty()
            .distinctBy { it.id }
            .filterNot { it.id == seedVideoId }
            .take(limit)
            .map { song ->
                val artist = song.artists.joinToString(", ") { it.name }
                // Record metadata so playing one still shows a real title downstream.
                searchMetadata[song.id] = song.title to artist
                SearchResultItem(videoId = song.id, title = song.title, artist = artist)
            }
    }

    /** Video ids that are fully cached, for the "offline" badges in lists. */
    suspend fun cachedVideoIds(): Set<String> = withContext(Dispatchers.IO) {
        mediaCache.keys.asSequence().filter(::isFullyCached).toSet()
    }

    /** Current configuration, for the Settings screen. Reads live fields, not prefs. */
    fun settingsSnapshot(): SettingsUiState = SettingsUiState(
        theme = watchTheme,
        phoneAudio = phoneAudio,
        phoneVolume = phoneVolume,
        cacheEnabled = cacheEnabled,
        cacheSizeMb = cacheSizeMb,
        cacheRadio = cacheRadioEnabled,
        coverArtBackground = coverArtBackground,
        watchHighQuality = watchAudioQuality,
        phoneHighQuality = phoneAudioQuality,
    )

    suspend fun cacheSnapshot(): CacheUiState {
        // Capture the metadata map on Main first â€” it is a plain mutableMapOf mutated
        // from the Main-dispatched scope, so reading it off-thread is a race.
        val metadata = HashMap(searchMetadata)
        return withContext(Dispatchers.IO) {
            val ids = cachedTrackIds()
            val entries = ids.map { videoId ->
                val (title, artist) = metadata[videoId]
                    ?: storedMetadataFor(videoId)
                    ?: ("Unknown title" to "")
                CachedSongEntry(
                    videoId = videoId,
                    title = title,
                    artist = artist,
                    cachedBytes = cachedBytesFor(videoId),
                )
            }
            CacheUiState(
                songsCached = ids.size,
                usedBytes = mediaCache.cacheSpace,
                budgetBytes = cacheSizeMb.toLong() * 1024L * 1024L,
                deviceFreeBytes = filesDir.usableSpace,
                entries = entries,
            )
        }
    }

    /** Per-song cached bytes using the most efficient method available. */
    private fun cachedBytesFor(videoId: String): Long {
        val contentLength = mediaCache.getContentMetadata(videoId).get(
            ContentMetadata.KEY_CONTENT_LENGTH,
            C.LENGTH_UNSET.toLong(),
        )
        if (contentLength > 0 && mediaCache.isCached(videoId, 0, contentLength)) {
            return contentLength
        }
        // Fallback: sum the cached spans.
        return mediaCache.getCachedSpans(videoId).sumOf { it.length }
    }

    /**
     * Current queue as resolved videoIds with metadata, for the phone's queue view.
     */
    fun currentQueue(): List<Pair<String, Pair<String, String>>> {
        return currentQueueList().map { videoId ->
            videoId to (searchMetadata[videoId]
                ?: storedMetadataFor(videoId)
                ?: ("Unknown title" to ""))
        }
    }

    companion object {
        const val ACTION_LOCAL_BIND = "dev.pebble.musicbridge.LOCAL_PLAYBACK_BIND"
        private const val LoopModeOff = 0
        private const val LoopModeOne = 1
        private const val LoopModeAll = 2
        private const val PREFERENCES_NAME = "dreamwave_playback"
        private const val KEY_PHONE_AUDIO = "phone_audio"
        private const val KEY_ROUTE_EPOCH = "route_epoch"
        private const val KEY_PHONE_VOLUME = "phone_volume"
        private const val KEY_CACHE_ENABLED = "cache_enabled"
        private const val KEY_CACHE_SIZE_MB = "cache_size_mb"
        private const val KEY_COVER_ART_BG = "cover_art_background"
        private const val KEY_THEME = "theme"
        private const val KEY_WATCH_AUDIO_QUALITY = "watch_audio_quality"
        private const val KEY_PHONE_AUDIO_QUALITY = "phone_audio_quality"
        private const val KEY_CACHE_RADIO = "cache_radio"
        private const val WATCH_EFFICIENT_KBPS = 64
        private const val PHONE_DATA_SAVER_KBPS = 96
        private const val ARTIST_RADIO_PREFIX = "artist:"
        private const val SONG_RADIO_PREFIX = "songradio:"
        // Synthetic id prefix the watch's Playlists rows carry; dispatchPlay turns it
        // into playPlaylist(). Same trick as the two radio prefixes.
        private const val PLAYLIST_PREFIX = "playlist:"
        private const val KEY_PLAYLISTS = "playlists"
        private const val KEY_LOOP_MODE = "loop_mode"
        private const val DEFAULT_PHONE_VOLUME = 50
        private const val KEY_HISTORY = "playback_history"
        private const val KEY_METADATA = "track_metadata"
        private const val KEY_FAVORITES = "favorite_video_ids"
        private const val KEY_RECENT_SEARCHES = "recent_searches"
        private const val KEY_RESUME = "continue_listening"
        private const val HISTORY_LIMIT = 25
        private const val RESUME_LIMIT = 25
        // Only offer to "continue" a track if the user got past this point and stopped
        // before this far from the end (avoids listing barely-started or finished songs).
        private const val RESUME_MIN_POSITION_MS = 15_000L
        private const val RESUME_END_MARGIN_MS = 15_000L
        // Keep more recent searches on disk than we typically display so the watch's
        // configurable display length can grow without discarding history.
        private const val RECENT_SEARCHES_STORE = 30
        // Hard upper bound on library entries sent to the watch. Must match the
        // watch's MAX_LIBRARY so the watch never drops entries it asked for.
        private const val LIBRARY_HARD_CAP = 60
        // Upper bound on queue entries sent to the watch (matches watch MAX_LIBRARY,
        // since the Queue screen reuses the watch's shared results array).
        private const val MAX_QUEUE_ITEMS = 60
        private const val DEFAULT_SEARCH_LIMIT = 5
        // Upper bound on search results returned to the watch (matches watch MAX_RESULTS).
        private const val SEARCH_LIMIT_MAX = 10
        private const val DEFAULT_CACHE_SIZE_MB = 250
        private const val MIN_CACHE_SIZE_MB = 100
        private const val MAX_CACHE_SIZE_MB = 1000
        private const val CACHE_SIZE_STEP_MB = 50
        private const val MEDIA_CACHE_DIR = "dreamwave_media_cache"
        // Fallback when the real cache dir cannot be opened even after a reset (see
        // openMediaCache). Deliberately a different directory: a SimpleCache that threw
        // mid-construction can leave its folder lock held for the rest of the process.
        private const val MEDIA_CACHE_SCRATCH_DIR = "dreamwave_media_cache_scratch"
        // Media3's DefaultMediaNotificationProvider posts the now-playing notification
        // with ID 1001 by default. We reuse that SAME id for both our placeholder
        // foreground notification and the watch-route status notification so that
        // whichever notification we or Media3 post always replaces the previous one
        // (no duplicate/stacked notifications) and the system media controls key off it.
        private const val MEDIA_NOTIFICATION_ID = 1001
        private const val WATCH_STATUS_CHANNEL_ID = "dreamwave_watch_status"
        private const val WATCH_STATUS_NOTIFICATION_ID = MEDIA_NOTIFICATION_ID
        // Channel for the placeholder foreground notification we post ourselves to
        // satisfy the startForeground() contract before Media3 swaps in its own.
        private const val MEDIA_CHANNEL_ID = "dreamwave_playback"
        // Cover-art size is chosen by audio route: small (64) while streaming audio to the
        // watch, so the cover transfer does not contend with the audio stream over BLE and
        // fail; sharp (144) when audio is on the phone and the link is free.
        private const val COVER_ART_LOW_DIM = 64
        private const val COVER_ART_HIGH_DIM = 144
        private const val COVER_ART_CHUNK_BYTES = 512
        // Longest the watch audio stream is held waiting for the cover to transfer before
        // giving up and letting audio flow (safety net against a slow/failed cover fetch).
        private const val COVER_HOLD_TIMEOUT_MS = 3500L
        // Heavily oversampled relative to the 64x64 target: more real source pixels give
        // the watchimage encoder's bilinear downscale far more to average, smoothing out
        // JPEG blocking and yielding a cleaner 64x64 than a small, already-degraded source.
        // YT Music serves album art up to ~1080 (the main app requests 1080 for its own UI),
        // so 720 won't upscale for most tracks.
        private const val COVER_ART_SOURCE_SIZE = 720
        // What the *watch* path requests instead. Still 4.5x the 64px target, so the
        // bilinear downscale has plenty to average, but small enough that the decode,
        // the PNG normalisation and the gomobile call all stay well inside the 3.5 s
        // the audio stream is held for. 720 is for Coil and the phone's screen only.
        private const val COVER_ART_WATCH_SOURCE_SIZE = 288
        private const val COVER_CACHE_DIR = "dreamwave_cover_cache"
        // Encoded payloads run from 512 B (64x64 mono) to ~20 KB (144x144 colour), so this
        // budget holds on the order of a thousand covers while staying negligible next to
        // even the 100 MB minimum audio cache.
        private const val COVER_CACHE_MAX_BYTES = 6L * 1024L * 1024L
        private val COVER_CACHE_UNSAFE_CHARS = "[^A-Za-z0-9_-]".toRegex()
        private val SQUARE_THUMBNAIL_HOST_PATTERN =
            "https://(?:lh3|yt3)\\.googleusercontent\\.com/.*=w\\d+-h\\d+.*".toRegex()

        private const val ThemeTeal = 0
        private const val ThemePurple = 1
        private const val ThemeSunset = 2
        private const val ThemeDefault = 3
        private const val ThemeMono = 4
    }

    /**
     * Chooses the notification style based on the current audio route. On the phone
     * route it delegates to Media3's [DefaultMediaNotificationProvider] for the full
     * now-playing media notification (transport controls, artwork, media-button
     * routing). On the watch route the phone is muted and simply relaying PCM audio,
     * so it shows a minimal, non-interactive "Streaming to watch" status notification.
     */
    private class RouteAwareNotificationProvider(
        private val context: Context,
        private val isPhoneRoute: () -> Boolean,
    ) : MediaNotification.Provider {
        private val defaultProvider = DefaultMediaNotificationProvider.Builder(context).build()

        init {
            val manager = context.getSystemService(NotificationManager::class.java)
            if (manager.getNotificationChannel(WATCH_STATUS_CHANNEL_ID) == null) {
                manager.createNotificationChannel(
                    NotificationChannel(
                        WATCH_STATUS_CHANNEL_ID,
                        "Watch playback",
                        NotificationManager.IMPORTANCE_LOW,
                    ).apply { setShowBadge(false) },
                )
            }
        }

        override fun createNotification(
            mediaSession: MediaSession,
            customLayout: ImmutableList<CommandButton>,
            actionFactory: MediaNotification.ActionFactory,
            onNotificationChangedCallback: MediaNotification.Provider.Callback,
        ): MediaNotification {
            if (isPhoneRoute()) {
                return defaultProvider.createNotification(
                    mediaSession, customLayout, actionFactory, onNotificationChangedCallback,
                )
            }
            val metadata = mediaSession.player.mediaMetadata
            val title = metadata.title?.toString() ?: "Playing on watch"
            val isPlaying = mediaSession.player.isPlaying
            // createMediaActionPendingIntent returns a plain platform PendingIntent, unlike
            // createMediaAction (which returns an androidx.core NotificationCompat.Action
            // this module can't compile against - androidx.core isn't compile-visible here).
            val playPauseIntent = actionFactory.createMediaActionPendingIntent(mediaSession, Player.COMMAND_PLAY_PAUSE)
            val stopIntent = actionFactory.createMediaActionPendingIntent(mediaSession, Player.COMMAND_STOP)
            val playPauseIcon = if (isPlaying) android.R.drawable.ic_media_pause else android.R.drawable.ic_media_play
            // minSdk 26 guarantees the channel-based Notification.Builder is available.
            // MediaStyle + the platform session token is what makes Pause/Stop (and the
            // system's own media controls, e.g. lock screen) actually work here - the
            // previous version of this notification had neither, so it was inert.
            val notification = Notification.Builder(context, WATCH_STATUS_CHANNEL_ID)
                .setContentTitle(title)
                .setContentText("Playing on watch")
                .setSmallIcon(android.R.drawable.stat_sys_headset)
                .setOngoing(true)
                .setStyle(
                    Notification.MediaStyle()
                        .setMediaSession(mediaSession.platformToken)
                        .setShowActionsInCompactView(0, 1),
                )
                .addAction(
                    Notification.Action.Builder(playPauseIcon, if (isPlaying) "Pause" else "Play", playPauseIntent).build(),
                )
                .addAction(
                    Notification.Action.Builder(android.R.drawable.ic_menu_close_clear_cancel, "Stop", stopIntent).build(),
                )
                .build()
            return MediaNotification(WATCH_STATUS_NOTIFICATION_ID, notification)
        }

        override fun getNotificationChannelInfo(): MediaNotification.Provider.NotificationChannelInfo {
            // Reuse whatever Media3's default provider configures for the media channel.
            return defaultProvider.notificationChannelInfo
        }

        override fun handleCustomCommand(
            session: MediaSession,
            action: String,
            extras: android.os.Bundle,
        ): Boolean = defaultProvider.handleCustomCommand(session, action, extras)
    }

    private class PcmRenderersFactory(
        context: Context,
        private val processor: AudioProcessor,
    ) : DefaultRenderersFactory(context) {
        override fun buildAudioSink(
            context: Context,
            enableFloatOutput: Boolean,
            enableAudioTrackPlaybackParams: Boolean,
        ): AudioSink = DefaultAudioSink.Builder(context)
            .setEnableFloatOutput(false)
            .setAudioProcessors(arrayOf(processor))
            .build()
    }
}

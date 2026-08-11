package dev.pebble.musicbridge

import android.app.Service
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.graphics.BitmapFactory
import android.media.AudioManager
import android.net.Uri
import android.os.Binder
import android.os.Bundle
import android.os.IBinder
import android.util.Log
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import androidx.media3.session.LibraryResult
import androidx.media3.session.MediaBrowser
import androidx.media3.session.SessionCommand
import androidx.media3.session.SessionToken
import dev.pebble.watchimagebridge.watchimagebridge.Watchimagebridge
import io.rebble.pebblekit2.common.model.PebbleDictionary
import io.rebble.pebblekit2.common.model.PebbleDictionaryItem
import io.rebble.pebblekit2.common.model.ReceiveResult
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.guava.await
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import okhttp3.OkHttpClient
import okhttp3.Request

/**
 * Remote-control backend for the "music-src" watch app fork: instead of resolving and
 * playing YouTube streams itself (see PebblePlaybackService), this binds to Symfonium's
 * exported MediaBrowserService and mirrors ITS playback state to the watch, translating
 * watch commands into standard Player transport calls / Symfonium's custom session
 * actions. Audio always plays on the phone through Symfonium's own player - there is no
 * watch-speaker route, since nothing lets a third-party app pull another app's decoded
 * audio (see the investigation that preceded this file).
 */
@OptIn(kotlinx.coroutines.ExperimentalCoroutinesApi::class)
class SymfoniumPlaybackService : Service() {
    inner class LocalBinder : Binder() {
        val service: SymfoniumPlaybackService
            get() = this@SymfoniumPlaybackService
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private val localBinder = LocalBinder()
    private val audioManager by lazy { getSystemService(Context.AUDIO_SERVICE) as AudioManager }
    private lateinit var transport: PebbleAudioTransport
    private var browserDeferred = CompletableDeferred<MediaBrowser>()
    private var activeGeneration = 0
    private var watchTheme = ThemeDefault

    // Custom session actions Symfonium exposes for its notification/Auto surface
    // (Shuffle/Repeat/Favorite), keyed by their display name - see refreshCustomActions().
    // There is no standard Player.COMMAND_SET_SHUFFLE_MODE/SET_REPEAT_MODE support on
    // Symfonium's exported session (confirmed via dumpsys media_session), so toggling
    // these has to go through sendCustomCommand() instead of Player.setShuffleModeEnabled().
    private val customActions = mutableMapOf<String, SessionCommand>()

    // MediaItems returned by the last library/queue browse, keyed by mediaId, so a
    // subsequent CommandPlay/CommandQueueJump can hand the *actual* MediaItem back to the
    // controller - legacy-bridged playback needs the original item's extras, not a
    // synthetic MediaItem reconstructed from a bare id string.
    private val itemCache = mutableMapOf<String, MediaItem>()

    private val artHttpClient = OkHttpClient()
    private var coverArtEnabled = false
    private var coverArtJob: Job? = null
    // The artwork actually last transferred to the watch, so repeated metadata pings for
    // the same track (position ticks etc. also fire onMediaMetadataChanged) don't re-send
    // the same image over and over.
    private var lastSentArtworkUri: String? = null

    override fun onCreate() {
        super.onCreate()
        Log.i(TAG, "Symfonium bridge starting")
        transport = PebbleAudioTransport(applicationContext, scope, Protocol.symfoniumAppUuid)
        getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE).run {
            watchTheme = getInt(KEY_THEME, ThemeDefault).coerceIn(ThemeTeal, ThemeArcade)
            // Defaults on here (unlike PebblePlaybackService's off-by-default): that
            // default existed to protect BLE bandwidth for the watch-speaker audio
            // stream, which this backend never runs (see maybeSendCoverArt's kdoc).
            coverArtEnabled = getBoolean(KEY_COVER_ART_BG, true)
        }
        scope.launch { connect() }
    }

    private suspend fun connect() {
        runCatching {
            val token = SessionToken(applicationContext, ComponentName(SYMFONIUM_PACKAGE, SYMFONIUM_SERVICE))
            val browser = MediaBrowser.Builder(applicationContext, token)
                .setListener(object : MediaBrowser.Listener {
                    override fun onDisconnected(controller: androidx.media3.session.MediaController) {
                        Log.w(TAG, "Symfonium session disconnected; reconnecting")
                        browserDeferred = CompletableDeferred()
                        scope.launch { connect() }
                    }
                    override fun onCustomLayoutChanged(
                        controller: androidx.media3.session.MediaController,
                        layout: List<androidx.media3.session.CommandButton>,
                    ) {
                        cacheCustomActions(layout)
                    }
                })
                .buildAsync()
                .await()
            cacheCustomActions(browser.customLayout)
            browser.addListener(object : Player.Listener {
                override fun onMediaMetadataChanged(mediaMetadata: androidx.media3.common.MediaMetadata) {
                    Log.i(
                        TAG,
                        "onMediaMetadataChanged: title=${mediaMetadata.title} art=${mediaMetadata.artworkUri} " +
                            "coverArtEnabled=$coverArtEnabled lastSent=$lastSentArtworkUri",
                    )
                    scope.launch { sendStateSnapshot() }
                    maybeSendCoverArt(browser)
                }
                override fun onIsPlayingChanged(isPlaying: Boolean) {
                    scope.launch { sendStateSnapshot() }
                }
                override fun onPlaybackStateChanged(playbackState: Int) {
                    scope.launch { sendStateSnapshot() }
                }
                override fun onPositionDiscontinuity(
                    oldPosition: Player.PositionInfo,
                    newPosition: Player.PositionInfo,
                    reason: Int,
                ) {
                    scope.launch { transport.sendPlaybackPosition(newPosition.positionMs.coerceAtLeast(0), activeGeneration) }
                }
            })
            browserDeferred.complete(browser)
            Log.i(TAG, "Connected to Symfonium session")
        }.onFailure {
            Log.e(TAG, "Failed to connect to Symfonium's MediaBrowserService", it)
        }
    }

    private fun cacheCustomActions(layout: List<androidx.media3.session.CommandButton>) {
        customActions.clear()
        layout.forEach { button ->
            val command = button.sessionCommand ?: return@forEach
            customActions[button.displayName.toString()] = command
        }
        Log.i(TAG, "Symfonium custom actions: ${customActions.keys}")
    }

    /** Awaits the browser, reconnecting first if a previous connection dropped. */
    private suspend fun awaitBrowser(): MediaBrowser? {
        if (browserDeferred.isCompleted && browserDeferred.getCompleted().isConnected) {
            return browserDeferred.getCompleted()
        }
        // Not yet connected (still connecting from onCreate) or reconnecting after
        // onDisconnected reset the deferred and relaunched connect() itself - either way
        // something is already driving it toward completion, just wait.
        return withTimeoutOrNull(CONNECT_TIMEOUT_MS) { browserDeferred.await() }
    }

    override fun onBind(intent: Intent?): IBinder = localBinder

    /**
     * Each step guarded independently, mirroring PebblePlaybackService.onDestroy. An
     * exception escaping onDestroy takes down the entire process - including the other
     * service's MediaSession and media notification - so shutdown never gets to throw.
     */
    override fun onDestroy() {
        runCatching { transport.close() }
        runCatching { coverArtJob?.cancel() }
        if (browserDeferred.isCompleted) runCatching { browserDeferred.getCompleted().release() }
        runCatching { scope.cancel() }
        super.onDestroy()
    }

    suspend fun onWatchMessage(data: PebbleDictionary): ReceiveResult {
        val command = (data[Protocol.keyCommand] as? PebbleDictionaryItem.Int32)?.value ?: return ReceiveResult.Nack
        Log.i(TAG, "Watch command received: $command")
        // The watch bumps its own generation counter locally on most button presses
        // (skip/pause/seek/etc.) before it even gets a reply, and silently drops any
        // state-snapshot/cover-art event whose generation doesn't match its current one.
        // Adopt whatever generation *any* incoming command carries, not just Play, so
        // outgoing events stay in step instead of echoing a stale value from the last play.
        (data[Protocol.keyGeneration] as? PebbleDictionaryItem.Int32)?.value?.let { activeGeneration = it }
        val browser = awaitBrowser() ?: run {
            transport.sendEvent(Protocol.eventError, "Can't reach Symfonium")
            return ReceiveResult.Nack
        }
        when (command) {
            Protocol.commandHello -> {
                transport.sendEvent(Protocol.eventReady, "Symfonium bridge ready")
                sendStateSnapshot()
            }
            Protocol.commandRequestState -> sendStateSnapshot()
            Protocol.commandRequestLibrary -> {
                val libraryType = (data[Protocol.keyLibraryType] as? PebbleDictionaryItem.Int32)?.value
                    ?: return ReceiveResult.Nack
                val limit = (data[Protocol.keyLibraryLimit] as? PebbleDictionaryItem.Int32)?.value
                sendLibrary(browser, libraryType, limit)
            }
            Protocol.commandSearch -> {
                val requestId = (data[Protocol.keySearchRequestId] as? PebbleDictionaryItem.Int32)?.value ?: 0
                transport.sendSearchError(requestId, "Search isn't available yet")
            }
            Protocol.commandPlay -> {
                val mediaId = (data[Protocol.keyVideoId] as? PebbleDictionaryItem.Text)?.value
                    ?: return ReceiveResult.Nack
                val generation = (data[Protocol.keyGeneration] as? PebbleDictionaryItem.Int32)?.value
                    ?: return ReceiveResult.Nack
                activeGeneration = generation
                playMediaId(browser, mediaId, generation)
            }
            Protocol.commandStop -> browser.stop()
            Protocol.commandPause -> browser.pause()
            Protocol.commandResume -> browser.play()
            Protocol.commandToggleLoop -> invokeCustomAction(browser, "Repeat")
            Protocol.commandToggleShuffle -> invokeCustomAction(browser, "Shuffle")
            Protocol.commandToggleFavorite -> invokeCustomAction(browser, "Favorite")
            Protocol.commandPrevious -> browser.seekToPrevious()
            Protocol.commandNext -> browser.seekToNext()
            Protocol.commandDeleteCached -> return ReceiveResult.Nack
            Protocol.commandRequestQueue -> sendQueue(browser)
            Protocol.commandQueueJump -> {
                val mediaId = (data[Protocol.keyVideoId] as? PebbleDictionaryItem.Text)?.value
                    ?: return ReceiveResult.Nack
                val generation = (data[Protocol.keyGeneration] as? PebbleDictionaryItem.Int32)?.value
                    ?: return ReceiveResult.Nack
                jumpQueue(browser, mediaId, generation)
            }
            Protocol.commandSeek -> {
                val positionMs = (data[Protocol.keyPosition] as? PebbleDictionaryItem.Int32)?.value
                    ?: return ReceiveResult.Nack
                browser.seekTo(positionMs.toLong().coerceAtLeast(0))
                transport.sendPlaybackPosition(positionMs.toLong().coerceAtLeast(0), activeGeneration)
            }
            Protocol.commandSetAudioRoute -> {
                // Symfonium always plays through the phone; there is no watch-speaker
                // route to switch to. Just echo the phone-route state back.
                sendStateSnapshot()
            }
            Protocol.commandSetVolume -> {
                val volume = (data[Protocol.keyVolume] as? PebbleDictionaryItem.Int32)?.value
                    ?: return ReceiveResult.Nack
                setSystemMediaVolumePercent(volume)
                sendStateSnapshot()
            }
            Protocol.commandSyncSettings -> {
                // No reply here, deliberately: the watch's EventStateSnapshot handler
                // calls bridge_connected() -> send_settings_sync() on *every* snapshot
                // (not just the first), so replying with a snapshot here would ping-pong
                // forever. The original PebblePlaybackService.applySyncedSettings() is
                // silent for the same reason.
                val themeConfig = (data[Protocol.keyTheme] as? PebbleDictionaryItem.Int32)?.value
                val coverArtConfig = (data[Protocol.keyConfigCoverArtBackground] as? PebbleDictionaryItem.Int32)?.value
                if (themeConfig != null || coverArtConfig != null) {
                    getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE).edit().apply {
                        themeConfig?.let {
                            watchTheme = it.coerceIn(ThemeTeal, ThemeArcade)
                            putInt(KEY_THEME, watchTheme)
                        }
                        coverArtConfig?.let {
                            coverArtEnabled = it != 0
                            putBoolean(KEY_COVER_ART_BG, coverArtEnabled)
                        }
                    }.apply()
                    if (coverArtConfig != null) {
                        val browser = if (browserDeferred.isCompleted) browserDeferred.getCompleted() else null
                        if (coverArtEnabled && browser != null) {
                            lastSentArtworkUri = null // force a resend under the (possibly new) theme
                            maybeSendCoverArt(browser)
                        } else {
                            coverArtJob?.cancel()
                            scope.launch { transport.sendCoverArtClear(activeGeneration) }
                        }
                    }
                }
            }
            else -> return ReceiveResult.Nack
        }
        return ReceiveResult.Ack
    }

    private fun invokeCustomAction(browser: MediaBrowser, displayName: String) {
        val command = customActions[displayName]
        if (command == null) {
            Log.w(TAG, "No '$displayName' custom action exposed by Symfonium right now")
            return
        }
        browser.sendCustomCommand(command, Bundle.EMPTY)
    }

    /**
     * Plays a browsed item by mediaId. Songs (Recently Played/Favorites rows) are
     * playable directly. Playlist rows are browsable containers, not playable - Symfonium
     * exposes a synthetic "Play" pseudo-item as the first child of a playlist (see the
     * MediaBrowser spike), which is what actually starts the playlist; we resolve and
     * play that instead of the container itself.
     */
    private suspend fun playMediaId(browser: MediaBrowser, mediaId: String, generation: Int) {
        var item = itemCache[mediaId]
        if (item == null) {
            transport.sendEvent(Protocol.eventError, "Song no longer available", generation)
            return
        }
        if (item.mediaMetadata.isPlayable != true) {
            val children = runCatching { browser.getChildren(mediaId, 0, 10, null).await() }.getOrNull()
            val firstPlayable = children?.value?.firstOrNull { it.mediaMetadata.isPlayable == true }
            children?.value?.forEach { itemCache[it.mediaId] = it }
            if (firstPlayable == null) {
                transport.sendEvent(Protocol.eventError, "Nothing playable in that playlist", generation)
                return
            }
            item = firstPlayable
        }
        browser.setMediaItem(item)
        browser.prepare()
        browser.play()
    }

    private suspend fun sendLibrary(browser: MediaBrowser, libraryType: Int, requestedLimit: Int?) {
        val limit = (requestedLimit ?: LIBRARY_HARD_CAP).coerceIn(1, LIBRARY_HARD_CAP)
        val browseId = when (libraryType) {
            Protocol.libraryRecent -> BROWSE_RECENT
            Protocol.libraryFavorites -> BROWSE_FAVORITE_SONGS
            Protocol.libraryContinue -> BROWSE_PLAYLISTS // repurposed: Playlists, see LIBRARY_ITEMS in the watch fork
            else -> null
        }
        if (browseId == null) {
            transport.sendLibraryComplete(libraryType)
            return
        }
        val result = runCatching { browser.getChildren(browseId, 0, limit, null).await() }.getOrNull()
        val items = result?.takeIf { it.resultCode == LibraryResult.RESULT_SUCCESS }?.value.orEmpty()
        items.forEachIndexed { index, item ->
            itemCache[item.mediaId] = item
            val title = item.mediaMetadata.title?.toString() ?: "Unknown title"
            val artist = item.mediaMetadata.artist?.toString()
                ?: item.mediaMetadata.subtitle?.toString().orEmpty()
            transport.sendLibraryItem(libraryType, index, item.mediaId, title, artist)
        }
        transport.sendLibraryComplete(libraryType)
    }

    /**
     * Mirrors Symfonium's own live playback timeline - whatever it's actually playing
     * next, regardless of how the current track was reached (a playlist, favorites,
     * Symfonium's own shuffle/radio). This is a read of the session's real queue rather
     * than something we reconstruct.
     */
    private suspend fun sendQueue(browser: MediaBrowser) {
        val count = browser.mediaItemCount
        if (count == 0) {
            transport.sendQueueComplete()
            return
        }
        val current = browser.currentMediaItemIndex.coerceIn(0, count - 1)
        val ordered = (current until count) + (0 until current)
        ordered.take(MAX_QUEUE_ITEMS).forEachIndexed { index, itemIndex ->
            val item = browser.getMediaItemAt(itemIndex)
            itemCache[item.mediaId] = item
            val title = item.mediaMetadata.title?.toString() ?: "Unknown title"
            val artist = item.mediaMetadata.artist?.toString().orEmpty()
            transport.sendQueueItem(index, item.mediaId, title, artist)
        }
        transport.sendQueueComplete()
    }

    private suspend fun jumpQueue(browser: MediaBrowser, mediaId: String, generation: Int) {
        val count = browser.mediaItemCount
        val index = (0 until count).firstOrNull { browser.getMediaItemAt(it).mediaId == mediaId }
        if (index == null) {
            transport.sendEvent(Protocol.eventError, "Song no longer in queue", generation)
            return
        }
        browser.seekTo(index, 0)
    }

    private suspend fun sendStateSnapshot() {
        val browser = if (browserDeferred.isCompleted) browserDeferred.getCompleted() else null
        val playbackState = when {
            browser == null -> Protocol.playbackIdle
            browser.playbackState == Player.STATE_BUFFERING -> Protocol.playbackBuffering
            browser.isPlaying -> Protocol.playbackPlaying
            browser.playbackState == Player.STATE_IDLE -> Protocol.playbackIdle
            else -> Protocol.playbackPaused
        }
        val metadata = browser?.mediaMetadata
        val durationMs = browser?.duration?.takeIf { it != androidx.media3.common.C.TIME_UNSET }?.coerceAtLeast(0) ?: 0L
        val positionMs = browser?.currentPosition?.coerceAtLeast(0) ?: 0L
        // The watch only latches a new "now playing" track once it has video+title+artist
        // all present together, so the current item's mediaId has to travel as videoId.
        browser?.currentMediaItem?.let { itemCache[it.mediaId] = it }
        transport.sendStateSnapshot(
            playbackState = playbackState,
            generation = activeGeneration,
            videoId = browser?.currentMediaItem?.mediaId,
            title = metadata?.title?.toString(),
            artist = metadata?.artist?.toString(),
            durationMs = durationMs,
            positionMs = positionMs,
            phoneAudio = true,
            volume = systemMediaVolumePercent(),
            loopEnabled = browser?.repeatMode == Player.REPEAT_MODE_ONE,
            loopMode = when (browser?.repeatMode) {
                Player.REPEAT_MODE_ALL -> LoopModeAll
                Player.REPEAT_MODE_ONE -> LoopModeOne
                else -> LoopModeOff
            },
            shuffleEnabled = browser?.shuffleModeEnabled == true,
            // Symfonium doesn't expose per-track favorite state through this API surface,
            // only a toggle action - so the watch's heart indicator can't reflect it yet.
            isFavorite = false,
            theme = watchTheme,
            // This backend has no watch-speaker route (audio always plays on the phone),
            // so the route never changes and stays at epoch 0.
            routeEpoch = 0,
        )
    }

    /**
     * Transfers Symfonium's current artwork to the watch, ported from
     * PebblePlaybackService.maybeSendCoverArt() - same watchimage encode pipeline, same
     * chunked transport. Simpler here because there is no watch-speaker audio stream to
     * share the AppMessage inbox with (audio always plays on the phone through Symfonium),
     * so none of the original's holdAudioForCover()/releaseAudioForCover() pacing applies.
     */
    private fun maybeSendCoverArt(browser: MediaBrowser) {
        if (!coverArtEnabled) return
        // Legacy-bridged metadata updates can arrive in quick bursts where an
        // intermediate snapshot is momentarily missing fields (artworkUri included) that
        // the very next snapshot has - observed live: a real artwork URI followed ~60ms
        // later by a metadata change with a null one, for the same track. Settling briefly
        // and re-reading the *current* metadata (rather than trusting the value the
        // triggering callback captured) avoids reacting to that transient blank state.
        // Each call cancels the previous job, so only the last event in a burst survives
        // the delay and actually runs.
        coverArtJob?.cancel()
        coverArtJob = scope.launch {
            delay(COVER_ART_SETTLE_MS)
            val artworkUri = browser.mediaMetadata.artworkUri
            val key = artworkUri?.toString()
            // Only short-circuits on a *confirmed* prior success (lastSentArtworkUri is
            // set at the bottom of this function, after the transfer actually completes -
            // never optimistically here). Marking it before attempting was the previous
            // bug: one failed/rejected attempt (e.g. the generation-mismatch bug) would
            // permanently mark that URI "done" and silently skip every retry - including
            // for every other track from the same album, since Symfonium's artwork URI is
            // keyed by album, not by track.
            if (key == lastSentArtworkUri) return@launch
            if (artworkUri == null) {
                Log.i(TAG, "No artwork URI for current track; clearing")
                transport.sendCoverArtClear(activeGeneration)
                lastSentArtworkUri = key
                return@launch
            }
            Log.i(TAG, "Fetching artwork: $artworkUri")
            val raw = loadArtworkBytes(artworkUri)
            if (raw == null) {
                Log.w(TAG, "Artwork fetch failed for $artworkUri")
                transport.sendCoverArtClear(activeGeneration)
                return@launch
            }
            Log.i(TAG, "Artwork fetched: ${raw.size} bytes; encoding at ${COVER_ART_DIM}px")
            val payload = encodeCoverArtWithWatchimage(raw, COVER_ART_DIM)
            val monoBytes = (COVER_ART_DIM / 8) * COVER_ART_DIM
            if (payload == null || (payload.size != monoBytes && payload.size != COVER_ART_DIM * COVER_ART_DIM)) {
                Log.w(TAG, "Artwork encode failed or wrong size: ${payload?.size}")
                transport.sendCoverArtClear(activeGeneration)
                return@launch
            }
            // Read activeGeneration fresh right before transmission (and again per chunk
            // below), not the value captured when this job started: the whole encode
            // pipeline can take a couple of seconds, long enough for several more watch
            // commands (each bumping the watch's own generation counter) to land first.
            // The watch drops any cover-art message whose generation doesn't match its
            // current one, so a stale captured value here silently loses the transfer -
            // this was actually happening (confirmed via `pebble logs`: zero "[CoverArt]
            // complete" lines despite the phone reporting every chunk delivered).
            Log.i(TAG, "Sending artwork to watch: ${payload.size} bytes")
            // The mediaId travels as the videoId here too (see sendStateSnapshot), so the
            // watch can match the cover to the track it is for instead of relying on the
            // generation counter this comment block describes.
            transport.sendCoverArtStart(
                COVER_ART_DIM,
                COVER_ART_DIM,
                payload.size,
                activeGeneration,
                browser.currentMediaItem?.mediaId.orEmpty(),
            )
            var sequence = 0
            var offset = 0
            while (offset < payload.size) {
                val end = minOf(offset + COVER_ART_CHUNK_BYTES, payload.size)
                val ok = transport.sendCoverArtChunk(payload.copyOfRange(offset, end), sequence, activeGeneration)
                if (!ok) {
                    Log.w(TAG, "Artwork chunk $sequence failed to deliver; aborting transfer")
                    return@launch
                }
                offset = end
                sequence++
            }
            lastSentArtworkUri = key
            Log.i(TAG, "Artwork transfer complete ($sequence chunks)")
        }
    }

    /** Symfonium's artwork URI is usually its own exported AutoArtContentProvider
     * (content://app.symfonik.music.player.aa/...) for Android Auto, occasionally a plain
     * http(s) URL - branch on scheme rather than assuming one or the other. */
    private suspend fun loadArtworkBytes(uri: Uri): ByteArray? = withContext(Dispatchers.IO) {
        runCatching {
            when (uri.scheme) {
                "http", "https" -> artHttpClient.newCall(Request.Builder().url(uri.toString()).get().build())
                    .execute().use { response -> if (response.isSuccessful) response.body.bytes() else null }
                else -> contentResolver.openInputStream(uri)?.use { it.readBytes() }
            }
        }.onFailure {
            Log.w(TAG, "Failed to load artwork from $uri", it)
        }.getOrNull()
    }

    private suspend fun encodeCoverArtWithWatchimage(raw: ByteArray, dim: Int): ByteArray? = withContext(Dispatchers.Default) {
        val normalized = normalizeImageForWatchimage(raw)
        val candidates = if (normalized != null && !normalized.contentEquals(raw)) listOf(normalized, raw) else listOf(raw)
        for (candidate in candidates) {
            val encoded = runCatching {
                if (watchTheme == ThemeMono) {
                    Watchimagebridge.encodeCoverMonoBytes(candidate, dim.toLong(), dim.toLong())
                } else {
                    Watchimagebridge.encodeCoverColorBytes(candidate, dim.toLong(), dim.toLong())
                }
            }.onFailure { Log.w(TAG, "watchimage encode failed", it) }.getOrNull()
            if (encoded != null) return@withContext encoded
        }
        null
    }

    private fun normalizeImageForWatchimage(raw: ByteArray): ByteArray? {
        val bitmap = BitmapFactory.decodeByteArray(raw, 0, raw.size) ?: return null
        return runCatching {
            java.io.ByteArrayOutputStream().use { out ->
                bitmap.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, out)
                out.toByteArray()
            }
        }.getOrNull().also { bitmap.recycle() }
    }

    private fun systemMediaVolumePercent(): Int {
        val max = audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC).coerceAtLeast(1)
        val current = audioManager.getStreamVolume(AudioManager.STREAM_MUSIC)
        return (current * 100 / max).coerceIn(0, 100)
    }

    private fun setSystemMediaVolumePercent(percent: Int) {
        val max = audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC).coerceAtLeast(1)
        val target = (percent.coerceIn(0, 100) * max + 50) / 100
        audioManager.setStreamVolume(AudioManager.STREAM_MUSIC, target.coerceIn(0, max), 0)
    }

    companion object {
        const val ACTION_LOCAL_BIND = "dev.pebble.musicbridge.SYMFONIUM_LOCAL_BIND"
        private const val TAG = "SymfoniumBridge"
        private const val SYMFONIUM_PACKAGE = "app.symfonik.music.player"
        private const val SYMFONIUM_SERVICE = "app.symfonik.core.playback.service.PlayerService"
        private const val BROWSE_RECENT = "browse_recent_played"
        private const val BROWSE_FAVORITE_SONGS = "browse_favorite_songs"
        private const val BROWSE_PLAYLISTS = "browse_playlist"
        private const val CONNECT_TIMEOUT_MS = 5_000L
        private const val LIBRARY_HARD_CAP = 40
        private const val MAX_QUEUE_ITEMS = 40
        private const val PREFERENCES_NAME = "dreamwave_symfonium"
        private const val KEY_THEME = "theme"
        private const val KEY_COVER_ART_BG = "cover_art_bg"
        // Always the original's "high" tier (144px): the low tier existed only to protect
        // BLE bandwidth for a concurrent watch-speaker audio stream, which this backend
        // never has (see maybeSendCoverArt's kdoc).
        private const val COVER_ART_DIM = 144
        private const val COVER_ART_CHUNK_BYTES = 512
        private const val COVER_ART_SETTLE_MS = 400L
        private const val LoopModeOff = 0
        private const val LoopModeOne = 1
        private const val LoopModeAll = 2
        private const val ThemeTeal = 0
        private const val ThemeDefault = 3
        private const val ThemeMono = 4
        private const val ThemeArcade = 5
    }
}

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
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.guava.await
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.withLock
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
class SymfoniumPlaybackService : Service(), MusicBackend {
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
    // Adopted from whatever the watch last sent, and echoed straight back in every
    // snapshot. Sending a constant 0 here (which this did) wedges the route: the watch
    // persists its own epoch across reboots and applies an incoming route only when the
    // epoch is *newer*, so once the user had ever toggled the route under the YouTube
    // backend, every "audio is on the phone" snapshot from here was silently dropped and
    // the watch sat on the Watch route waiting for a stream that never comes.
    private var activeRouteEpoch = 0
    private var watchTheme = ThemeDefault

    // Custom session actions Symfonium exposes for its notification/Auto surface
    // (Shuffle/Repeat/Favorite), keyed by their display name - see refreshCustomActions().
    // There is no standard Player.COMMAND_SET_SHUFFLE_MODE/SET_REPEAT_MODE support on
    // Symfonium's exported session (confirmed via dumpsys media_session), so toggling
    // these has to go through sendCustomCommand() instead of Player.setShuffleModeEnabled().
    private val customActions = mutableMapOf<String, SessionCommand>()

    // A second, legacy connection to the same Symfonium service, used *only* for its custom
    // actions.
    //
    // Media3's MediaBrowser does not expose them for this session: browser.customLayout is
    // empty at connect and stays empty once settled, and availableSessionCommands holds only
    // predefined codes with blank customAction strings. Verified on device - the actions are
    // plainly there in `dumpsys media_session` ("Favorite", "Repeat", "Shuffle"), they are
    // just carried in the legacy PlaybackStateCompat that Media3 does not surface here.
    //
    // So Shuffle and Repeat were unreachable in both directions: sending went through an
    // empty map and returned having done nothing, and reading had nothing to read. Every
    // toggle from the watch was a no-op. MediaControllerCompat is the API that does see
    // them, and androidx.media is already a dependency.
    private var browserCompat: android.support.v4.media.MediaBrowserCompat? = null
    private var controllerCompat: android.support.v4.media.session.MediaControllerCompat? = null
    // Action string -> the custom action to send, keyed by the action id Symfonium declares
    // (its name is the display label; the id is what sendCustomAction wants).
    private val legacyActions = mutableMapOf<String, String>()

    // Shuffle and repeat as read from the legacy custom actions' icons - see
    // observeLegacyActions(), which is the only place these are written. The last icon resource id seen for each is what a
    // change is measured against; null means we have not seen the action yet.
    private var shuffleIcon: Int? = null
    private var repeatIcon: Int? = null
    private var shuffleEnabled = false
    private var loopMode = LoopModeOff
    private var pendingShuffleToggle = false
    private var pendingRepeatToggle = false
    private var pendingShuffleUntilMs = 0L
    private var pendingRepeatUntilMs = 0L

    // Learned mapping from an action's icon resource id to the state that icon means.
    //
    // Symfonium publishes no shuffle or repeat state - only an icon that changes when the
    // state does (verified: Repeat's id moved 2131230853 -> 2131230851 across a change).
    // Which id means which state cannot be known up front, so it is *learned*: the first
    // time the icon moves, the id we were on meant the state we believed, and the id we
    // moved to means the state we moved to. One observation teaches both.
    //
    // Learning it is what makes the state survive. Without it the service could only ever
    // assume "off" on connect and flip from there, so shuffle already on in Symfonium read
    // as off on the watch, and - because Symfonium's session drops whenever it goes idle
    // and this service reconnects constantly - that wrong assumption was re-made every few
    // minutes, taking the watch's own persisted value with it. Persisted here so a phone
    // restart does not throw the lesson away.
    private val shuffleIconState = mutableMapOf<Int, Boolean>()
    private val repeatIconState = mutableMapOf<Int, Int>()

    // Start playlists shuffled. Owned by the watch (Settings -> Auto shuffle, shown only
    // under this source) and mirrored here so a playlist started while the watch is out
    // of range still honours it.
    private var autoShufflePlaylists = false

    // MediaItems returned by the last library/queue browse, keyed by mediaId, so a
    // subsequent CommandPlay/CommandQueueJump can hand the *actual* MediaItem back to the
    // controller - legacy-bridged playback needs the original item's extras, not a
    // synthetic MediaItem reconstructed from a bare id string.
    private val itemCache = mutableMapOf<String, MediaItem>()

    // The whole of the last paged library type, walked once when the watch opens the list
    // and then sliced for each page it asks for (see libraryAll). One type at a time: the
    // watch shows one library list at a time, so a second entry would only ever be a list
    // nobody is looking at.
    //
    // MediaItems rather than the SearchResultItems that go on the wire, because playing a
    // row needs the original item back (playMediaId cannot reconstruct one from an id).
    // They are converted - and so entered into itemCache - only for the rows actually
    // sent, which is what keeps that map growing with what the user scrolled past rather
    // than with the whole library the moment it is opened.
    private var libraryCacheType: Int? = null
    private var libraryCache: List<MediaItem> = emptyList()

    // Real Symfonium media id -> the short token we put on the wire in its place. Kept
    // stable per track (rather than minted per send) because the watch matches an incoming
    // cover art transfer against the id in the last state snapshot: two different tokens
    // for the same song would mean the art is never recognised as belonging to it.
    private val wireIds = mutableMapOf<String, String>()

    // The in-progress search's full ranked list, so the watch's later pages are slices of
    // one stable ranking.
    //
    // Keyed on the query and mode as well as the watch's request id, and all three matter.
    // The id alone is what the watch holds constant across the pages of one search - but it
    // is a counter that restarts at zero every time the watchapp launches, so after a
    // relaunch a brand new query arrives bearing an id this cache already knows. Keyed on
    // the id alone that was a hit: the watch got a page of the *previous* session's results
    // for a query it never asked about, and because a cache hit skips the search entirely
    // there was not even a log line to say so.
    private var pagedSearchKey: String? = null
    private var pagedSearchResults: List<SearchResultItem>? = null

    private val _uiState = MutableStateFlow(PlaybackUiState())

    /**
     * What Symfonium is playing, for the phone UI to display. Read-only: the transport
     * controls stay disabled for this source because Symfonium's own app owns them.
     */
    val uiState: StateFlow<PlaybackUiState> = _uiState.asStateFlow()

    private val artHttpClient = OkHttpClient()
    private var coverArtEnabled = false
    private var coverArtJob: Job? = null
    // Artwork identity is the TRACK, not the payload: Symfonium re-encodes the same cover
    // on every metadata republish (same byte length, different hash), so keying on the
    // payload re-streamed 20 KB over BLE on every pause/resume. The watch keeps showing
    // art until told otherwise, so one completed transfer per track is enough.
    private var lastSentArtworkTrack: String? = null
    // A transfer in flight, so a metadata burst mid-stream doesn't start a parallel
    // fetch/encode/send of the same image (lastSent only updates on completion).
    private var inflightArtworkTrack: String? = null

    override fun onCreate() {
        super.onCreate()
        Log.i(TAG, "Symfonium bridge starting")
        transport = PebbleAudioTransport(applicationContext, scope, Protocol.appUuid)
        // Dev probe, debug builds only: run a search over adb without the UI:
        //   am broadcast -a dev.pebble.musicbridge.DEBUG --es op search \
        //     --es query "sophie" --ei mode 0
        if (applicationInfo.flags and android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE != 0) {
            androidx.core.content.ContextCompat.registerReceiver(
                this,
                debugReceiver,
                android.content.IntentFilter("dev.pebble.musicbridge.DEBUG"),
                androidx.core.content.ContextCompat.RECEIVER_EXPORTED,
            )
        }
        getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE).run {
            watchTheme = getInt(KEY_THEME, ThemeDefault).coerceIn(ThemeTeal, ThemeDefaultDark)
            // Defaults on here (unlike PebblePlaybackService's off-by-default): that
            // default existed to protect BLE bandwidth for the watch-speaker audio
            // stream, which this backend never runs (see maybeSendCoverArt's kdoc).
            coverArtEnabled = getBoolean(KEY_COVER_ART_BG, true)
            autoShufflePlaylists = getBoolean(KEY_AUTO_SHUFFLE, false)
        }
        loadIconLessons()
        scope.launch { connect() }
        connectLegacy()
    }

    private suspend fun connect() {
        runCatching {
            val token = SessionToken(applicationContext, ComponentName(SYMFONIUM_PACKAGE, SYMFONIUM_SERVICE))
            val browser = MediaBrowser.Builder(applicationContext, token)
                .setListener(object : MediaBrowser.Listener {
                    override fun onDisconnected(controller: androidx.media3.session.MediaController) {
                        Log.w(TAG, "Symfonium session disconnected; reconnecting")
                        browserDeferred = CompletableDeferred()
                        // Release the dead controller before building its replacement.
                        // Symfonium's service stops itself when idle and takes the session
                        // with it, so this fires repeatedly in normal use - without the
                        // release each cycle leaked a MediaBrowser and its binder.
                        runCatching { controller.release() }
                        scope.launch { connect() }
                    }
                    override fun onCustomLayoutChanged(
                        controller: androidx.media3.session.MediaController,
                        layout: List<androidx.media3.session.CommandButton>,
                    ) {
                        cacheCustomActions(layout)
                    }
                    // No onSearchResultChanged override: this backend never calls
                    // MediaBrowser.search(), so it could never fire. See searchLibrary().
                })
                .buildAsync()
                .await()
            cacheCustomActions(browser.customLayout)
            browser.addListener(object : Player.Listener {
                override fun onMediaMetadataChanged(mediaMetadata: androidx.media3.common.MediaMetadata) {
                    Log.i(
                        TAG,
                        "onMediaMetadataChanged: title=${mediaMetadata.title} " +
                            "artUri=${mediaMetadata.artworkUri} artBytes=${mediaMetadata.artworkData?.size} " +
                            "coverArtEnabled=$coverArtEnabled lastSentTrack=$lastSentArtworkTrack",
                    )
                    scope.launch { sendStateSnapshot() }
                    maybeSendCoverArt(browser)
                }
                override fun onIsPlayingChanged(isPlaying: Boolean) {
                    refreshLegacyActions()
                    scope.launch { sendStateSnapshot() }
                }
                override fun onPlaybackStateChanged(playbackState: Int) {
                    refreshLegacyActions()
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
    /**
     * Opens the legacy connection whose only job is Symfonium's custom actions.
     *
     * Its PlaybackStateCompat is also the only place their state lives: each action carries
     * an icon resource id that changes when the state does, and nothing else says whether
     * shuffle is on. [observeLegacyActions] reads both from here.
     */
    private fun connectLegacy() {
        if (browserCompat != null) return
        val callbacks = object : android.support.v4.media.MediaBrowserCompat.ConnectionCallback() {
            override fun onConnected() {
                val token = browserCompat?.sessionToken ?: return
                val controller = runCatching {
                    android.support.v4.media.session.MediaControllerCompat(this@SymfoniumPlaybackService, token)
                }.getOrNull() ?: return
                controllerCompat = controller
                controller.registerCallback(object : android.support.v4.media.session.MediaControllerCompat.Callback() {
                    override fun onPlaybackStateChanged(state: android.support.v4.media.session.PlaybackStateCompat?) {
                        observeLegacyActions(state)
                    }
                    override fun onSessionDestroyed() {
                        Log.w(TAG, "Legacy session destroyed; reconnecting")
                        controllerCompat = null
                        browserCompat?.disconnect()
                        browserCompat = null
                        connectLegacy()
                    }
                })
                val initial = controller.playbackState
                observeLegacyActions(initial)
                Log.i(TAG, "Legacy controller connected (state=${initial != null} " +
                    "actions=${initial?.customActions?.size ?: -1})")
            }
            override fun onConnectionFailed() {
                Log.e(TAG, "Legacy MediaBrowserCompat connection failed")
                browserCompat = null
            }
        }
        browserCompat = android.support.v4.media.MediaBrowserCompat(
            this,
            ComponentName(SYMFONIUM_PACKAGE, SYMFONIUM_SERVICE),
            callbacks,
            null,
        ).also { runCatching { it.connect() }.onFailure { e -> Log.e(TAG, "Legacy connect threw", e) } }
    }

    /**
     * Reads the custom actions out of a legacy PlaybackState: what can be sent, and - from
     * each action's icon - what state it is in.
     *
     * The icon is the whole signal. Symfonium's action *names* are the bare, stable strings
     * "Favorite" / "Repeat" / "Shuffle" and its extras are null, so which resource id means
     * "on" cannot be known up front. It is learned the first time an icon moves: the id we
     * were on meant the state we believed, the id we moved to means the other. One
     * observation teaches both ends, and the pairs are persisted so a reconnect adopts the
     * real state instead of assuming.
     */
    // Re-read the actions from whatever the session is holding right now.
    //
    // Needed because onPlaybackStateChanged only fires on a *change*: connecting while
    // Symfonium sits paused and idle leaves playbackState null at connect and no callback
    // afterwards, so the action list stayed empty and every toggle found nothing to send.
    private fun refreshLegacyActions() {
        observeLegacyActions(controllerCompat?.playbackState)
    }

    private fun observeLegacyActions(state: android.support.v4.media.session.PlaybackStateCompat?) {
        val actions = state?.customActions ?: return
        if (actions.isEmpty()) return
        val now = System.currentTimeMillis()
        var learned = false
        val wasShuffle = shuffleEnabled
        val wasLoop = loopMode
        actions.forEach { action ->
            val name = action.name?.toString() ?: return@forEach
            legacyActions[name] = action.action
            val icon = action.icon
            when (name) {
                "Shuffle" -> {
                    val prevIcon = shuffleIcon
                    val changed = prevIcon != null && prevIcon != icon
                    if (pendingShuffleToggle && now <= pendingShuffleUntilMs && changed) {
                        // Authoritative correction path: this icon change follows our own
                        // toggle request, so the new state is exactly the inversion of what
                        // we had before the request. Use that to repair stale persisted icon
                        // lessons, which otherwise pin the watch to the wrong boolean.
                        shuffleEnabled = !wasShuffle
                        shuffleIconState[icon] = shuffleEnabled
                        shuffleIconState[prevIcon] = wasShuffle
                        learned = true
                        pendingShuffleToggle = false
                        pendingShuffleUntilMs = 0L
                        shuffleIcon = icon
                        return@forEach
                    }
                    val known = shuffleIconState[icon]
                    when {
                        known != null -> shuffleEnabled = known
                        // No absolute mapping for this icon yet. Do NOT infer by flipping on
                        // passive icon changes: Symfonium can republish actions across state
                        // churn and the icon id can change without a user toggle, which would
                        // falsely flip the watch state. We only infer from a change while a
                        // local Shuffle toggle is pending (handled above).
                        else -> {}
                    }
                    shuffleIcon = icon
                    if (pendingShuffleToggle && changed) {
                        pendingShuffleToggle = false
                        pendingShuffleUntilMs = 0L
                    }
                }
                "Repeat" -> {
                    val prevIcon = repeatIcon
                    val changed = prevIcon != null && prevIcon != icon
                    if (pendingRepeatToggle && now <= pendingRepeatUntilMs && changed) {
                        val next = when (wasLoop) {
                            LoopModeOff -> LoopModeAll
                            LoopModeAll -> LoopModeOne
                            else -> LoopModeOff
                        }
                        loopMode = next
                        repeatIconState[icon] = loopMode
                        repeatIconState[prevIcon] = wasLoop
                        learned = true
                        pendingRepeatToggle = false
                        pendingRepeatUntilMs = 0L
                        repeatIcon = icon
                        return@forEach
                    }
                    val known = repeatIconState[icon]
                    when {
                        known != null -> loopMode = known
                        // Same rule as shuffle above: no speculative state flips on passive
                        // icon changes. Repeat has seeded absolute ids; unknown ids are left
                        // unchanged unless they follow a local pending toggle (handled above).
                        else -> {}
                    }
                    repeatIcon = icon
                    if (pendingRepeatToggle && changed) {
                        pendingRepeatToggle = false
                        pendingRepeatUntilMs = 0L
                    }
                }
            }
        }
        // A request with no icon transition likely did nothing; clear so the next real
        // transition is not misattributed to an older request.
        if (pendingShuffleToggle && now > pendingShuffleUntilMs) {
            pendingShuffleToggle = false
            pendingShuffleUntilMs = 0L
        }
        if (pendingRepeatToggle && now > pendingRepeatUntilMs) {
            pendingRepeatToggle = false
            pendingRepeatUntilMs = 0L
        }
        if (learned) saveIconLessons()
        if (shuffleEnabled != wasShuffle || loopMode != wasLoop) {
            Log.i(TAG, "Toggles now shuffle=$shuffleEnabled loop=$loopMode (learned=$learned)")
            scope.launch { sendStateSnapshot() }
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

    /**
     * Stores what [observeLegacyActions] worked out, as "id:value" pairs.
     *
     * Icon resource ids are only stable for one installed build of Symfonium - an update can
     * renumber them - so a stale lesson is possible. It is self-correcting rather than sticky:
     * an unrecognised id simply is not in the map, and the next observed change relearns it.
     */
    private fun saveIconLessons() {
        getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE).edit()
            .putStringSet(KEY_SHUFFLE_ICONS, shuffleIconState.map { "${it.key}:${if (it.value) 1 else 0}" }.toSet())
            .putStringSet(KEY_REPEAT_ICONS, repeatIconState.map { "${it.key}:${it.value}" }.toSet())
            .apply()
    }

    private fun loadIconLessons() {
        val prefs = getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
        prefs.getStringSet(KEY_SHUFFLE_ICONS, emptySet()).orEmpty().forEach { entry ->
            val (id, value) = entry.split(':').let { it.getOrNull(0) to it.getOrNull(1) }
            val key = id?.toIntOrNull() ?: return@forEach
            shuffleIconState[key] = value == "1"
        }
        prefs.getStringSet(KEY_REPEAT_ICONS, emptySet()).orEmpty().forEach { entry ->
            val parts = entry.split(':')
            val key = parts.getOrNull(0)?.toIntOrNull() ?: return@forEach
            val mode = parts.getOrNull(1)?.toIntOrNull() ?: return@forEach
            repeatIconState[key] = mode
        }
        // After the persisted pairs, so the pinned mapping overrides any older, offset
        // lesson rather than being overwritten by it.
        repeatIconState.putAll(REPEAT_ICON_SEED)
        Log.i(TAG, "Icon lessons: shuffle=${shuffleIconState.size} repeat=${repeatIconState.size}")
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
        // The legacy connection is a second binding to Symfonium's service and leaks the
        // same way the Media3 one would if it were not released.
        runCatching { browserCompat?.disconnect() }
        browserCompat = null
        controllerCompat = null
        runCatching { scope.cancel() }
        super.onDestroy()
    }

    /**
     * Handing the watch over to the YouTube backend. Symfonium is a separate app the user
     * drives themselves, so this pauses rather than stops it, and only clears what we put
     * on the watch's screen.
     */
    override suspend fun onSourceDeactivated() {
        coverArtJob?.cancel()
        lastSentArtworkTrack = null
        inflightArtworkTrack = null
        if (browserDeferred.isCompleted) runCatching { browserDeferred.getCompleted().pause() }
        transport.sendCoverArtClear(activeGeneration)
    }

    override suspend fun announceSourceChange(source: Int, epoch: Int) {
        transport.sendSourceChanged(source, epoch)
    }

    override suspend fun onWatchMessage(data: PebbleDictionary): ReceiveResult {
        val command = (data[Protocol.keyCommand] as? PebbleDictionaryItem.Int32)?.value ?: return ReceiveResult.Nack
        Log.i(TAG, "Watch command received: $command")
        // The watch bumps its own generation counter locally on most button presses
        // (skip/pause/seek/etc.) before it even gets a reply, and silently drops any
        // state-snapshot/cover-art event whose generation doesn't match its current one.
        // Adopt whatever generation *any* incoming command carries, not just Play, so
        // outgoing events stay in step instead of echoing a stale value from the last play.
        (data[Protocol.keyGeneration] as? PebbleDictionaryItem.Int32)?.value?.let { activeGeneration = it }
        // Same reasoning for the route epoch - adopt whatever the watch is currently at so
        // the phone-route snapshots we send back are not discarded as stale.
        (data[Protocol.keyRouteEpoch] as? PebbleDictionaryItem.Int32)?.value?.let { activeRouteEpoch = it }
        val browser = awaitBrowser() ?: run {
            transport.sendEvent(Protocol.eventError, "Can't reach Symfonium")
            return ReceiveResult.Nack
        }
        when (command) {
            Protocol.commandHello -> {
                transport.sendEvent(Protocol.eventReady, "Symfonium bridge ready")
                sendStateSnapshot()
                // A freshly launched watchapp holds no cover art: its buffer is malloc'd
                // on demand and freed when the app exits. Nothing about the *track*
                // changes when the app is merely reopened, so the "already sent this one"
                // guard concluded the art had been delivered and this instance never got
                // one - which is precisely why art appeared only after skipping to the
                // next song. Hello is the watch telling us it has no state, so the record
                // of what it holds goes with it. (PebblePlaybackService clears
                // deliveredCover on Hello for exactly this reason.)
                maybeSendCoverArt(browser, force = true)
            }
            Protocol.commandRequestState -> sendStateSnapshot()
            Protocol.commandRequestLibrary -> {
                val libraryType = (data[Protocol.keyLibraryType] as? PebbleDictionaryItem.Int32)?.value
                    ?: return ReceiveResult.Nack
                val limit = (data[Protocol.keyLibraryLimit] as? PebbleDictionaryItem.Int32)?.value
                val offset = (data[Protocol.keyLibraryOffset] as? PebbleDictionaryItem.Int32)?.value
                sendLibrary(browser, libraryType, limit, offset)
            }
            Protocol.commandSearch -> {
                val requestId = (data[Protocol.keySearchRequestId] as? PebbleDictionaryItem.Int32)?.value ?: 0
                val query = (data[Protocol.keyQuery] as? PebbleDictionaryItem.Text)?.value
                val mode = (data[Protocol.keySearchMode] as? PebbleDictionaryItem.Int32)?.value
                    ?: Protocol.searchModeSong
                val limit = (data[Protocol.keySearchLimit] as? PebbleDictionaryItem.Int32)?.value
                // Absent (or 0) means the watch wants the head of the list, which is every
                // request an unpaged watch ever makes - so the old behaviour is the
                // offset-0 case of the new one rather than a separate path.
                val offset = (data[Protocol.keySearchOffset] as? PebbleDictionaryItem.Int32)?.value ?: 0
                when {
                    query.isNullOrBlank() -> transport.sendSearchError(requestId, "Nothing to search for")
                    // Song Radio generates an endless queue from a seed, which is a
                    // YouTube feature with no counterpart in a fixed library.
                    mode == Protocol.searchModeSongRadio ->
                        transport.sendSearchError(requestId, "Radio needs YouTube")
                    else -> searchLibrary(browser, query, requestId, offset, limit, mode)
                }
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
                // CRITICAL: this blob arrives constantly (on every snapshot), so the art
                // side effects must be gated on an actual CHANGE. The previous shape
                // reset lastSentArtworkTrack whenever the key was merely *present*,
                // forcing a 20 KB cover re-stream on every sync - which, mid-playback
                // and on each pause/resume, redrew the watch's art over and over.
                val autoShuffleConfig =
                    (data[Protocol.keyConfigSymfoniumAutoShuffle] as? PebbleDictionaryItem.Int32)?.value
                val newTheme = themeConfig?.coerceIn(ThemeTeal, ThemeDefaultDark)
                val newCoverArt = coverArtConfig?.let { it != 0 }
                // No change-gating needed here, unlike the art keys above: this only writes a
                // flag read at the next playlist start, so a redundant write costs nothing.
                val newAutoShuffle = autoShuffleConfig?.let { it != 0 }
                val themeChanged = newTheme != null && newTheme != watchTheme
                val coverArtToggled = newCoverArt != null && newCoverArt != coverArtEnabled
                if (newTheme != null || newCoverArt != null || newAutoShuffle != null) {
                    getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE).edit().apply {
                        newTheme?.let {
                            watchTheme = it
                            putInt(KEY_THEME, watchTheme)
                        }
                        newCoverArt?.let {
                            coverArtEnabled = it
                            putBoolean(KEY_COVER_ART_BG, coverArtEnabled)
                        }
                        newAutoShuffle?.let {
                            autoShufflePlaylists = it
                            putBoolean(KEY_AUTO_SHUFFLE, autoShufflePlaylists)
                        }
                    }.apply()
                }
                if (themeChanged || coverArtToggled) {
                    // A theme change re-encodes (mono vs color); a toggle on resumes art.
                    lastSentArtworkTrack = null
                    inflightArtworkTrack = null
                    val browser = if (browserDeferred.isCompleted) browserDeferred.getCompleted() else null
                    if (coverArtEnabled && browser != null) {
                        maybeSendCoverArt(browser)
                    } else {
                        coverArtJob?.cancel()
                        scope.launch { transport.sendCoverArtClear(activeGeneration) }
                    }
                }
            }
            else -> return ReceiveResult.Nack
        }
        return ReceiveResult.Ack
    }

    /**
     * Caches [item] and returns the id to send to the watch for it.
     *
     * The watch keeps ids in fixed 81-byte buffers (TEXT_LENGTH in its main.c) while
     * Symfonium's media ids are arbitrary strings. An over-long id arrives truncated, comes
     * back truncated on the next CommandPlay, misses [itemCache] and the song reads as
     * "no longer available" - so anything that would not survive the trip travels as a
     * short token instead. Every consumer treats the id as opaque, so a token works
     * wherever the real id would.
     */
    private fun wireIdFor(item: MediaItem): String {
        val id = item.mediaId
        val wireId = if (id.toByteArray().size <= MAX_WIRE_ID_BYTES) {
            id
        } else {
            wireIds.getOrPut(id) { "s:${wireIds.size}" }
        }
        itemCache[wireId] = item
        return wireId
    }

    /**
     * Fires one of Symfonium's custom actions (Shuffle / Repeat / Favorite).
     *
     * Goes through the legacy transport controls, not `browser.sendCustomCommand`. The
     * Media3 route looked right and did nothing: [customActions] is filled from
     * `browser.customLayout`, which is empty for this session, so every lookup missed and
     * every toggle returned having sent nothing. The action ids come from the legacy
     * PlaybackState instead - see [observeLegacyActions].
     */
    private fun invokeCustomAction(browser: MediaBrowser, displayName: String) {
        if (legacyActions.isEmpty()) refreshLegacyActions()
        val action = legacyActions[displayName]
        val controls = controllerCompat?.transportControls
        if (action == null || controls == null) {
            Log.w(TAG, "No '$displayName' action available (known=${legacyActions.keys}, " +
                "controller=${controllerCompat != null})")
            return
        }
        if (displayName == "Shuffle") {
            pendingShuffleToggle = true
            pendingShuffleUntilMs = System.currentTimeMillis() + TOGGLE_CONFIRM_WINDOW_MS
        }
        if (displayName == "Repeat") {
            pendingRepeatToggle = true
            pendingRepeatUntilMs = System.currentTimeMillis() + TOGGLE_CONFIRM_WINDOW_MS
        }
        controls.sendCustomAction(action, null)
    }

    /**
     * Plays a browsed item by mediaId. Songs (Recently Played/Favorites rows) are
     * playable directly. Playlist rows are browsable containers, not playable - Symfonium
     * exposes a synthetic "Play" pseudo-item as the first child of a playlist (see the
     * MediaBrowser spike), which is what actually starts the playlist; we resolve and
     * play that instead of the container itself.
     */
    /**
     * Plays a row the phone UI listed (search result, library entry, playlist). The id
     * is the wire id the row carried, resolved through the same [itemCache] the watch's
     * CommandPlay uses. Phone plays reuse [activeGeneration], the same rule the YouTube
     * backend's onUiCommand follows - bumping it would orphan in-flight cover art.
     */
    suspend fun playFromUi(mediaId: String) {
        val browser = awaitBrowser() ?: return
        playMediaId(browser, mediaId, activeGeneration)
    }

    private suspend fun playMediaId(browser: MediaBrowser, mediaId: String, generation: Int) {
        var item = itemCache[mediaId]
        if (item == null) {
            transport.sendEvent(Protocol.eventError, "Song no longer available", generation)
            return
        }
        if (item.mediaMetadata.isPlayable != true) {
            val children = runCatching { browser.getChildren(mediaId, 0, 10, null).await() }.getOrNull()
            children?.value?.forEach { itemCache[it.mediaId] = it }
            // Browsable nodes lead with pseudo actions - a playlist's "Play" (what we
            // want: it starts the whole list) or an album/artist's "Shuffle" (not what
            // a tap means: an album should start at track 1, in order). Shuffle rows
            // are skipped; the first real child wins.
            //
            // Unless Auto shuffle is on and this is a playlist, in which case the shuffle
            // pseudo-row is exactly what we want and we take it instead. Going through
            // Symfonium's own shuffle-play action rather than starting the list and then
            // toggling shuffle matters: the toggle is a blind flip (see invokeCustomAction
            // - there is no "set shuffle on", only "invert it"), so it would turn shuffle
            // *off* whenever it was already on, and it would race the play it follows.
            // Albums and artists are left alone - "auto shuffle playlists" is the setting.
            //
            // A playlist node is browse_playlist_action/<n> and leads with three pseudo-rows
            // - playlist/<n> "Play", playlist_shuffle/<n> "Shuffle", playlist_resume/<n>
            // "Resume" - before its songs. Note the shuffle row here is NOT one of the
            // "_random" ids the filter below skips; those are the album/artist form. Both
            // prefixes are verified against Symfonium's own browse tree, not guessed.
            val wantShuffle = autoShufflePlaylists && mediaId.startsWith(ID_PREFIX_PLAYLIST_NODE)
            val shuffleRow = if (!wantShuffle) null else children?.value?.firstOrNull {
                it.mediaMetadata.isPlayable == true && it.mediaId.startsWith(ID_PLAYLIST_SHUFFLE)
            }
            val firstPlayable = shuffleRow ?: children?.value?.firstOrNull {
                it.mediaMetadata.isPlayable == true && !it.mediaId.contains("_random")
            }
            if (firstPlayable == null) {
                transport.sendEvent(Protocol.eventError, "Nothing playable in that playlist", generation)
                return
            }
            if (shuffleRow != null) Log.i(TAG, "Auto shuffle: starting $mediaId via ${shuffleRow.mediaId}")
            item = firstPlayable
        }
        browser.setMediaItem(item)
        browser.prepare()
        browser.play()
    }

    /** Dev probe (debug builds): adb-triggered search/browse, logging what it finds. */
    private val debugReceiver = object : android.content.BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val op = intent.getStringExtra("op") ?: return
            scope.launch {
                val browser = awaitBrowser() ?: run {
                    Log.w(TAG, "[Probe] no browser")
                    return@launch
                }
                when (op) {
                    // Feasibility probe for deepening search past Symfonium's 15-song cap:
                    // takes the albums and artists the same search returned and walks them
                    // through browse (which DOES page), reporting how many unique songs
                    // that reaches versus the 15 the search alone gives.
                    "deep" -> {
                        val q = intent.getStringExtra("query").orEmpty()
                        val raw = compatSearch(q).orEmpty().filter { it.mediaId != null }
                        val direct = raw.filter { it.isPlayable && !it.isBrowsable }
                        val albums = raw.filter { it.mediaId!!.startsWith(ID_PREFIX_ALBUM) }
                        val artists = raw.filter { it.mediaId!!.startsWith(ID_PREFIX_ARTIST) }
                        val songs = linkedSetOf<String>()
                        direct.forEach { songs.add(it.mediaId!!) }
                        val fromDirect = songs.size
                        var albumCalls = 0
                        val started = System.currentTimeMillis()
                        for (a in albums) {
                            val kids = runCatching {
                                browser.getChildren(a.mediaId!!, 0, 200, null).await()
                            }.getOrNull()
                            albumCalls++
                            kids?.value?.filter {
                                it.mediaMetadata.isPlayable == true && !it.mediaId.contains("_random")
                            }?.forEach { songs.add(it.mediaId) }
                        }
                        Log.i(TAG, "[Probe] deep '$q': direct=$fromDirect albums=${albums.size} " +
                            "artists=${artists.size} -> ${songs.size} unique song(s) " +
                            "after $albumCalls album call(s) in ${System.currentTimeMillis() - started}ms")
                    }
                    // Raw source search, bypassing searchAndCollect's filtering, so the
                    // count logged is exactly what Symfonium handed over.
                    "rawsearch" -> {
                        val query = intent.getStringExtra("query").orEmpty()
                        val focus = intent.getBooleanExtra("focus", false)
                        val page = intent.getIntExtra("page", 0)
                        val raw = compatSearch(query, page, SEARCH_SOURCE_PAGE, focus)
                        val songs = raw?.count { it.isPlayable && !it.isBrowsable }
                        Log.i(TAG, "[Probe] rawsearch '$query' focus=$focus page=$page -> " +
                            "${raw?.size} item(s), $songs song(s)")
                    }
                    "search" -> {
                        val query = intent.getStringExtra("query").orEmpty()
                        val mode = intent.getIntExtra("mode", Protocol.searchModeSong)
                        // Defaults to the phone UI's cap; pass --ei limit to exercise the
                        // deeper budget the watch's Deep mode uses.
                        val cap = intent.getIntExtra("limit", UI_SEARCH_CAP)
                        val results = searchAndCollect(browser, query, cap, mode)
                        Log.i(TAG, "[Probe] search '$query' mode=$mode -> ${results.size} row(s)")
                        results.forEach { Log.i(TAG, "[Probe]   ${it.videoId} | '${it.title}' / '${it.artist}'") }
                    }
                    "children" -> {
                        val id = intent.getStringExtra("id").orEmpty()
                        // page/size are settable so browse pagination can be probed from
                        // adb without a rebuild - which is how we found out whether
                        // Symfonium honours it (its *search* ignores the same extras).
                        val page = intent.getIntExtra("page", 0)
                        val size = intent.getIntExtra("size", 10)
                        val kids = runCatching { browser.getChildren(id, page, size, null).await() }.getOrNull()
                        Log.i(TAG, "[Probe] children of $id page=$page size=$size -> " +
                            "code=${kids?.resultCode} count=${kids?.value?.size} " +
                            "first=${kids?.value?.firstOrNull()?.mediaMetadata?.title}")
                        kids?.value?.forEach {
                            Log.i(
                                TAG,
                                "[Probe]   ${it.mediaId} | '${it.mediaMetadata.title}' " +
                                    "browse=${it.mediaMetadata.isBrowsable} play=${it.mediaMetadata.isPlayable}",
                            )
                        }
                    }
                    // Enumerates the browse tree from its real root rather than from
                    // guessed ids - which is how we established what Symfonium actually
                    // exposes to Android Auto, as opposed to what we assumed it did.
                    // Is Media3's customLayout ever populated for this legacy session?
                    // At connect it is empty, which makes every custom action unreachable.
                    "layout" -> {
                        Log.i(TAG, "[Probe] customLayout size=${browser.customLayout.size}")
                        browser.customLayout.forEachIndexed { i, b ->
                            Log.i(TAG, "[Probe]   [$i] '${b.displayName}' icon=${b.iconResId} " +
                                "cmd=${b.sessionCommand?.customAction} enabled=${b.isEnabled}")
                        }
                        Log.i(TAG, "[Probe] availableSessionCommands=" +
                            browser.availableSessionCommands.commands.size)
                        browser.availableSessionCommands.commands.take(20).forEach {
                            Log.i(TAG, "[Probe]   cmd '${it.customAction}'")
                        }
                    }
                    // Fire a custom action by name and watch the state come back:
                    //   am broadcast -a dev.pebble.musicbridge.DEBUG -p dev.pebble.musicbridge                     //     --es op action --es name Shuffle
                    "action" -> {
                        val name = intent.getStringExtra("name") ?: "Shuffle"
                        Log.i(TAG, "[Probe] known=${legacyActions.keys} " +
                            "controller=${controllerCompat != null} " +
                            "shuffle=$shuffleEnabled loop=$loopMode")
                        // Does the legacy session expose absolute state, rather than only
                        // the icon that changes with it?
                        val c = controllerCompat
                        Log.i(TAG, "[Probe] legacy repeatMode=${c?.repeatMode} " +
                            "shuffleMode=${c?.shuffleMode}")
                        c?.playbackState?.customActions?.forEach { a ->
                            Log.i(TAG, "[Probe]   action '${a.name}' icon=${a.icon} " +
                                "extras=${a.extras?.keySet()?.joinToString()}")
                        }
                        invokeCustomAction(browser, name)
                    }
                    // What the watch would actually be shown for a library row:
                    //   --es op library --ei type 0 --ei limit 10   (0 = Recently Played)
                    "library" -> {
                        val type = intent.getIntExtra("type", Protocol.libraryRecent)
                        val limit = intent.getIntExtra("limit", 10)
                        val rows = browseLibrary(browser, type, limit)
                        Log.i(TAG, "[Probe] library type=$type -> ${rows.size} rows")
                        rows.forEach { Log.i(TAG, "[Probe]   '${it.title}' - '${it.artist}'") }
                    }
                    // What the session reports as current - chiefly which mediaId form a
                    // *played* track carries, since the played-songs ledger has to match
                    // the "album_song/" ids the expansion produces.
                    "now" -> {
                        val item = browser.currentMediaItem
                        Log.i(TAG, "[Probe] now id='${item?.mediaId}' " +
                            "title='${item?.mediaMetadata?.title}' " +
                            "artist='${item?.mediaMetadata?.artist}' " +
                            "album='${item?.mediaMetadata?.albumTitle}'")
                    }
                    "root" -> {
                        val root = runCatching { browser.getLibraryRoot(null).await() }.getOrNull()
                        val rootId = root?.value?.mediaId
                        Log.i(TAG, "[Probe] root id='$rootId' code=${root?.resultCode}")
                        if (rootId != null) {
                            val kids = runCatching { browser.getChildren(rootId, 0, 100, null).await() }.getOrNull()
                            Log.i(TAG, "[Probe] root children=${kids?.value?.size}")
                            kids?.value?.forEach {
                                Log.i(TAG, "[Probe]   '${it.mediaId}' | ${it.mediaMetadata.title} " +
                                    "browse=${it.mediaMetadata.isBrowsable} play=${it.mediaMetadata.isPlayable}")
                            }
                        }
                    }
                    else -> Log.w(TAG, "[Probe] unknown op '$op'")
                }
            }
        }
    }

    /**
     * Searches Symfonium's library through a second, plain-compat client and reports
     * its ranked results. Playback is never touched: the list arrives, the user picks,
     * and only the pick plays.
     *
     * Why a separate `MediaBrowserCompat` (androidx.media) rather than the Media3
     * browser this service uses for everything else:
     *
     *  - Media3's `MediaBrowser.search()` crashes Symfonium: its legacy bridge parcels
     *    the callback as `android.support.v4.os.ResultReceiver`, a class Symfonium does
     *    not ship, and unmarshalling it kills Symfonium's main thread (confirmed on
     *    device: FATAL BadParcelableException in app.symfonik.music.player).
     *  - androidx.media's `MediaBrowserCompat.search()` parcels `androidx.core.os`
     *    classes instead, which every modern app carries - so the same legacy service
     *    answers happily. Confirmed on device: ranked results in ~200ms.
     *
     * Symfonium itself searches its full offline copy of the library (it is an
     * offline-first client of the media server), so this needs no network and no
     * walking of the browse tree.
     */
    private suspend fun searchLibrary(
        browser: MediaBrowser,
        query: String,
        requestId: Int,
        offset: Int,
        requestedLimit: Int?,
        mode: Int,
    ) {
        val pageSize = (requestedLimit ?: SEARCH_PAGE_CAP).coerceIn(1, SEARCH_PAGE_CAP)
        // The full ranked list for this search, not just this page: Symfonium answers a
        // search in one shot (there is no cursor to resume from), so a page is a slice of
        // a list we hold rather than a deeper query. Held against the request id so page 2
        // costs nothing and - more importantly - comes from the same ranking page 1 did.
        // Re-running the search per page would let the library shifting underneath
        // duplicate or skip rows across a page boundary.
        val key = "$requestId|$mode|$query"
        val cached = pagedSearchResults?.takeIf { pagedSearchKey == key }
        val all = cached ?: searchAndCollect(browser, query, SEARCH_COLLECT_CAP, mode).also {
            pagedSearchKey = key
            pagedSearchResults = it
        }
        if (all.isEmpty()) {
            transport.sendSearchError(requestId, "No matches in Symfonium")
            return
        }
        val start = offset.coerceIn(0, all.size)
        val page = all.drop(start).take(pageSize)
        // A page starting past the end is not an error - it is the watch asking for one
        // row more than exists, which the total in the completion below settles.
        page.forEachIndexed { index, item ->
            // Global index, not a position within the page: the watch places each row at
            // its absolute position in a list it only partly holds.
            transport.sendSearchResult(start + index, item.videoId, item.title, item.artist, requestId)
        }
        Log.i(TAG, "Search page '$query' mode=$mode ${start}..${start + page.size} of ${all.size}")
        transport.sendSearchComplete(requestId, all.size)
    }

    /** The phone UI's search: same as the watch's, with a deeper page. */
    suspend fun searchForUi(
        query: String,
        requestedLimit: Int? = null,
        searchMode: Int = Protocol.searchModeSong,
    ): List<SearchResultItem> {
        val browser = awaitBrowser() ?: return emptyList()
        val limit = (requestedLimit ?: UI_SEARCH_CAP).coerceIn(1, UI_SEARCH_CAP)
        return searchAndCollect(browser, query, limit, searchMode)
    }

    private suspend fun searchAndCollect(
        browser: MediaBrowser,
        query: String,
        limit: Int,
        mode: Int,
    ): List<SearchResultItem> {
        // Symfonium's search answers all three kinds in one ranked list - observed on
        // device: song/<id> hits, then browse_album_songs/<id> albums, then
        // browse_albums_artists/<id> artists. Modes just pick their slice out of it.
        // (The browse tree itself is a small curated subset - 28 albums on a 10k-song
        // library - so filtering the tree instead would miss nearly everything.)
        // One call, because there is provably no second page to fetch. Symfonium answers
        // every search with a fixed 15 items per category - 45 in total, songs then albums
        // then artists - and nothing on this side changes that. Established against
        // Symfonium's own debug log (its Settings > debug mode, which writes
        // /sdcard/Android/data/app.symfonik.music.player/files/debug.log):
        //
        //  - It RECEIVES our pagination extras and ignores them. Its log echoes
        //    "PAGE_SIZE : 100, PAGE : 0" and still returns 45; EXTRA_PAGE=1 returns
        //    byte-identical results to page 0.
        //  - It is not short of data. For query "a" it logged
        //    "SELECT ... FROM songs WHERE title LIKE '%a%' ... [4631 in 20ms]" - no SQL
        //    LIMIT at all - then truncated to 15 while building the Auto response.
        //  - A media-focus hint does not help either: EXTRA_MEDIA_FOCUS moves its
        //    queryType from 2 to 6 and it parses the structured "song=" field, and the
        //    answer is still 15.
        //
        // So the cap lives in Symfonium's Auto response builder and only Symfonium can
        // lift it. Its *browse* nodes page correctly (getChildren honours page/pageSize -
        // verified: favourite tracks returns 268 and pages), which is the only place
        // walking deeper is worth doing.
        val items = compatSearch(query).orEmpty().filter { it.mediaId != null }
        if (items.isEmpty()) {
            Log.i(TAG, "Search '$query' -> no matches")
            return emptyList()
        }
        val filtered = when (mode) {
            Protocol.searchModeAlbum ->
                items.filter { it.mediaId!!.startsWith(ID_PREFIX_ALBUM) }
            Protocol.searchModeArtist ->
                items.filter { it.mediaId!!.startsWith(ID_PREFIX_ARTIST) }
            else -> items.filter { it.isPlayable && !it.isBrowsable }
        }
        Log.i(TAG, "Search '$query' mode=$mode -> ${filtered.size} of ${items.size} item(s)")
        val head = filtered.take(limit).map { it.toSearchResult() }
        // Songs only, and only when the caller wants more rows than the search gave: the
        // 15-song answer is all Symfonium's search will ever produce, but the albums it
        // matched in the same breath are browsable, and browse is not capped. Walking them
        // roughly triples the reachable songs (measured: "da" 15 -> 42, "summer" 13 -> 36)
        // for ~10ms per album, because these are local database reads.
        //
        // Self-limiting by construction: with Results at 5 or 10 the direct hits already
        // fill the request and nothing is walked at all.
        if (mode != Protocol.searchModeSong || head.size >= limit) return head
        val seen = head.map { it.videoId }.toMutableSet()
        val deeper = mutableListOf<SearchResultItem>()
        deeper += expandAlbums(browser, items, limit - head.size, seen)
        deeper.forEach { seen.add(it.videoId) }
        // Artists last, and only if albums did not fill the request. An artist match is
        // the loosest kind - "A Boogie Wit da Hoodie" matches "da" and none of their song
        // titles need contain it - so these belong at the bottom of the list, where paging
        // means they cost nothing until someone scrolls that far.
        if (head.size + deeper.size < limit) {
            deeper += expandArtists(browser, items, limit - head.size - deeper.size, seen)
        }
        return head + deeper
    }

    /**
     * One browse read, bounded. Every deepening call goes through here because they are
     * unbounded otherwise: `getChildren(...).await()` has no timeout, so a single call
     * Symfonium never answers hangs the whole search - and a search that never returns
     * leaves the watch on its Searching screen with no results and no error. Found the
     * hard way: album expansion (15 calls) was fine, artist expansion (many more) stalled.
     */
    private suspend fun childrenOf(browser: MediaBrowser, id: String): List<MediaItem> =
        withTimeoutOrNull(BROWSE_TIMEOUT_MS) {
            runCatching { browser.getChildren(id, 0, ALBUM_TRACK_CAP, null).await() }
                .getOrNull()?.value
        }.orEmpty()

    /**
     * A browsed track turned into a row, with the two things browse rows get wrong for a
     * flat list.
     *
     * Symfonium formats an album's children for a car list, where the album is already the
     * heading: the title carries a track-number prefix ("1 • ONE MORE TIME") and the artist
     * is blank because the context implies it. Dropped into search results those rows read
     * as numbered gibberish with no artist, which is exactly what they looked like on the
     * watch. So the prefix comes off, and [fallbackArtist] - the album's or artist's own
     * name, which is the context being lost - fills the empty field.
     */
    private fun browsedTrackRow(track: MediaItem, fallbackArtist: String): SearchResultItem {
        val rawTitle = track.mediaMetadata.title?.toString().orEmpty()
        val title = rawTitle.replace(TRACK_NUMBER_PREFIX, "").trim()
        val artist = track.mediaMetadata.artist?.toString()?.takeIf { it.isNotBlank() }
            ?: fallbackArtist.takeIf { it.isNotBlank() }
            ?: track.mediaMetadata.albumTitle?.toString().orEmpty()
        return SearchResultItem(
            videoId = wireIdFor(track),
            title = title.ifBlank { "Unknown title" },
            artist = artist,
        )
    }

    /**
     * Songs reached by walking the albums a search matched, in match order, skipping any
     * already in [have]. The albums are the search's own album hits - so these are songs
     * from records whose title or artist matched, which is the same relevance the search
     * was working from, one level down.
     */
    private suspend fun expandAlbums(
        browser: MediaBrowser,
        items: List<android.support.v4.media.MediaBrowserCompat.MediaItem>,
        want: Int,
        have: Set<String>,
    ): List<SearchResultItem> {
        val out = mutableListOf<SearchResultItem>()
        val seen = have.toMutableSet()
        val deadline = System.currentTimeMillis() + EXPAND_DEADLINE_MS
        for (album in items.filter { it.mediaId!!.startsWith(ID_PREFIX_ALBUM) }) {
            if (out.size >= want || System.currentTimeMillis() > deadline) break
            val children = childrenOf(browser, album.mediaId!!)
            for (track in children) {
                if (out.size >= want) break
                // Albums lead with a "Shuffle" pseudo-row, which is an action, not a song.
                if (track.mediaMetadata.isPlayable != true || track.mediaId.contains("_random")) continue
                val wireId = wireIdFor(track)
                if (!seen.add(wireId)) continue
                // The album row's own subtitle is its artist, which is the context these
                // children are stripped of.
                out += browsedTrackRow(track, album.description.subtitle?.toString().orEmpty())
            }
        }
        if (out.isNotEmpty()) Log.i(TAG, "Search expanded albums -> ${out.size} extra song(s)")
        return out
    }

    /**
     * Songs reached through the artists a search matched: artist -> their albums -> tracks.
     * Two levels rather than one, so it is bounded by [ARTIST_EXPAND_MAX] artists as well
     * as by [want] - an artist node also carries "All albums" and "Shuffle" pseudo-rows,
     * which are actions rather than records and are skipped.
     */
    private suspend fun expandArtists(
        browser: MediaBrowser,
        items: List<android.support.v4.media.MediaBrowserCompat.MediaItem>,
        want: Int,
        have: Set<String>,
    ): List<SearchResultItem> {
        val out = mutableListOf<SearchResultItem>()
        val seen = have.toMutableSet()
        val deadline = System.currentTimeMillis() + EXPAND_DEADLINE_MS
        val artists = items.filter { it.mediaId!!.startsWith(ID_PREFIX_ARTIST) }.take(ARTIST_EXPAND_MAX)
        for (artist in artists) {
            if (out.size >= want || System.currentTimeMillis() > deadline) break
            val albums = childrenOf(browser, artist.mediaId!!)
                .filter { it.mediaId.startsWith(ID_PREFIX_ALBUM) }
            for (album in albums) {
                if (out.size >= want || System.currentTimeMillis() > deadline) break
                val tracks = childrenOf(browser, album.mediaId)
                for (track in tracks) {
                    if (out.size >= want) break
                    if (track.mediaMetadata.isPlayable != true || track.mediaId.contains("_random")) continue
                    val wireId = wireIdFor(track)
                    if (!seen.add(wireId)) continue
                    // Walked from the artist down, so the artist's own name is the fallback.
                    out += browsedTrackRow(track, artist.description.title?.toString().orEmpty())
                }
            }
        }
        if (out.isNotEmpty()) Log.i(TAG, "Search expanded artists -> ${out.size} extra song(s)")
        return out
    }

    /**
     * Runs [query] through Symfonium's own search over a short-lived
     * `MediaBrowserCompat` connection (connect+search measured at ~200ms, so no
     * persistent second client is worth babysitting). Null on timeout/error.
     *
     * Serialized, deadlined and unconditionally disconnected, all for the same reason:
     * these connections used to leak. The connect had no timeout at all, so a connect
     * Symfonium never completed hung the caller forever; and `disconnect()` sat on the
     * happy path, so a search that timed out - or a coroutine cancelled while searching -
     * walked away from a live connection. Observed on device: after a few such calls
     * Symfonium stopped completing new connections entirely, and every later search hung
     * before it could log a thing. The mutex then keeps concurrent callers (the watch and
     * the phone UI both search) from opening several at once.
     */
    private val compatSearchLock = kotlinx.coroutines.sync.Mutex()

    private suspend fun compatSearch(
        query: String,
        page: Int = 0,
        pageSize: Int = SEARCH_SOURCE_PAGE,
        songFocus: Boolean = false,
    ): MutableList<android.support.v4.media.MediaBrowserCompat.MediaItem>? = compatSearchLock.withLock {
        val ref = java.util.concurrent.atomic.AtomicReference<android.support.v4.media.MediaBrowserCompat?>()
        try {
            val compat = withTimeoutOrNull(CONNECT_TIMEOUT_MS) {
                kotlinx.coroutines.suspendCancellableCoroutine<android.support.v4.media.MediaBrowserCompat?> { cont ->
            val b = android.support.v4.media.MediaBrowserCompat(
                this@SymfoniumPlaybackService,
                ComponentName(SYMFONIUM_PACKAGE, SYMFONIUM_SERVICE),
                object : android.support.v4.media.MediaBrowserCompat.ConnectionCallback() {
                    override fun onConnected() {
                        cont.resume(ref.get(), null)
                    }
                    override fun onConnectionFailed() {
                        cont.resume(null, null)
                    }
                    override fun onConnectionSuspended() = Unit
                },
                null,
            )
            ref.set(b)
            runCatching { b.connect() }.onFailure { cont.resume(null, null) }
        }
        } ?: return@withLock null
        val results = withTimeoutOrNull(SEARCH_TIMEOUT_MS) {
            kotlinx.coroutines.suspendCancellableCoroutine<MutableList<android.support.v4.media.MediaBrowserCompat.MediaItem>?> { cont ->
                compat.search(
                    query,
                    // The standard browse-pagination extras. Symfonium answers a search
                    // with a fixed 15 items per category otherwise, which is the whole
                    // reason a 10k-song library returns fifteen songs however broad the
                    // query. Honoured or ignored is up to Symfonium - searchAndCollect
                    // detects a repeat page and stops, so passing them is safe either way.
                    Bundle().apply {
                        putInt(android.support.v4.media.MediaBrowserCompat.EXTRA_PAGE, page)
                        putInt(android.support.v4.media.MediaBrowserCompat.EXTRA_PAGE_SIZE, pageSize)
                        if (songFocus) {
                            // Voice-assistant style hint: "this is a song search", which is
                            // what Symfonium's onSearch logs as queryType. Worth a try
                            // because a song-focused query need not split its budget
                            // across songs/albums/artists.
                            putString(
                                android.provider.MediaStore.EXTRA_MEDIA_FOCUS,
                                android.provider.MediaStore.Audio.Media.ENTRY_CONTENT_TYPE,
                            )
                            putString(android.provider.MediaStore.EXTRA_MEDIA_TITLE, query)
                        }
                    },
                    object : android.support.v4.media.MediaBrowserCompat.SearchCallback() {
                        override fun onSearchResult(
                            query: String,
                            extras: Bundle?,
                            items: MutableList<android.support.v4.media.MediaBrowserCompat.MediaItem>,
                        ) {
                            cont.resume(items, null)
                        }
                        override fun onError(query: String, extras: Bundle?) {
                            cont.resume(null, null)
                        }
                    },
                )
            }
        }
        return@withLock results
        } finally {
            // The whole point: whatever happened above - connect timeout, search timeout,
            // an exception, or this coroutine being cancelled mid-search - the connection
            // is handed back. ref, not the local, because a connect that timed out never
            // produced a local to disconnect.
            runCatching { ref.get()?.disconnect() }
        }
    }

    /**
     * Maps a compat search hit onto a row, with a synthetic MediaItem over the hit's
     * real id going into the play cache. Playing it resolves to playFromMediaId on the
     * legacy session - the same call an Android Auto row tap makes.
     *
     * The id is deliberately NOT enriched through Media3's `getItem()`: the legacy
     * bridge parcels that callback as `android.support.v4.os.ResultReceiver`, which
     * crashes Symfonium's main thread exactly like Media3's search() did (confirmed on
     * device, 3ms after a successful search). Browse results keep their real MediaItems;
     * search hits are the one place a synthetic is the only safe option.
     */
    private fun android.support.v4.media.MediaBrowserCompat.MediaItem.toSearchResult(): SearchResultItem {
        val id = mediaId!!
        val title = description.title?.toString() ?: "Unknown title"
        val artist = description.subtitle?.toString().orEmpty()
        // The compat hit's browsable/playable flags must survive into the synthetic
        // item: playMediaId keys playlist/album/artist resolution off isPlayable, and
        // forcing playable here would skip that and try to play a browse node bare.
        val item = MediaItem.Builder()
            .setMediaId(id)
            .setMediaMetadata(
                androidx.media3.common.MediaMetadata.Builder()
                    .setTitle(title)
                    .setArtist(artist)
                    .setIsBrowsable(isBrowsable)
                    .setIsPlayable(isPlayable)
                    .build(),
            )
            .build()
        return SearchResultItem(videoId = wireIdFor(item), title = title, artist = artist)
    }

    /**
     * Answers a library request. [offset] present means the watch is reading this type
     * through a sliding window and wants the page starting at that global index; absent
     * means the old shape - one call, one whole (capped) list - which is still what every
     * type but Favorites asks for.
     */
    private suspend fun sendLibrary(
        browser: MediaBrowser,
        libraryType: Int,
        requestedLimit: Int?,
        offset: Int?,
    ) {
        if (offset == null) {
            val limit = (requestedLimit ?: LIBRARY_HARD_CAP).coerceIn(1, LIBRARY_HARD_CAP)
            browseLibrary(browser, libraryType, limit).forEachIndexed { index, item ->
                transport.sendLibraryItem(libraryType, index, item.videoId, item.title, item.artist)
            }
            transport.sendLibraryComplete(libraryType)
            return
        }
        // Page 0 is the watch opening the list, which is the only honest moment to go and
        // read it again - refreshing under a window the user is halfway down would shift
        // rows out from under the selection.
        val all = libraryAll(browser, libraryType, rebuild = offset <= 0)
        val pageSize = (requestedLimit ?: LIBRARY_PAGE_SIZE).coerceIn(1, LIBRARY_PAGE_SIZE)
        val start = offset.coerceIn(0, all.size)
        val end = (start + pageSize).coerceAtMost(all.size)
        for (i in start until end) {
            val row = all[i].toLibraryItem()
            transport.sendLibraryItem(libraryType, i, row.videoId, row.title, row.artist)
        }
        Log.i(TAG, "Library page type=$libraryType $start..${end - 1} of ${all.size}")
        transport.sendLibraryComplete(libraryType, total = all.size)
    }

    /** The same browse the watch gets, returned as data for the phone's Library screen. */
    suspend fun libraryForUi(libraryType: Int, requestedLimit: Int? = null): List<SearchResultItem> {
        val browser = awaitBrowser() ?: return emptyList()
        val limit = (requestedLimit ?: LIBRARY_HARD_CAP).coerceIn(1, LIBRARY_HARD_CAP)
        return browseLibrary(browser, libraryType, limit)
    }

    // Cached / Continue / Recent Searches are YouTube-backend concepts with nothing behind
    // them here; the watch hides those rows for this source, and null covers the case
    // where either end asks anyway.
    private fun browseIdFor(libraryType: Int): String? = when (libraryType) {
        Protocol.libraryRecent -> BROWSE_RECENT
        Protocol.libraryFavorites -> BROWSE_FAVORITE_SONGS
        Protocol.libraryPlaylists -> BROWSE_PLAYLISTS
        else -> null
    }

    private fun MediaItem.toLibraryItem() = SearchResultItem(
        videoId = wireIdFor(this),
        title = mediaMetadata.title?.toString() ?: "Unknown title",
        artist = mediaMetadata.artist?.toString() ?: mediaMetadata.subtitle?.toString().orEmpty(),
    )

    /**
     * A user-created Symfonium smart playlist found by title under the playlists node.
     * Recently Played and Most Played are both backed this way: Symfonium maintains the
     * playlist's membership and order itself, so its children are real `song/<id>` items
     * with canonical ids that play through the normal itemCache path - no bookkeeping
     * of our own. The match is loose about anything around the phrase ("Recently played
     * songs", "Most Played (100)", ...), and the same regex hides the playlist from the
     * Playlists list so the rows do not appear twice (see asLibraryRows).
     */
    private suspend fun findSmartPlaylist(browser: MediaBrowser, title: Regex): MediaItem? =
        childrenOf(browser, BROWSE_PLAYLISTS).firstOrNull {
            it.mediaId.startsWith(ID_PREFIX_PLAYLIST_NODE) &&
                title.containsMatchIn(it.mediaMetadata.title?.toString().orEmpty())
        }

    /**
     * The songs of a smart playlist found by title, or null when no playlist matches.
     * Children lead with the pseudo-action rows every playlist has ("Play" / "Shuffle"
     * / "Resume" - see playMediaId), then the songs themselves.
     */
    private suspend fun smartPlaylistSongs(
        browser: MediaBrowser,
        title: Regex,
        limit: Int,
        logName: String,
    ): List<SearchResultItem>? {
        val playlist = findSmartPlaylist(browser, title) ?: return null
        val rows = childrenOf(browser, playlist.mediaId)
            .filter { it.mediaId.startsWith(ID_PREFIX_SONG) }
            .take(limit)
            .map { it.toLibraryItem() }
        Log.i(TAG, "$logName: playlist '${playlist.mediaMetadata.title}' -> ${rows.size} songs")
        return rows
    }

    /**
     * Recently Played, as songs.
     *
     * Primary source: the user's "recently played" smart playlist, which is exactly
     * this list maintained by Symfonium itself, in play order.
     *
     * Fallback, when no such playlist exists or it yields nothing: the album walk.
     * Symfonium's browse tree has no recently-played-songs node. `browse_recent_played`
     * returns `recent_album/<id>` rows — albums — and every plausible variant of that id
     * (`_songs`, `_tracks`) returns the identical list, because the suffix is ignored.
     * Those rows are playable leaves with no children, so they cannot be expanded in place
     * either. All verified against the device, not guessed: an earlier attempt "fixed" this
     * by filtering out browsable rows, which did nothing at all — the albums are marked
     * `isPlayable=true, isBrowsable=false` and sailed straight through.
     *
     * What does work is that the numeric id is shared: `recent_album/63337` and
     * `browse_album_songs/63337` are the same album, and the latter lists its tracks. So
     * the walk expands the recent albums newest first — "songs from recently played
     * albums", in album order, not the songs themselves in play order.
     *
     * (An intermediate design kept its own played-songs ledger, recorded off the player
     * listener. It died on two verified facts: the playback queue's mediaIds are
     * context-scoped — `current_playlist/<n>`, re-used for consecutive tracks — so they
     * can never be matched to browse-tree ids, and re-resolving them needs `getItem()`,
     * which crashes Symfonium the same way Media3 `search()` does (see [searchLibrary]).
     * The smart playlist is Symfonium doing the same job with canonical ids, for free.)
     */
    private suspend fun recentSongs(browser: MediaBrowser, limit: Int): List<SearchResultItem> {
        smartPlaylistSongs(browser, RECENT_PLAYED_TITLE, limit, "Recently played")
            ?.takeIf { it.isNotEmpty() }
            ?.let { return it }
        val albums = childrenOf(browser, BROWSE_RECENT)
        val out = mutableListOf<SearchResultItem>()
        val deadline = System.currentTimeMillis() + EXPAND_DEADLINE_MS
        for (album in albums.take(RECENT_ALBUM_SCAN)) {
            if (out.size >= limit || System.currentTimeMillis() > deadline) break
            val numericId = album.mediaId.substringAfterLast('/')
            if (numericId.isEmpty()) continue
            val albumTitle = album.mediaMetadata.title?.toString().orEmpty()
            for (track in childrenOf(browser, ID_PREFIX_ALBUM + numericId)) {
                if (out.size >= limit) break
                // Skips the album_random/ "Shuffle" pseudo-row each album leads with.
                if (!track.mediaId.startsWith(ID_PREFIX_ALBUM_SONG)) continue
                itemCache[track.mediaId] = track
                // browsedTrackRow, not a plain conversion. Album tracks come with the track
                // number on the title and *no artist at all* - the album is the heading in
                // the car list Symfonium formats these for. Converting them directly fell
                // back to the subtitle for the artist field, and Symfonium puts the track
                // duration there, so every row read "3:41" where the artist belongs. This
                // helper strips the number and fills the artist with the album name.
                out += browsedTrackRow(track, albumTitle)
            }
        }
        Log.i(TAG, "Recently played: ${albums.size} albums -> ${out.size} songs")
        return out
    }

    private suspend fun browseLibrary(
        browser: MediaBrowser,
        libraryType: Int,
        limit: Int,
    ): List<SearchResultItem> {
        // Recently Played is not a plain browse - see recentSongs() for why it has to
        // expand albums to reach songs at all.
        if (libraryType == Protocol.libraryRecent) {
            return recentSongs(browser, limit)
        }
        // Most Played is smart-playlist-backed only: Symfonium's browse tree has no
        // most-played-songs node at all, so without the playlist there is nothing to
        // fall back to and the watch shows its empty state.
        if (libraryType == Protocol.libraryMostPlayed) {
            return smartPlaylistSongs(browser, MOST_PLAYED_TITLE, limit, "Most played").orEmpty()
        }
        val browseId = browseIdFor(libraryType) ?: return emptyList()
        val result = runCatching { browser.getChildren(browseId, 0, limit, null).await() }.getOrNull()
        val items = result?.takeIf { it.resultCode == LibraryResult.RESULT_SUCCESS }?.value.orEmpty()
        val rows = items.asLibraryRows(libraryType)
        Log.i(
            TAG,
            "Library type=$libraryType browse='$browseId' code=${result?.resultCode} " +
                "items=${items.size} rows=${rows.size}",
        )
        return rows.map { it.toLibraryItem() }
    }

    /**
     * Drops rows that are not songs.
     *
     * Recently Played came back with albums in it. Symfonium's browse nodes are not
     * uniform - a node mixes real songs with browsable containers (albums, artists) and,
     * inside a playlist, with playable pseudo-action rows ("Play" / "Shuffle" / "Resume",
     * see playMediaId). Nothing filtered them, so every row was sent to the watch as a
     * song: picking an album row asked the watch to play a container, which is not
     * playable, and the row's "artist" was whatever subtitle the album carried.
     *
     * A song is playable and not browsable. Playlists are the deliberate exception - a
     * playlist row *is* a browsable container and is meant to be, so that list keeps
     * what Symfonium gives it.
     */
    private fun List<MediaItem>.asLibraryRows(libraryType: Int): List<MediaItem> =
        if (libraryType == Protocol.libraryPlaylists) {
            // Playlists that back watch library rows (Recently Played, Most Played)
            // are the same songs under a second name here, so they hide.
            filter {
                val title = it.mediaMetadata.title?.toString().orEmpty()
                !RECENT_PLAYED_TITLE.containsMatchIn(title) && !MOST_PLAYED_TITLE.containsMatchIn(title)
            }
        } else {
            filter { it.mediaMetadata.isPlayable == true && it.mediaMetadata.isBrowsable != true }
        }

    /**
     * Walks a browse id to its end and keeps the answer, so the watch's pages are served
     * from memory rather than costing Symfonium a round trip per twelve rows scrolled.
     *
     * Walking is also the only way to know the total, and the total is the number the
     * watch needs: without it the list has no end and the scrollbar has nothing to
     * measure against. Media3 offers no count, only children.
     */
    private suspend fun libraryAll(
        browser: MediaBrowser,
        libraryType: Int,
        rebuild: Boolean,
    ): List<MediaItem> {
        if (!rebuild && libraryCacheType == libraryType) return libraryCache
        val browseId = browseIdFor(libraryType) ?: return emptyList()
        val out = ArrayList<MediaItem>()
        var page = 0
        var previousFirstId: String? = null
        while (out.size < LIBRARY_WALK_CAP) {
            val result = runCatching {
                browser.getChildren(browseId, page, BROWSE_PAGE, null).await()
            }.getOrNull()
            val items = result?.takeIf { it.resultCode == LibraryResult.RESULT_SUCCESS }?.value.orEmpty()
            if (items.isEmpty()) break
            // Symfonium is documented (by observation - see searchAndCollect) to ignore
            // paging parameters on some browse ids and answer every call with the same
            // first slice. Left alone that turns this walk into LIBRARY_WALK_CAP round
            // trips returning the same rows over and over; a repeated head means paging
            // is not honoured here, so what we already have is all there is.
            val firstId = items.first().mediaId
            if (firstId == previousFirstId) {
                Log.w(TAG, "Library browse='$browseId' ignores paging; stopping at ${out.size}")
                break
            }
            previousFirstId = firstId
            // Filtered as it goes, so the walk's total is a count of songs rather than of
            // rows - that total is what the watch's scrollbar and end-of-list check use.
            // The paging decisions below still read the unfiltered page size, which is
            // what Symfonium actually returned.
            out += items.asLibraryRows(libraryType)
            if (items.size < BROWSE_PAGE) break
            page++
        }
        Log.i(TAG, "Library walk type=$libraryType browse='$browseId' total=${out.size}")
        libraryCacheType = libraryType
        libraryCache = out
        return out
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
            val title = item.mediaMetadata.title?.toString() ?: "Unknown title"
            val artist = item.mediaMetadata.artist?.toString().orEmpty()
            transport.sendQueueItem(index, wireIdFor(item), title, artist)
        }
        transport.sendQueueComplete()
    }

    private suspend fun jumpQueue(browser: MediaBrowser, mediaId: String, generation: Int) {
        // mediaId is what we put on the wire, which may be a token - match the timeline on
        // the real id behind it (see wireIdFor).
        val targetId = itemCache[mediaId]?.mediaId ?: mediaId
        val count = browser.mediaItemCount
        val index = (0 until count).firstOrNull { browser.getMediaItemAt(it).mediaId == targetId }
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
            // STATE_IDLE with an item set is a track spinning up, not "nothing loaded":
            // the legacy bridge emits it transiently between setMediaItem and prepare.
            // Reporting Idle here made the watch bail out of its Buffering screen right
            // as playback started (raced, so "sometimes"), for picks from search and
            // playlists alike.
            browser.playbackState == Player.STATE_IDLE && browser.currentMediaItem != null ->
                Protocol.playbackBuffering
            browser.playbackState == Player.STATE_IDLE -> Protocol.playbackIdle
            else -> Protocol.playbackPaused
        }
        val metadata = browser?.mediaMetadata
        val durationMs = browser?.duration?.takeIf { it != androidx.media3.common.C.TIME_UNSET }?.coerceAtLeast(0) ?: 0L
        val positionMs = browser?.currentPosition?.coerceAtLeast(0) ?: 0L
        // The watch only latches a new "now playing" track once it has video+title+artist
        // all present together, so the current item's id has to travel as videoId.
        val currentWireId = browser?.currentMediaItem?.let { wireIdFor(it) }
        // Shuffle and repeat come from the custom actions' icons, not from the player's own
        // fields - see observeLegacyActions() for why those are always empty here.
        val loopMode = this.loopMode
        val shuffleEnabled = this.shuffleEnabled
        val volume = systemMediaVolumePercent()
        transport.sendStateSnapshot(
            playbackState = playbackState,
            generation = activeGeneration,
            videoId = currentWireId,
            title = metadata?.title?.toString(),
            artist = metadata?.artist?.toString(),
            durationMs = durationMs,
            positionMs = positionMs,
            phoneAudio = true,
            volume = volume,
            loopEnabled = loopMode == LoopModeOne,
            loopMode = loopMode,
            shuffleEnabled = shuffleEnabled,
            // Symfonium doesn't expose per-track favorite state through this API surface,
            // only a toggle action - so the watch's heart indicator can't reflect it yet.
            isFavorite = false,
            theme = watchTheme,
            // Echoes the watch's own epoch rather than asserting one: this backend never
            // changes the route (audio always plays on the phone), it only reports it.
            routeEpoch = activeRouteEpoch,
        )
        // The same picture for the phone UI, which shows this backend read-only: Symfonium
        // owns the transport controls, so the phone mirrors rather than drives.
        _uiState.value = PlaybackUiState(
            playbackState = playbackState,
            generation = activeGeneration,
            videoId = currentWireId,
            title = metadata?.title?.toString(),
            artist = metadata?.artist?.toString(),
            durationMs = durationMs,
            positionMs = positionMs,
            phoneAudio = true,
            volume = volume,
            loopEnabled = loopMode == LoopModeOne,
            loopMode = loopMode,
            shuffleEnabled = shuffleEnabled,
            isFavorite = false,
            theme = watchTheme,
            queueSize = browser?.mediaItemCount ?: 0,
            artworkUri = metadata?.artworkUri?.toString(),
        )
    }

    /**
     * Transfers Symfonium's current artwork to the watch, ported from
     * PebblePlaybackService.maybeSendCoverArt() - same watchimage encode pipeline, same
     * chunked transport. Simpler here because there is no watch-speaker audio stream to
     * share the AppMessage inbox with (audio always plays on the phone through Symfonium),
     * so none of the original's holdAudioForCover()/releaseAudioForCover() pacing applies.
     */
    /**
     * Identity of the *track* whose art we are holding, which is deliberately not just its
     * media id.
     *
     * Symfonium's id for the playing item is positional - `current_playlist/0` - so it
     * names a slot in the queue rather than a record. Starting a different song puts it at
     * position 0 too, which made the new track look like one we had already sent art for,
     * and the watch kept showing the previous cover. Observed live: a metadata change to
     * "Lock It Up Instrumental" arriving while lastSentTrack was still `current_playlist/0`
     * from the song before it.
     *
     * Skipping *forward* never showed this, because the index moves with you
     * (current_playlist/2 and so on) - which is why it read as art being unreliable rather
     * than as art being broken. The title pins the identity to the record instead of the
     * slot; a track whose title is momentarily absent from a partial republish returns
     * null so the caller waits for the next one rather than keying on half a snapshot.
     */
    private fun artworkKey(browser: MediaBrowser): String? {
        val id = browser.currentMediaItem?.mediaId ?: return null
        val title = browser.mediaMetadata.title?.toString() ?: return null
        return "$id|$title"
    }

    private fun maybeSendCoverArt(browser: MediaBrowser, force: Boolean = false) {
        if (!coverArtEnabled) return
        // A re-send the watch explicitly asked for (see the Hello branch) has to defeat
        // the "already sent this track" guard - the track has not changed, the watch's
        // memory of it has.
        if (force) lastSentArtworkTrack = null
        // Decided BEFORE anything is cancelled: a job already working on this track owns
        // it until it finishes or fails. Cancelling and restarting on every metadata
        // event meant a routine republish - Symfonium emits them on pause/resume and
        // mid-transfer - killed a 41-chunk transfer partway and began again from chunk
        // zero, and worse, left the track marked in-flight (see the try/finally below).
        if (inflightArtworkTrack != null && inflightArtworkTrack == artworkKey(browser)) {
            return
        }
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
            // A null current item is a mid-transition republish - there is nothing to
            // key on, and acting on it used to stomp lastSentArtworkTrack to null, which
            // then re-streamed the same cover on the next ping (and on every
            // pause/resume, whose republish can arrive exactly like this).
            val trackId = artworkKey(browser) ?: return@launch
            // Identity is the track, not the payload: Symfonium re-encodes the same cover
            // on every republish (same byte length, different hash), so a payload key
            // re-streamed the image on every pause/resume. A track whose art completed
            // (or is streaming right now) is never re-sent.
            if (trackId == lastSentArtworkTrack || trackId == inflightArtworkTrack) return@launch
            // Claimed for the whole fetch/encode/transfer and released in the finally
            // below, so cancellation releases it too. Clearing it only on the normal path
            // (as this did) meant a cancel mid-transfer - which the settle above makes
            // routine - left the track marked in-flight forever, and every later attempt
            // at that song returned early. Its art then never arrived again, however long
            // it played, until the user skipped.
            inflightArtworkTrack = trackId
            try {
                // Deadlined: a link that takes the header and then dies mid-stream can
                // otherwise keep the retries inside busy for minutes on a track that is
                // still playing. Giving up frees the slot, and the next metadata ping
                // starts over.
                when (val outcome = withTimeoutOrNull(ART_DEADLINE_MS) { pursueCoverArt(browser, trackId) }) {
                    ArtOutcome.Sent -> lastSentArtworkTrack = trackId
                    // A track that genuinely carries no art is settled: without marking
                    // it, every metadata ping on it re-runs the whole thing and re-clears
                    // the watch. A failure is *not* settled - leave it retryable.
                    ArtOutcome.NoArtwork -> {
                        Log.i(TAG, "No artwork at all for $trackId; clearing")
                        transport.sendCoverArtClear(activeGeneration)
                        lastSentArtworkTrack = trackId
                    }
                    // Someone else's track now; its own job owns what the watch shows.
                    ArtOutcome.Superseded -> Unit
                    else -> {
                        Log.w(TAG, "Gave up on artwork for $trackId (outcome=$outcome)")
                        transport.sendCoverArtClear(activeGeneration)
                    }
                }
            } finally {
                if (inflightArtworkTrack == trackId) inflightArtworkTrack = null
            }
        }
    }

    /**
     * Chases [trackId]'s cover until it arrives or the attempts run out, and reports how
     * it ended.
     *
     * Symfonium hands out a stand-in cover while the real one loads and does not reliably
     * republish metadata once it lands, so waiting for a republish - which is all this
     * used to do - can mean waiting for an event that never comes, leaving the watch on
     * its own placeholder for the rest of the song. The art is chased rather than
     * hoped for.
     */
    private suspend fun pursueCoverArt(browser: MediaBrowser, trackId: String): ArtOutcome {
        var outcome = ArtOutcome.Failed
        repeat(ART_ATTEMPTS) { attempt ->
            if (attempt > 0) delay(ART_RETRY_DELAY_MS)
            // The track moved on under us - the new one gets its own job.
            if (artworkKey(browser) != trackId) return ArtOutcome.Superseded
            outcome = sendCoverArtOnce(browser)
            if (outcome == ArtOutcome.Sent) return ArtOutcome.Sent
        }
        return outcome
    }

    /** Why an artwork attempt stopped, and so whether another is worth making. */
    private enum class ArtOutcome { Sent, NotReady, NoArtwork, Failed, Superseded }

    /** One end-to-end pass at the current track's cover: read, fetch, encode, stream. */
    private suspend fun sendCoverArtOnce(browser: MediaBrowser): ArtOutcome {
        val artworkUri = browser.mediaMetadata.artworkUri
        // Legacy sessions usually carry artwork as a Bitmap in the metadata rather than
        // as a URI, and Media3 surfaces that as artworkData - which is what Symfonium
        // does. Reading only artworkUri (as this did) meant every track looked like it
        // had no cover: "onMediaMetadataChanged: art=null" on every single one.
        val artworkData = browser.mediaMetadata.artworkData
        if (artworkUri == null && artworkData == null) {
            Log.i(TAG, "No artwork on the current track's metadata yet")
            return ArtOutcome.NoArtwork
        }
        Log.i(TAG, "Fetching artwork: uri=$artworkUri embeddedBytes=${artworkData?.size}")
        val raw = if (artworkUri != null) loadArtworkBytes(artworkUri) else artworkData
        if (raw == null) {
            Log.w(TAG, "Artwork fetch failed for $artworkUri")
            return ArtOutcome.Failed
        }
        // Symfonium publishes a tiny stand-in while the real cover loads. Streaming it
        // wastes a transfer and flashes a blank square, so the attempt is abandoned and
        // made again - by then the real cover has usually replaced it behind the same URI.
        if (isPlaceholderArtwork(raw)) {
            Log.i(TAG, "Artwork is a placeholder (${raw.size} bytes); waiting for the real cover")
            return ArtOutcome.NotReady
        }
        Log.i(TAG, "Artwork fetched: ${raw.size} bytes; encoding at ${COVER_ART_DIM}px")
        val payload = encodeCoverArtWithWatchimage(raw, COVER_ART_DIM)
        val monoBytes = (COVER_ART_DIM / 8) * COVER_ART_DIM
        if (payload == null || (payload.size != monoBytes && payload.size != COVER_ART_DIM * COVER_ART_DIM)) {
            Log.w(TAG, "Artwork encode failed or wrong size: ${payload?.size}")
            return ArtOutcome.Failed
        }
        // The mediaId travels as the videoId here too (see sendStateSnapshot), so the
        // watch can match the cover to the track it is for rather than relying on the
        // generation counter.
        val wireId = browser.currentMediaItem?.let { wireIdFor(it) }.orEmpty()
        return if (streamCoverArt(payload, wireId)) ArtOutcome.Sent else ArtOutcome.Failed
    }

    /**
     * Streams an encoded payload to the watch, restarting from chunk zero if the link
     * drops out partway. The watch enforces strictly consecutive sequence numbers and
     * discards the whole image on a gap, so there is no resuming from the middle: a
     * 144x144 colour cover is 41 consecutive acked messages, and a single all-or-nothing
     * pass over BLE fails often enough on its own to read as "album art is unreliable".
     * (PebblePlaybackService.sendCoverArtPayload reached the same conclusion.)
     *
     * activeGeneration is read fresh per message rather than captured when the job
     * started: the encode ahead of this can take a couple of seconds, long enough for
     * several watch commands - each bumping the watch's own counter - to land first, and
     * the watch drops any cover-art message that is older than its current generation.
     */
    private suspend fun streamCoverArt(payload: ByteArray, wireId: String): Boolean {
        repeat(COVER_SEND_ATTEMPTS) { attempt ->
            if (attempt > 0) {
                Log.w(TAG, "Cover art transfer failed; retrying (${attempt + 1}/$COVER_SEND_ATTEMPTS)")
                delay(COVER_RETRY_DELAY_MS)
            }
            Log.i(TAG, "Sending artwork to watch: ${payload.size} bytes")
            // There is no point streaming 41 chunks at a watch that never took the header.
            val opened = transport.sendCoverArtStart(
                COVER_ART_DIM,
                COVER_ART_DIM,
                payload.size,
                activeGeneration,
                wireId,
            )
            if (!opened) return@repeat
            var sequence = 0
            var offset = 0
            var delivered = true
            while (offset < payload.size) {
                val end = minOf(offset + COVER_ART_CHUNK_BYTES, payload.size)
                if (!transport.sendCoverArtChunk(payload.copyOfRange(offset, end), sequence, activeGeneration)) {
                    Log.w(TAG, "Artwork chunk $sequence failed to deliver; abandoning this attempt")
                    delivered = false
                    break
                }
                offset = end
                sequence++
            }
            if (delivered) {
                Log.i(TAG, "Artwork transfer complete ($sequence chunks)")
                return true
            }
        }
        return false
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

    /** A cover smaller than this is Symfonium's stand-in, never real album art. */
    private fun isPlaceholderArtwork(raw: ByteArray): Boolean {
        val opts = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeByteArray(raw, 0, raw.size, opts)
        return opts.outWidth in 1 until PLACEHOLDER_ART_MAX_DIM
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
        // mediaId prefixes in search results, observed live: albums arrive as
        // browse_album_songs/<id>, artists as browse_albums_artists/<id>; songs are the
        // playable non-browsable rest and need no prefix check.
        private const val ID_PREFIX_ALBUM = "browse_album_songs/"
        private const val ID_PREFIX_ARTIST = "browse_albums_artists/"

        // A playlist as it appears under BROWSE_PLAYLISTS, and the shuffle-play pseudo-row
        // found inside one. Verified against Symfonium's browse tree via the "children"
        // probe - the node is browse_playlist_action/<n>, not browse_playlist/<n>.
        // A track inside browse_album_songs/<id>. The album leads with album_random/<id>
        // ("Shuffle"), which is an action rather than a song.
        private const val ID_PREFIX_ALBUM_SONG = "album_song/"
        // A song inside a playlist's children, as opposed to the "Play"/"Shuffle"/
        // "Resume" pseudo-rows they lead with (see playMediaId).
        private const val ID_PREFIX_SONG = "song/"
        // Titles of the user smart playlists that back watch library rows. Deliberately
        // loose about anything around the phrase; the same regexes hide those playlists
        // from the Playlists list so the songs are not listed under two names.
        private val RECENT_PLAYED_TITLE = Regex("(?i)\\brecently played\\b")
        private val MOST_PLAYED_TITLE = Regex("(?i)\\bmost played\\b")
        // How many recent albums Recently Played walks before giving up on filling the
        // page. Each one costs a browse round trip, so this is a ceiling on the wait, not
        // on the result - the song limit usually stops it long before.
        private const val RECENT_ALBUM_SCAN = 25
        private const val ID_PREFIX_PLAYLIST_NODE = "browse_playlist_action/"
        private const val ID_PLAYLIST_SHUFFLE = "playlist_shuffle/"
        private const val CONNECT_TIMEOUT_MS = 5_000L
        // Matches the watch's MAX_LIBRARY: it asks for its full capacity and this cap
        // used to silently shave it (40 < 60), which read as "Favorites is capped".
        // 100 because the watch's Symfonium History grid now reaches that high.
        // Applies to the unpaged library types only - a paged type (Favorites) is not
        // bounded by the watch's array any more and so is not bounded by this either.
        private const val LIBRARY_HARD_CAP = 100
        // One page of a paged library. Matches the watch's SPAN_PAGE; the watch sends its
        // own page size and this only stops a malformed request asking for thousands.
        private const val LIBRARY_PAGE_SIZE = 60
        // How much of the list is pulled from Symfonium per browse call while walking it.
        // Larger than a watch page on purpose: this is a local IPC round trip, and the
        // walk is what decides how long opening Favorites takes.
        private const val BROWSE_PAGE = 200
        // A runaway guard on that walk, not a display cap - it is far past any real
        // library, and a browse id that never returns a short page is a bug on the other
        // side rather than a collection of a million favourites. It does bound what the
        // cache can cost: the walked MediaItems are held until the list is reopened.
        private const val LIBRARY_WALK_CAP = 10_000
        private const val MAX_QUEUE_ITEMS = 40
        // One page of search results. The watch asks for what its window can hold; this
        // only stops a malformed request from asking for thousands of rows at once.
        private const val SEARCH_PAGE_CAP = 40
        // How deep the ranked list is retained for paging. Symfonium returns its whole
        // ranked answer in a single call, so this bounds what we keep, not what it
        // searched - and 500 rows of three short strings is a few tens of KB on a phone.
        private const val SEARCH_COLLECT_CAP = 500
        // Asked of Symfonium per search call. It ignores this and answers with its own
        // fixed slice regardless (see searchAndCollect), so this is aspirational rather
        // than effective - kept because a future Symfonium may honour it, and asking for
        // more than it gives costs nothing.
        private const val SEARCH_SOURCE_PAGE = 100
        // Tracks read from one matched album while deepening a song search. Comfortably
        // past any real album, so a walk is never cut mid-record.
        private const val ALBUM_TRACK_CAP = 200
        // Artists walked while deepening a song search. Each costs one call for their
        // albums plus one per album, so this is the knob that keeps a broad query from
        // turning into a hundred round trips.
        private const val ARTIST_EXPAND_MAX = 6
        // "1 • Title", "12. Title", "3 - Title": the track-number prefix Symfonium puts on
        // an album's children for a car list, which a flat result list has no use for.
        private val TRACK_NUMBER_PREFIX = Regex("""^\s*\d+\s*[•·.\-–]\s*""")
        // Per browse call while deepening. Symfonium answers these in ~10ms when it
        // answers at all, so this only ever fires on a call that is not coming back.
        private const val BROWSE_TIMEOUT_MS = 3_000L
        // Whole-phase budget for deepening one search. Whatever has been gathered by then
        // is returned - a shorter list beats a search that never lands.
        private const val EXPAND_DEADLINE_MS = 6_000L
        // The phone has no fixed-size result buffers; it can afford a fuller page.
        private const val UI_SEARCH_CAP = 40
        private const val SEARCH_TIMEOUT_MS = 8_000L
        // TEXT_LENGTH in the watch's main.c is 81 bytes including the terminator.
        private const val MAX_WIRE_ID_BYTES = 80
        private const val PREFERENCES_NAME = "dreamwave_symfonium"
        private const val KEY_THEME = "theme"
        private const val KEY_COVER_ART_BG = "cover_art_bg"
        private const val KEY_AUTO_SHUFFLE = "auto_shuffle_playlists"
        // _v2: clears stale icon lessons that can pin shuffle opposite to Symfonium.
        private const val KEY_SHUFFLE_ICONS = "shuffle_icon_states_v2"
        // _v4: reset persisted repeat icon lessons again so stale mappings are dropped,
        // same treatment shuffle just got.
        private const val KEY_REPEAT_ICONS = "repeat_icon_states_v4"

        /**
         * Symfonium's Repeat icons, mapped absolutely.
         *
         * The learning in observeLegacyActions can only ever see a *change*, never a state,
         * so if its opening belief is wrong every pair it records inherits the same offset.
         * That is exactly what happened: the watch sat one step ahead of Symfonium - Off
         * read as One, One as All, All as Off - and being persisted, it stayed wrong.
         *
         * Nothing on the session gives the absolute state. The action names are bare, the
         * extras are null, and the legacy controller's own repeatMode/shuffleMode are
         * hardcoded 0 here (all verified on device). So it is pinned once from observation:
         * cycling Repeat and reading the icon each time gives 853 -> 851 -> 852 -> 853,
         * which anchored against what Symfonium was actually showing is Off -> All -> One.
         *
         * These are resource ids inside Symfonium's own APK, so an update to Symfonium can
         * renumber them. An unrecognised id is deliberately not forced into a mode - it
         * falls through to the relative learning, which is no worse than before.
         */
        private val REPEAT_ICON_SEED = mapOf(
            2131230853 to LoopModeOff,
            2131230851 to LoopModeAll,
            2131230852 to LoopModeOne,
        )
        // Always the original's "high" tier (144px): the low tier existed only to protect
        // BLE bandwidth for a concurrent watch-speaker audio stream, which this backend
        // never has (see maybeSendCoverArt's kdoc).
        private const val COVER_ART_DIM = 144
        private const val COVER_ART_CHUNK_BYTES = 512
        // Symfonium's stand-in art while the real cover loads is a tiny image (observed:
        // a 500-byte one); anything under this square size is treated as a placeholder.
        private const val PLACEHOLDER_ART_MAX_DIM = 48
        private const val COVER_ART_SETTLE_MS = 400L
        // Passes over the whole pipeline for one track, spaced by ART_RETRY_DELAY_MS -
        // enough to outlast Symfonium swapping its stand-in cover for the real one
        // (typically the first second or two of a track) without chasing a track the
        // user has already skipped past.
        private const val ART_ATTEMPTS = 4
        private const val ART_RETRY_DELAY_MS = 1_500L
        // Ceiling on one track's whole pursuit, retries and all.
        private const val ART_DEADLINE_MS = 45_000L
        // Restarts of the chunk stream itself, for a link that dropped mid-transfer.
        private const val COVER_SEND_ATTEMPTS = 3
        private const val COVER_RETRY_DELAY_MS = 400L
        private const val LoopModeOff = 0
        private const val LoopModeOne = 1
        private const val LoopModeAll = 2
        private const val TOGGLE_CONFIRM_WINDOW_MS = 2_500L
        private const val ThemeTeal = 0
        private const val ThemeDefault = 3
        private const val ThemeMono = 4
        private const val ThemeArcade = 5
        // The neutral pair the watch now defaults to. Only ThemeMono changes anything on
        // this side (it selects the mono cover-art encoder), but the accepted range still
        // has to cover every value the watch can send - otherwise the coerceIn below
        // silently rewrites Default Dark into Arcade.
        private const val ThemeDefaultLight = 6
        private const val ThemeDefaultDark = 7
    }
}

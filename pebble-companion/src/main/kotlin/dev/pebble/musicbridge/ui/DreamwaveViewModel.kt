package dev.pebble.musicbridge.ui

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import dev.pebble.musicbridge.CacheUiState
import dev.pebble.musicbridge.MusicSourcePrefs
import dev.pebble.musicbridge.PlaybackConnection
import dev.pebble.musicbridge.PlaybackPrefs
import dev.pebble.musicbridge.PlaybackUiState
import dev.pebble.musicbridge.PlaylistInfo
import dev.pebble.musicbridge.Protocol
import dev.pebble.musicbridge.SearchResultItem
import dev.pebble.musicbridge.SettingsUiState
import dev.pebble.musicbridge.SourceConnection
import dev.pebble.musicbridge.UiCommand
import dev.pebble.musicbridge.ui.theme.ThemeMode
import dev.pebble.musicbridge.ui.theme.UiPrefs
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/**
 * Shared ViewModel for all dreamwave screens. Owns the [PlaybackConnection] and
 * exposes the service's flows once bound.
 */
class DreamwaveViewModel(application: Application) : AndroidViewModel(application) {

    private val connection = PlaybackConnection(application)
    private val sourceConnection = SourceConnection(application)

    val service = connection.service

    /**
     * Which backend is serving the watch. Two-way: changing it here reaches the watch, and
     * changing it on the watch lands back here. Seeded synchronously so the first frame
     * shows the right source instead of flashing YouTube while the binding completes.
     */
    private val _musicSource = MutableStateFlow(MusicSourcePrefs.source(application))
    val musicSource: StateFlow<Int> = _musicSource.asStateFlow()

    val isSymfonium: StateFlow<Boolean> = _musicSource
        .map { it == Protocol.sourceSymfonium }
        .stateIn(viewModelScope, SharingStarted.Eagerly, MusicSourcePrefs.isSymfonium(application))

    fun setMusicSource(source: Int) {
        if (source == _musicSource.value) return
        // Optimistic: the service is the authority, and its own flow corrects this if the
        // change is refused, but the toggle should not sit still while it round-trips.
        _musicSource.value = source
        sourceConnection.service.value?.setMusicSource(source)
    }

    /** Synchronously-read theme index so the first frame has the right accent. */
    val initialTheme: Int = PlaybackPrefs.themeIndex(application)

    /**
     * Light/dark, read synchronously for the same reason as the accent: the theme is
     * needed before the first composition, and going through the service would flash
     * the wrong one while the binding completes.
     */
    private val _themeMode = MutableStateFlow(UiPrefs.themeMode(application))
    val themeMode: StateFlow<ThemeMode> = _themeMode.asStateFlow()

    fun setThemeMode(mode: ThemeMode) {
        UiPrefs.setThemeMode(getApplication(), mode)
        _themeMode.value = mode
    }

    /**
     * The phone's chosen theme, or [UiPrefs.FOLLOW_WATCH]. Phone-only, like [themeMode]:
     * it never reaches the watch, so changing it here cannot disturb what the watch is
     * wearing.
     */
    private val _appTheme = MutableStateFlow(UiPrefs.appTheme(application))
    val appTheme: StateFlow<Int> = _appTheme.asStateFlow()

    fun setAppTheme(theme: Int) {
        UiPrefs.setAppTheme(getApplication(), theme)
        _appTheme.value = theme
    }

    private val _uiState = MutableStateFlow(PlaybackUiState())
    val uiState: StateFlow<PlaybackUiState> = _uiState.asStateFlow()

    private val _cacheState = MutableStateFlow(CacheUiState())
    val cacheState: StateFlow<CacheUiState> = _cacheState.asStateFlow()

    private val _errorMessage = MutableStateFlow<String?>(null)
    val errorMessage: StateFlow<String?> = _errorMessage.asStateFlow()

    private val _searchQuery = MutableStateFlow("")
    val searchQuery: StateFlow<String> = _searchQuery.asStateFlow()

    private val _searchResults = MutableStateFlow<List<SearchResultItem>>(emptyList())
    val searchResults: StateFlow<List<SearchResultItem>> = _searchResults.asStateFlow()

    /**
     * Cached songs matching the query. Filtered locally and shown above the online
     * hits — anything already on disk plays instantly and costs no data, so it
     * deserves to be the first thing offered.
     */
    private val _cachedMatches = MutableStateFlow<List<SearchResultItem>>(emptyList())
    val cachedMatches: StateFlow<List<SearchResultItem>> = _cachedMatches.asStateFlow()

    private val _recentlyPlayed = MutableStateFlow<List<SearchResultItem>>(emptyList())
    val recentlyPlayed: StateFlow<List<SearchResultItem>> = _recentlyPlayed.asStateFlow()

    /** Favorited tracks, newest first — mirrors the watch's Favorites library list. */
    private val _favorites = MutableStateFlow<List<SearchResultItem>>(emptyList())
    val favorites: StateFlow<List<SearchResultItem>> = _favorites.asStateFlow()

    /** Playlist headers for the Library's Playlists tab. Phone-owned; watch is read-only. */
    private val _playlists = MutableStateFlow<List<PlaylistInfo>>(emptyList())
    val playlists: StateFlow<List<PlaylistInfo>> = _playlists.asStateFlow()

    /**
     * Symfonium's own library, for the Search/Library screens while that source is
     * active. These mirror exactly the three lists the watch can pull (recently
     * played, favorites, playlists); the YouTube-shaped flows above are meaningless
     * for this source since Symfonium owns the library.
     */
    private val _symfoniumRecent = MutableStateFlow<List<SearchResultItem>>(emptyList())
    val symfoniumRecent: StateFlow<List<SearchResultItem>> = _symfoniumRecent.asStateFlow()

    private val _symfoniumFavorites = MutableStateFlow<List<SearchResultItem>>(emptyList())
    val symfoniumFavorites: StateFlow<List<SearchResultItem>> = _symfoniumFavorites.asStateFlow()

    private val _symfoniumPlaylists = MutableStateFlow<List<SearchResultItem>>(emptyList())
    val symfoniumPlaylists: StateFlow<List<SearchResultItem>> = _symfoniumPlaylists.asStateFlow()

    /** Songs of the playlist currently open in the Library detail subscreen. */
    private val _playlistSongs = MutableStateFlow<List<SearchResultItem>>(emptyList())
    val playlistSongs: StateFlow<List<SearchResultItem>> = _playlistSongs.asStateFlow()

    /** "Songs you might like", seeded from the most recent play. */
    private val _recommended = MutableStateFlow<List<SearchResultItem>>(emptyList())
    val recommended: StateFlow<List<SearchResultItem>> = _recommended.asStateFlow()

    /** Fully-cached ids, so lists can mark what plays without the network. */
    private val _cachedIds = MutableStateFlow<Set<String>>(emptySet())
    val cachedIds: StateFlow<Set<String>> = _cachedIds.asStateFlow()

    private val _searching = MutableStateFlow(false)
    val searching: StateFlow<Boolean> = _searching.asStateFlow()

    /**
     * What a submitted search looks for: plain songs, an artist to shuffle ("Artist
     * Radio"), or a song to seed a radio queue from — the same three modes the watch
     * offers, over the same protocol values.
     */
    private val _searchMode = MutableStateFlow(Protocol.searchModeSong)
    val searchMode: StateFlow<Int> = _searchMode.asStateFlow()

    fun setSearchMode(mode: Int) {
        if (mode == _searchMode.value) return
        _searchMode.value = mode
        // Mode changes what the query means, so stale results from the old mode must
        // not survive it.
        _searchResults.value = emptyList()
        if (_searchQuery.value.isNotBlank()) submitSearch()
    }

    private val _settings = MutableStateFlow(SettingsUiState())
    val settings: StateFlow<SettingsUiState> = _settings.asStateFlow()

    private val _position = MutableStateFlow(0L)
    val position: StateFlow<Long> = _position.asStateFlow()

    private val _artworkUrl = MutableStateFlow<String?>(null)
    val artworkUrl: StateFlow<String?> = _artworkUrl.asStateFlow()

    /**
     * videoId -> cover URL for every track the player can show, not just the current
     * one. The album carousel renders artwork for each entry in the *queue*, so
     * resolving only the playing track left every card blank.
     */
    private val _artworkUrls = MutableStateFlow<Map<String, String>>(emptyMap())
    val artworkUrls: StateFlow<Map<String, String>> = _artworkUrls.asStateFlow()

    private val _queue = MutableStateFlow<List<Pair<String, Pair<String, String>>>>(emptyList())
    val queue: StateFlow<List<Pair<String, Pair<String, String>>>> = _queue.asStateFlow()

    /**
     * Collectors attached to the *current* service instance. The service is killed and
     * recreated by the OS in normal operation, and each rebind used to stack another set
     * of collectors onto the old instance — including a `positionFlow()` polling a
     * released ExoPlayer every 500 ms. Cancelling the previous job before attaching to
     * the new service keeps exactly one set of collectors, bound to a live instance.
     */
    private var serviceCollectors: Job? = null

    init {
        connection.bind()
        sourceConnection.bind()
        // The source can change from either end; this is the one place that learns about
        // both, since the service applies the epoch rule for watch-initiated changes.
        viewModelScope.launch {
            sourceConnection.service.collect { svc ->
                if (svc != null) launch { svc.musicSource.collect { _musicSource.value = it } }
            }
        }
        // While Symfonium is the active source the phone mirrors ITS player. Read-only:
        // the transport controls stay disabled because Symfonium's own app owns them.
        viewModelScope.launch {
            _musicSource.collectLatest { source ->
                if (source != Protocol.sourceSymfonium) return@collectLatest
                val svc = sourceConnection.service.value ?: return@collectLatest
                svc.symfoniumState()?.collect { _uiState.value = it }
            }
        }
        viewModelScope.launch {
            connection.service.collect { svc ->
                serviceCollectors?.cancel()
                serviceCollectors = null
                if (svc != null) {
                    _settings.value = svc.settingsSnapshot()
                    // Home's first composition happens before the service binds, so
                    // its LaunchedEffect(Unit) would hit a null service and give up.
                    // Loading here means the screen fills in as soon as data exists.
                    refreshCache()
                    refreshRecentlyPlayed()
                    refreshFavorites()
                    refreshPlaylists()
                    serviceCollectors = launch {
                        // Guarded: this backend is stopped while Symfonium is the source,
                        // and its idle state would otherwise overwrite what Symfonium is
                        // actually playing.
                        launch {
                            svc.uiState.collect {
                                if (_musicSource.value != Protocol.sourceSymfonium) _uiState.value = it
                            }
                        }
                        launch { svc.errors.collect { _errorMessage.value = it } }
                        launch { svc.positionFlow().collect { _position.value = it } }
                    }
                }
            }
        }
        // Refresh artwork when the track changes.
        viewModelScope.launch {
            _uiState.map { it.videoId }.distinctUntilChanged().collect { videoId ->
                if (videoId == null) {
                    _artworkUrl.value = null
                    _queue.value = emptyList()
                    return@collect
                }
                // Symfonium supplies its own artwork with the metadata, when it supplies one
                // at all (often it hands over raw bytes instead, which cannot be a URI). The
                // resolver below is a YouTube cover-URL lookup keyed by videoId and would
                // both fail and mislead on a Symfonium media id, so this source never enters
                // it — a missing cover stays missing rather than becoming a wrong one.
                if (_musicSource.value == Protocol.sourceSymfonium) {
                    _artworkUrl.value = _uiState.value.artworkUri
                    _queue.value = emptyList()
                    return@collect
                }
                val svc = connection.service.value ?: return@collect
                _artworkUrl.value = svc.artworkUrlFor(videoId)
                val queue = svc.currentQueue()
                _queue.value = queue
                resolveArtworkFor(listOf(videoId) + queue.map { it.first })
            }
        }
    }

    fun sendCommand(command: UiCommand) {
        connection.service.value?.onUiCommand(command)
    }

    /**
     * Resolves cover URLs for [videoIds], publishing each one as it arrives so the
     * carousel fills in progressively rather than waiting on the slowest lookup.
     * Already-known ids are skipped; the service memoises the rest.
     */
    fun resolveArtworkForIds(videoIds: List<String>) = resolveArtworkFor(videoIds)

    private fun resolveArtworkFor(videoIds: List<String>) {
        // Deliberately does NOT cancel a running resolution. These jobs are additive
        // and each id is fetched once; cancelling on every call meant leaving a tab
        // mid-resolve abandoned the rest, and the caller's LaunchedEffect key would
        // not have changed, so nothing would ever finish it.
        val missing = videoIds.distinct().filter {
            it.isNotBlank() && it !in _artworkUrls.value && inFlightArtwork.add(it)
        }
        if (missing.isEmpty()) return
        viewModelScope.launch {
            val svc = connection.service.value
            if (svc == null) {
                inFlightArtwork.removeAll(missing.toSet())
                return@launch
            }
            for (id in missing) {
                val url = runCatching { svc.artworkUrlFor(id) }.getOrNull()
                if (url != null) _artworkUrls.value = _artworkUrls.value + (id to url)
                // Keep failures marked in-flight-free so a later pass can retry them.
                inFlightArtwork.remove(id)
            }
        }
    }

    /**
     * Sends a settings write, then reads the result back rather than assuming it.
     * The service clamps and quantises (cache size snaps to 50 MB steps, theme to
     * 0..4), so echoing the requested value would let the UI drift from the truth.
     */
    fun applySetting(command: UiCommand) {
        sendCommand(command)
        refreshSettings()
    }

    private var searchJob: Job? = null
    /** ids currently being resolved, so overlapping callers do not refetch the same cover. */
    private val inFlightArtwork = java.util.Collections.synchronizedSet(mutableSetOf<String>())

    fun onSearchQueryChanged(query: String) {
        _searchQuery.value = query
        if (query.isBlank()) {
            searchJob?.cancel()
            _searchResults.value = emptyList()
            _cachedMatches.value = emptyList()
            _searching.value = false
            return
        }
        // Local matches are free, so they update as you type rather than waiting
        // for you to submit.
        val needle = query.trim().lowercase()
        _cachedMatches.value = _cacheState.value.entries
            .filter {
                it.title.lowercase().contains(needle) || it.artist.lowercase().contains(needle)
            }
            .map { SearchResultItem(it.videoId, it.title, it.artist) }
    }

    /**
     * Runs the query against the active source. Cancels any search already running, so
     * typing quickly cannot let an older, slower response land on top of a newer one.
     */
    fun submitSearch() {
        val query = _searchQuery.value.trim()
        if (query.isEmpty()) return
        searchJob?.cancel()
        searchJob = viewModelScope.launch {
            _searching.value = true
            if (_musicSource.value == Protocol.sourceSymfonium) {
                // Symfonium answers real ranked searches without touching playback (see
                // SymfoniumPlaybackService.searchLibrary) - the list arrives first, and
                // only the picked row plays. Radio modes are a YouTube feature, so the
                // mode is ignored here.
                val backend = runCatching {
                    sourceConnection.service.filterNotNull().first().symfoniumBackend()
                }.getOrNull()
                if (backend == null) {
                    _errorMessage.value = "Can't reach Symfonium"
                } else {
                    runCatching { backend.searchForUi(query, searchMode = _searchMode.value) }
                        .onSuccess { _searchResults.value = it }
                        .onFailure {
                            _errorMessage.value = "Search failed: ${it.message ?: "unknown error"}"
                        }
                }
                _searching.value = false
                return@launch
            }
            val svc = connection.service.value
            if (svc == null) {
                _errorMessage.value = "Not connected to playback service"
                _searching.value = false
                return@launch
            }
            svc.searchForUi(query, searchMode = _searchMode.value)
                // Drop online hits already listed under "on this phone", so the same
                // track does not appear twice in one list.
                .onSuccess { results ->
                    val cachedIds = _cachedMatches.value.mapTo(mutableSetOf()) { it.videoId }
                    _searchResults.value = results.filterNot { it.videoId in cachedIds }
                }
                .onFailure { _errorMessage.value = "Search failed: ${it.message ?: "unknown error"}" }
            _searching.value = false
        }
    }

    /** Loads Symfonium's own library lists for the Library screen. */
    fun refreshSymfoniumLibrary() {
        viewModelScope.launch {
            // filterNotNull().first() waits out the bind: the screen composes before the
            // source service is up, and a one-shot value read would leave the lists
            // empty until the tab was revisited.
            val backend = runCatching {
                sourceConnection.service.filterNotNull().first().symfoniumBackend()
            }.getOrNull() ?: return@launch
            _symfoniumRecent.value = backend.libraryForUi(Protocol.libraryRecent)
            _symfoniumFavorites.value = backend.libraryForUi(Protocol.libraryFavorites)
            _symfoniumPlaylists.value = backend.libraryForUi(Protocol.libraryPlaylists)
        }
    }

    /**
     * Plays a listed track (search result, library row) through whichever backend is
     * active. Under Symfonium the id is that backend's wire id and playing means telling
     * Symfonium itself; under YouTube it is the resolver/stream path as before.
     */
    fun playFromActiveSource(videoId: String) {
        if (_musicSource.value == Protocol.sourceSymfonium) {
            viewModelScope.launch {
                runCatching {
                    sourceConnection.service.filterNotNull().first().symfoniumBackend()
                        ?.playFromUi(videoId)
                }
            }
        } else {
            sendCommand(UiCommand.Play(videoId))
        }
    }

    fun refreshRecentlyPlayed() {
        viewModelScope.launch {
            val svc = connection.service.value ?: return@launch
            val recent = svc.recentlyPlayed()
            _recentlyPlayed.value = recent
            _cachedIds.value = svc.cachedVideoIds()
            resolveArtworkFor(recent.map { it.videoId })

            refreshRecommendations()
        }
    }

    fun refreshFavorites() {
        viewModelScope.launch {
            val svc = connection.service.value ?: return@launch
            val favs = svc.favoritesList()
            _favorites.value = favs
            resolveArtworkFor(favs.map { it.videoId })
        }
    }

    fun refreshPlaylists() {
        viewModelScope.launch {
            val svc = connection.service.value ?: return@launch
            val lists = svc.playlistsList()
            _playlists.value = lists
            resolveArtworkFor(lists.mapNotNull { it.firstVideoId })
        }
    }

    /** Loads (or reloads) the songs of the playlist open in the Library detail view. */
    fun openPlaylist(playlistId: String) {
        viewModelScope.launch {
            val svc = connection.service.value ?: return@launch
            val songs = svc.playlistSongs(playlistId)
            _playlistSongs.value = songs
            resolveArtworkFor(songs.map { it.videoId })
        }
    }

    /** Creates a playlist, optionally adding [addVideoId] to it in the same action. */
    fun createPlaylist(name: String, addVideoId: String? = null) {
        viewModelScope.launch {
            val svc = connection.service.value ?: return@launch
            val id = svc.createPlaylist(name)
            if (addVideoId != null) svc.addToPlaylist(id, addVideoId)
            refreshPlaylists()
        }
    }

    fun deletePlaylist(playlistId: String) {
        viewModelScope.launch {
            val svc = connection.service.value ?: return@launch
            svc.deletePlaylist(playlistId)
            refreshPlaylists()
        }
    }

    fun addToPlaylist(playlistId: String, videoId: String) {
        viewModelScope.launch {
            val svc = connection.service.value ?: return@launch
            svc.addToPlaylist(playlistId, videoId)
            refreshPlaylists()
        }
    }

    fun removeFromPlaylist(playlistId: String, videoId: String) {
        viewModelScope.launch {
            val svc = connection.service.value ?: return@launch
            svc.removeFromPlaylist(playlistId, videoId)
            // The header song count changed; the caller refreshes any open detail view.
            refreshPlaylists()
        }
    }

    private var recommendationSeed: String? = null

    /**
     * Refreshes "songs you might like".
     *
     * Seeded from the library rather than from playback: the point is to suggest
     * things based on what you keep, and a suggestion shelf that empties out the
     * moment nothing is playing is not much use on a home screen. Falls back to
     * history, then to the playing track, so a phone with an empty cache still gets
     * something.
     */
    fun refreshRecommendations() {
        viewModelScope.launch {
            val svc = connection.service.value ?: return@launch
            val seed = _cacheState.value.entries.firstOrNull()?.videoId
                ?: _recentlyPlayed.value.firstOrNull()?.videoId
                ?: _uiState.value.videoId
                ?: return@launch
            if (seed == recommendationSeed && _recommended.value.isNotEmpty()) return@launch
            recommendationSeed = seed
            val suggestions = runCatching { svc.recommendationsFor(seed) }.getOrElse { return@launch }
            // Never suggest something already sitting in the library.
            val known = _cacheState.value.entries.mapTo(mutableSetOf()) { it.videoId }
            _recommended.value = suggestions.filterNot { it.videoId in known }
            resolveArtworkFor(_recommended.value.map { it.videoId })
        }
    }

    /**
     * Re-reads the queue from the service.
     *
     * The queue is otherwise only refreshed when the track changes, but the pool it
     * comes from depends on shuffle and loop mode too — so opening the queue sheet
     * after toggling either would otherwise show a stale list.
     */
    fun refreshQueue() {
        viewModelScope.launch {
            val svc = connection.service.value ?: return@launch
            val queue = svc.currentQueue()
            _queue.value = queue
            resolveArtworkForIds(queue.map { it.first })
        }
    }

    fun refreshSettings() {
        _settings.value = connection.service.value?.settingsSnapshot() ?: return
    }

    /**
     * Forces every cover image to be fetched again.
     *
     * Three caches hold artwork and all three have to go, or the stale image simply
     * comes back from whichever one was missed:
     *  - the service's videoId -> URL map (via [UiCommand.RefreshArtworkCache]),
     *  - Coil 3's caches, used by the app's own screens,
     *  - Coil 2's caches, used by the vendored PixelPlayer player. Both Coil majors
     *    are on the classpath (see build.gradle.kts) and keep separate stores.
     */
    @OptIn(coil.annotation.ExperimentalCoilApi::class) // Coil 2's diskCache accessor
    fun refreshArtwork() {
        val context = getApplication<Application>()
        sendCommand(UiCommand.RefreshArtworkCache)
        _artworkUrls.value = emptyMap()
        inFlightArtwork.clear()

        val coil3Loader = coil3.SingletonImageLoader.get(context)
        coil3Loader.memoryCache?.clear()
        val coil2Loader = coil.Coil.imageLoader(context)
        coil2Loader.memoryCache?.clear()

        // Disk eviction is blocking IO, so it does not belong on the main thread.
        viewModelScope.launch(Dispatchers.IO) {
            runCatching { coil3Loader.diskCache?.clear() }
            runCatching { coil2Loader.diskCache?.clear() }
            // Re-read the current URL so the player rebinds against the emptied caches.
            connection.service.value?.let { svc ->
                _uiState.value.videoId?.let { _artworkUrl.value = svc.artworkUrlFor(it) }
            }
        }
    }

    fun refreshCache() {
        viewModelScope.launch {
            connection.service.value?.let { svc ->
                _cacheState.value = svc.cacheSnapshot()
                _cachedIds.value = svc.cachedVideoIds()
            }
        }
    }

    fun clearError() {
        _errorMessage.value = null
    }

    override fun onCleared() {
        connection.unbind()
        sourceConnection.unbind()
        super.onCleared()
    }
}

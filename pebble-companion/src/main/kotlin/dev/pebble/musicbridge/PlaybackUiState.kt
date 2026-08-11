package dev.pebble.musicbridge

/**
 * Everything the phone UI needs to render the Now Playing screen and transport controls.
 * Published by [PebblePlaybackService.publishState] as a [kotlinx.coroutines.flow.StateFlow].
 */
data class PlaybackUiState(
    val playbackState: Int = PlaybackState.IDLE,
    val generation: Int = 0,
    val videoId: String? = null,
    val title: String? = null,
    val artist: String? = null,
    val durationMs: Long = 0,
    val positionMs: Long = 0,
    val phoneAudio: Boolean = false,
    val volume: Int = 50,
    val loopEnabled: Boolean = false,
    val loopMode: Int = LoopMode.OFF,
    val shuffleEnabled: Boolean = false,
    val isFavorite: Boolean = false,
    val theme: Int = 3, // ThemeDefault
    val queueName: String? = null,
    val queueSize: Int = 0,
) {
    val canSkip: Boolean
        get() = loopMode == LoopMode.ALL || shuffleEnabled

    object PlaybackState {
        const val IDLE = 0
        const val BUFFERING = 1
        const val PLAYING = 2
        const val PAUSED = 3
    }

    object LoopMode {
        const val OFF = 0
        const val ONE = 1
        const val ALL = 2
    }
}

/**
 * Everything the Settings screen shows. Read through
 * [PebblePlaybackService.settingsSnapshot]; writes go back one field at a time as
 * [UiCommand]s, which all funnel into the same `applySyncedSettings` the watch uses,
 * so phone and watch can never disagree about configuration.
 */
data class SettingsUiState(
    val theme: Int = 3,
    val phoneAudio: Boolean = false,
    val phoneVolume: Int = 50,
    val cacheEnabled: Boolean = true,
    val cacheSizeMb: Int = 250,
    val cacheRadio: Boolean = false,
    val coverArtBackground: Boolean = false,
    val watchHighQuality: Boolean = false,
    val phoneHighQuality: Boolean = false,
) {
    companion object {
        const val MIN_CACHE_SIZE_MB = 100
        const val MAX_CACHE_SIZE_MB = 1000
        const val CACHE_SIZE_STEP_MB = 50

        /** Labels for the watch's accent themes, indexed by the persisted theme int. */
        val THEME_NAMES = listOf("Teal", "Purple", "Sunset", "Default", "Mono", "Arcade")
    }
}

/**
 * One search hit, for the Search screen. A public mirror of the service's internal
 * `PipePipeResolver.SearchResult`, which cannot cross the module boundary.
 */
data class SearchResultItem(
    val videoId: String,
    val title: String,
    val artist: String,
)

/**
 * Snapshot of the on-disk cache for the Cache screen.
 */
data class CacheUiState(
    val songsCached: Int = 0,
    val usedBytes: Long = 0,
    val budgetBytes: Long = 0,
    val deviceFreeBytes: Long = 0,
    val entries: List<CachedSongEntry> = emptyList(),
)

data class CachedSongEntry(
    val videoId: String,
    val title: String,
    val artist: String,
    val cachedBytes: Long,
)

/** One playlist header, for the Library's Playlists tab and the watch's Playlists row. */
data class PlaylistInfo(
    val id: String,
    val name: String,
    val songCount: Int,
    /** First song in the playlist; its cover doubles as the playlist's artwork. */
    val firstVideoId: String?,
)

/**
 * Commands the phone UI can send to the playback service. These are playback actions
 * and phone-local maintenance only — shared settings (route default, volumes, quality
 * tiers, cache policy, cover art, theme) are owned by the watch and Clay, and arrive
 * through `commandSyncSettings`; the phone deliberately has no settings UI.
 */
sealed class UiCommand {
    data object Pause : UiCommand()
    data object Resume : UiCommand()
    data object Next : UiCommand()
    data object Previous : UiCommand()
    data object Stop : UiCommand()
    data class SeekTo(val positionMs: Long) : UiCommand()
    data object ToggleShuffle : UiCommand()
    data object ToggleLoop : UiCommand()

    /** Toggles a favorite; a null [videoId] targets the currently playing track. */
    data class ToggleFavorite(val videoId: String? = null) : UiCommand()
    data class SetAudioRoute(val toPhone: Boolean) : UiCommand()
    data class Play(val videoId: String) : UiCommand()
    data class JumpQueue(val videoId: String) : UiCommand()

    /** Plays a playlist as an ordered queue, starting at [startVideoId] or its first song. */
    data class PlayPlaylist(val playlistId: String, val startVideoId: String? = null) : UiCommand()
    data class DeleteCachedSong(val videoId: String) : UiCommand()

    /** Empties the media cache except the currently playing track (its spans are in use). */
    data object ClearCache : UiCommand()

    /**
     * Drops every resolved cover-art URL so the next request re-fetches from
     * YouTube. Clearing the bitmap caches is the UI layer's job — the service
     * only owns the videoId -> URL map. See [DreamwaveViewModel.refreshArtwork].
     */
    data object RefreshArtworkCache : UiCommand()
}

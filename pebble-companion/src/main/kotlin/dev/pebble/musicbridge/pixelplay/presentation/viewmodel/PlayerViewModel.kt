package dev.pebble.musicbridge.pixelplay.presentation.viewmodel

import androidx.compose.runtime.Immutable
import dev.pebble.musicbridge.pixelplay.data.model.Artist
import dev.pebble.musicbridge.pixelplay.data.model.Lyrics
import dev.pebble.musicbridge.pixelplay.data.model.Song
import dev.pebble.musicbridge.pixelplay.data.preferences.AlbumArtQuality
import dev.pebble.musicbridge.pixelplay.data.repository.LyricsSearchResult
import dev.pebble.musicbridge.pixelplay.utils.ValidatedLyricsImport
import kotlinx.collections.immutable.ImmutableList
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

@Immutable
data class PlaybackAudioMetadata(
    val mediaId: String? = null,
    val mimeType: String? = null,
    val bitrate: Int? = null,
    val sampleRate: Int? = null,
    val channelCount: Int? = null,
    val bitDepth: Int? = null,
)

/**
 * ADAPTER — the one deliberately non-vendored file in this package.
 *
 * `FullPlayerContent` is vendored verbatim and takes upstream's `PlayerViewModel`.
 * Upstream's is 3117 lines and transitively pulls in Hilt, Room, MediaStore, cast
 * and the lyrics stack — the whole app. This stands in for it, exposing only the
 * ~20 members FullPlayerContent actually touches, backed by whatever the caller
 * pushes in from `DreamwaveViewModel`.
 *
 * Of those members, most are lyrics, AI translation and library navigation, none
 * of which exist in a watch-audio companion; they are no-ops. The genuine
 * playback surface is small: [stablePlayerState], [currentPlaybackPosition],
 * [playPause], [seekTo], [showAndPlaySong].
 *
 * Keeping the shape identical to upstream is what lets FullPlayerContent.kt stay
 * byte-for-byte vendored, so it can be re-synced from upstream by re-running the
 * package rewrite. Do not add app logic here — push it through [update] instead.
 */
class PlayerViewModel(
    private val onPlayPauseRequested: () -> Unit = {},
    private val onSeekRequested: (Long) -> Unit = {},
    private val onToastRequested: (String) -> Unit = {},
    private val onPlaySongRequested: (Song) -> Unit = {},
) {
    @Immutable
    data class FullPlayerSlice(
        val currentSongArtists: List<Artist> = emptyList(),
        val lyricsSyncOffset: Int = 0,
        val albumArtQuality: AlbumArtQuality = AlbumArtQuality.MEDIUM,
        val audioMetadata: PlaybackAudioMetadata = PlaybackAudioMetadata(),
        val showPlayerFileInfo: Boolean = true,
        val immersiveLyricsEnabled: Boolean = false,
        val immersiveLyricsTimeout: Long = 4000L,
        val isImmersiveTemporarilyDisabled: Boolean = false,
        val isRemotePlaybackActive: Boolean = false,
        val selectedRouteName: String? = null,
        val isBluetoothEnabled: Boolean = false,
        val bluetoothName: String? = null,
    )

    private val _stablePlayerState = MutableStateFlow(StablePlayerState())
    val stablePlayerState: StateFlow<StablePlayerState> = _stablePlayerState.asStateFlow()

    private val _currentPlaybackPosition = MutableStateFlow(0L)
    val currentPlaybackPosition: StateFlow<Long> = _currentPlaybackPosition.asStateFlow()

    private val _fullPlayerSlice = MutableStateFlow(FullPlayerSlice())
    val fullPlayerSlice: StateFlow<FullPlayerSlice> = _fullPlayerSlice.asStateFlow()

    private val _lyricsSearchUiState =
        MutableStateFlow<LyricsSearchUiState>(LyricsSearchUiState.Idle)
    val lyricsSearchUiState: StateFlow<LyricsSearchUiState> = _lyricsSearchUiState.asStateFlow()

    /** Single write path from the host app's state. */
    fun update(state: StablePlayerState, position: Long, slice: FullPlayerSlice) {
        _stablePlayerState.value = state
        _currentPlaybackPosition.value = position
        _fullPlayerSlice.value = slice
    }

    // ── Wired to real playback ──────────────────────────────────────────────
    fun playPause() = onPlayPauseRequested()

    fun seekTo(positionMs: Long) = onSeekRequested(positionMs)

    fun sendToast(message: String) = onToastRequested(message)

    /**
     * Play a song the user picked out of the *current* queue.
     *
     * Upstream uses this to both replace the queue and start a track; here the only
     * caller is the album carousel, which can only ever land on something already in
     * the queue — so `contextSongs` and `queueName` are redundant and the host maps
     * this onto a jump that leaves the surrounding order intact.
     *
     * This being a no-op is what made swiping the cover art animate and then snap
     * back without ever changing track.
     */
    @Suppress("UNUSED_PARAMETER")
    fun showAndPlaySong(
        song: Song,
        contextSongs: ImmutableList<Song>,
        queueName: String,
        indexInQueue: Int = 0,
    ) = onPlaySongRequested(song)

    // ── No-ops: features this companion does not have ───────────────────────
    // Signatures mirror upstream so the vendored call sites compile unchanged.

    fun setImmersiveTemporarilyDisabled(disabled: Boolean) {
        _fullPlayerSlice.value =
            _fullPlayerSlice.value.copy(isImmersiveTemporarilyDisabled = disabled)
    }

    @Suppress("UNUSED_PARAMETER")
    fun fetchLyricsForCurrentSong(forcePick: Boolean) = Unit
    fun resetLyricsForCurrentSong() = Unit
    fun resetLyricsSearchState() = Unit
    fun translateLyricsViaAi() = Unit

    @Suppress("UNUSED_PARAMETER")
    fun acceptLyricsSearchResultForCurrentSong(result: LyricsSearchResult) = Unit

    @Suppress("UNUSED_PARAMETER")
    fun searchLyricsManually(title: String, artist: String?) = Unit

    @Suppress("UNUSED_PARAMETER")
    fun importLyricsFromFile(songId: Long, import: ValidatedLyricsImport) = Unit

    @Suppress("UNUSED_PARAMETER")
    fun setLyricsSyncOffset(songId: String, offsetMs: Int) = Unit

    @Suppress("UNUSED_PARAMETER")
    fun saveLyricsToFile(song: Song, lyrics: Lyrics, synced: Boolean) = Unit

    @Suppress("UNUSED_PARAMETER")
    fun triggerArtistNavigationFromPlayer(artistId: Long) = Unit

    @Suppress("UNUSED_PARAMETER")
    fun triggerAlbumNavigationFromPlayer(albumId: Long) = Unit
}

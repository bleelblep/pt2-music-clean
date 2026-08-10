package dev.pebble.musicbridge.ui.nowplaying

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.media3.common.Player
import dev.pebble.musicbridge.PlaybackUiState
import dev.pebble.musicbridge.UiCommand
import dev.pebble.musicbridge.pixelplay.data.model.Song
import dev.pebble.musicbridge.pixelplay.data.preferences.CarouselStyle
import dev.pebble.musicbridge.pixelplay.data.preferences.FullPlayerLoadingTweaks
import dev.pebble.musicbridge.pixelplay.presentation.components.player.FullPlayerContent
import dev.pebble.musicbridge.pixelplay.presentation.components.player.LocalShowLyricsButton
import dev.pebble.musicbridge.pixelplay.presentation.viewmodel.PlaybackAudioMetadata
import dev.pebble.musicbridge.pixelplay.presentation.viewmodel.PlayerSheetState
import dev.pebble.musicbridge.pixelplay.presentation.viewmodel.PlayerViewModel
import dev.pebble.musicbridge.pixelplay.presentation.viewmodel.StablePlayerState
import dev.pebble.musicbridge.ui.DreamwaveViewModel
import dev.pebble.musicbridge.ui.components.SongRow
import kotlinx.collections.immutable.ImmutableList
import kotlinx.collections.immutable.persistentListOf
import kotlinx.collections.immutable.toImmutableList

/**
 * Hosts PixelPlayer's vendored [FullPlayerContent] against this app's playback
 * state.
 *
 * Upstream drives that composable as the expanded half of a bottom sheet that
 * collapses to a mini-player, and [PlayerSheet] reproduces that here: the real
 * 0..1 [expansionProvider] is what drives FullPlayerContent's own expansion
 * animations, so the player animates open rather than just appearing.
 *
 * Defaults to fully expanded with no collapse affordance, for callers that show it
 * as a plain screen.
 */
@Composable
fun DreamwavePlayer(
    viewModel: DreamwaveViewModel,
    expansionProvider: () -> Float = { 1f },
    onCollapse: (() -> Unit)? = null,
) {
    val state by viewModel.uiState.collectAsState()
    val position by viewModel.position.collectAsState()
    val artworkUrl by viewModel.artworkUrl.collectAsState()
    val artworkUrls by viewModel.artworkUrls.collectAsState()
    val queue by viewModel.queue.collectAsState()
    var showQueue by remember { mutableStateOf(false) }

    val isPlaying = state.playbackState == PlaybackUiState.PlaybackState.PLAYING

    // Read through a live reference: the adapter is remembered once, so capturing
    // `isPlaying` directly would freeze it at whatever it was on first composition.
    val isPlayingNow by rememberUpdatedState(isPlaying)
    val playerViewModel = remember {
        PlayerViewModel(
            onPlayPauseRequested = {
                viewModel.sendCommand(if (isPlayingNow) UiCommand.Pause else UiCommand.Resume)
            },
            onSeekRequested = { viewModel.sendCommand(UiCommand.SeekTo(it)) },
        )
    }

    val currentSong = remember(
        state.videoId, state.title, state.artist, state.durationMs,
        artworkUrl, artworkUrls, state.isFavorite,
    ) {
        state.videoId?.let { id -> state.toSong(id, artworkUrls[id] ?: artworkUrl) }
    }
    // The carousel draws a cover per queue entry, so each one needs its own URL —
    // building these without artwork is what left the player blank.
    val queueSongs: ImmutableList<Song> = remember(queue, artworkUrls) {
        if (queue.isEmpty()) persistentListOf()
        else queue.map { (videoId, meta) ->
            Song.emptySong().copy(
                id = videoId,
                title = meta.first,
                artist = meta.second,
                albumArtUriString = artworkUrls[videoId],
            )
        }.toImmutableList()
    }
    val repeatMode = when (state.loopMode) {
        PlaybackUiState.LoopMode.ONE -> Player.REPEAT_MODE_ONE
        PlaybackUiState.LoopMode.ALL -> Player.REPEAT_MODE_ALL
        else -> Player.REPEAT_MODE_OFF
    }

    // Single write path into the adapter — see PlayerViewModel's docs.
    LaunchedEffect(currentSong, isPlaying, state, position, queueSongs) {
        playerViewModel.update(
            state = StablePlayerState(
                currentSong = currentSong,
                currentMediaItemIndex = queueSongs.indexOfFirst { it.id == state.videoId },
                isPlaying = isPlaying,
                playWhenReady = isPlaying,
                totalDuration = state.durationMs,
                isShuffleEnabled = state.shuffleEnabled,
                repeatMode = repeatMode,
                isBuffering = state.playbackState == PlaybackUiState.PlaybackState.BUFFERING,
            ),
            position = position,
            slice = PlayerViewModel.FullPlayerSlice(
                // The watch/phone route reuses upstream's remote-playback chrome:
                // "playing somewhere else" is exactly what routing to the watch means.
                isRemotePlaybackActive = !state.phoneAudio,
                selectedRouteName = if (state.phoneAudio) null else "Pebble",
                audioMetadata = PlaybackAudioMetadata(mediaId = state.videoId),
                showPlayerFileInfo = false,
            ),
        )
    }

    CompositionLocalProvider(LocalShowLyricsButton provides false) {
    FullPlayerContent(
        currentSong = currentSong,
        currentPlaybackQueue = queueSongs,
        currentQueueSourceName = state.queueName ?: "Now Playing",
        currentMediaItemIndex = queueSongs.indexOfFirst { it.id == state.videoId },
        isShuffleEnabled = state.shuffleEnabled,
        shuffleTransitionInProgress = false,
        repeatMode = repeatMode,
        expansionFractionProvider = expansionProvider,
        currentSheetState = if (expansionProvider() > 0.5f) {
            PlayerSheetState.EXPANDED
        } else {
            PlayerSheetState.COLLAPSED
        },
        carouselStyle = CarouselStyle.NO_PEEK,
        loadingTweaks = remember { FullPlayerLoadingTweaks(delayAll = false) },
        playerViewModel = playerViewModel,
        currentPositionProvider = { position },
        isPlayingProvider = { isPlaying },
        playWhenReadyProvider = { isPlaying },
        isFavoriteProvider = { state.isFavorite },
        repeatModeProvider = { repeatMode },
        isShuffleEnabledProvider = { state.shuffleEnabled },
        totalDurationProvider = { state.durationMs },
        onPlayPause = {
            viewModel.sendCommand(if (isPlaying) UiCommand.Pause else UiCommand.Resume)
        },
        onSeek = { viewModel.sendCommand(UiCommand.SeekTo(it)) },
        onNext = { viewModel.sendCommand(UiCommand.Next) },
        onPrevious = { viewModel.sendCommand(UiCommand.Previous) },
        onCollapse = { onCollapse?.invoke() },
        // The chevron is shown only when there is somewhere to collapse *to*. Lyrics
        // stay hidden either way — there is no lyrics provider here.
        showCollapseButton = onCollapse != null,
        onShowQueueClicked = {
            viewModel.refreshQueue()
            showQueue = true
        },
        onQueueDragStart = { },
        onQueueDrag = { },
        onQueueRelease = { _, _ -> },
        // Upstream's cast button routes audio to another device; here that is the
        // watch/phone toggle, which is the same idea with two fixed endpoints.
        onShowCastClicked = {
            viewModel.sendCommand(UiCommand.SetAudioRoute(toPhone = !state.phoneAudio))
        },
        onShuffleToggle = { viewModel.sendCommand(UiCommand.ToggleShuffle) },
        onRepeatToggle = { viewModel.sendCommand(UiCommand.ToggleLoop) },
        onFavoriteToggle = { viewModel.sendCommand(UiCommand.ToggleFavorite()) },
    )
    }

    if (showQueue) {
        QueueSheet(
            queue = queue,
            artworkUrls = artworkUrls,
            currentId = state.videoId,
            isPlaying = isPlaying,
            queueName = state.queueName ?: "Now Playing",
            onPlay = { viewModel.sendCommand(UiCommand.JumpQueue(it)) },
            onDismiss = { showQueue = false },
        )
    }
}

/**
 * The queue, as a bottom sheet.
 *
 * Ordered current-first, the way `sendQueue` orders it for the watch, so the list
 * reads as "playing, then up next" rather than as a raw pool you have to find
 * yourself in. Tapping jumps rather than restarting the queue — [UiCommand.JumpQueue]
 * keeps the surrounding order intact.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun QueueSheet(
    queue: List<Pair<String, Pair<String, String>>>,
    artworkUrls: Map<String, String>,
    currentId: String?,
    isPlaying: Boolean,
    queueName: String,
    onPlay: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    val scheme = MaterialTheme.colorScheme
    val ordered = remember(queue, currentId) {
        val start = queue.indexOfFirst { it.first == currentId }.takeIf { it >= 0 } ?: 0
        queue.subList(start, queue.size) + queue.subList(0, start)
    }

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        containerColor = scheme.surfaceContainerLow,
        contentColor = scheme.onSurface,
    ) {
        Column(modifier = Modifier.padding(horizontal = 16.dp)) {
            Text(
                text = queueName,
                style = MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.Bold,
                color = scheme.primary,
                modifier = Modifier.padding(start = 4.dp, bottom = 2.dp),
            )
            Text(
                text = if (ordered.size == 1) "1 track" else "${ordered.size} tracks",
                style = MaterialTheme.typography.bodyMedium,
                color = scheme.onSurfaceVariant,
                modifier = Modifier.padding(start = 4.dp, bottom = 14.dp),
            )
            if (ordered.isEmpty()) {
                Text(
                    text = "Nothing queued.",
                    style = MaterialTheme.typography.bodyLarge,
                    color = scheme.onSurfaceVariant,
                    modifier = Modifier.padding(start = 4.dp, bottom = 32.dp),
                )
            } else {
                LazyColumn(
                    contentPadding = PaddingValues(bottom = 32.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    items(ordered, key = { it.first }) { (videoId, meta) ->
                        SongRow(
                            title = meta.first,
                            artist = meta.second,
                            artworkUrl = artworkUrls[videoId],
                            isCurrent = videoId == currentId,
                            isPlaying = isPlaying,
                            onClick = { onPlay(videoId) },
                        )
                    }
                }
            }
        }
    }
}

/** Maps the service's flat playback state onto the Song shape the player expects. */
private fun PlaybackUiState.toSong(videoId: String, artworkUrl: String?): Song =
    Song.emptySong().copy(
        id = videoId,
        title = title ?: "Unknown title",
        artist = artist.orEmpty(),
        album = queueName.orEmpty(),
        albumArtUriString = artworkUrl,
        duration = durationMs,
        isFavorite = isFavorite,
    )

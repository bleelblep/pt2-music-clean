package dev.pebble.musicbridge.pixelplay.presentation.components

import androidx.compose.material3.ColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.TextStyle
import dev.pebble.musicbridge.pixelplay.data.model.Lyrics
import dev.pebble.musicbridge.pixelplay.data.model.Song
import dev.pebble.musicbridge.pixelplay.data.repository.LyricsSearchResult
import dev.pebble.musicbridge.pixelplay.presentation.viewmodel.LyricsSearchUiState
import dev.pebble.musicbridge.pixelplay.presentation.viewmodel.StablePlayerState
import kotlinx.coroutines.flow.StateFlow

/**
 * ADAPTER — not vendored.
 *
 * FullPlayerContent is vendored verbatim, so it still references the lyrics
 * surfaces. This companion streams audio from the watch and has no lyrics
 * provider, so these render nothing; the buttons that open them are hidden in
 * DreamwavePlayer rather than left as dead ends.
 *
 * Signatures match upstream exactly. If lyrics ever land here, delete this file
 * and vendor LyricsSheet.kt / FetchLyricsDialog.kt in its place — nothing else
 * has to change.
 */
@Composable
@Suppress("UNUSED_PARAMETER")
fun LyricsSheet(
    stablePlayerStateFlow: StateFlow<StablePlayerState>,
    playbackPositionFlow: StateFlow<Long>,
    lyricsSearchUiState: LyricsSearchUiState,
    resetLyricsForCurrentSong: () -> Unit,
    onSearchLyrics: (Boolean) -> Unit,
    onPickResult: (LyricsSearchResult) -> Unit,
    onManualSearch: (String, String?) -> Unit,
    onImportLyrics: () -> Unit,
    onDismissLyricsSearch: () -> Unit,
    lyricsSyncOffset: Int,
    onLyricsSyncOffsetChange: (Int) -> Unit,
    lyricsTextStyle: TextStyle,
    colorScheme: ColorScheme,
    onBackClick: () -> Unit,
    onSeekTo: (Long) -> Unit,
    onPlayPause: () -> Unit,
    onNext: () -> Unit,
    onPrev: () -> Unit,
    immersiveLyricsEnabled: Boolean,
    immersiveLyricsTimeout: Long,
    isImmersiveTemporarilyDisabled: Boolean,
    onSetImmersiveTemporarilyDisabled: (Boolean) -> Unit,
    onSaveLyricsToFile: (Song, Lyrics, Boolean) -> Unit,
    // Not passed at our vendored commit (the MIT-era call site pre-dates the feature);
    // default keeps this stub compatible with both call shapes.
    onTranslateViaAi: () -> Unit = {},
    isShuffleEnabled: Boolean,
    repeatMode: Int,
    isFavoriteProvider: () -> Boolean,
    onShuffleToggle: () -> Unit,
    onRepeatToggle: () -> Unit,
    onFavoriteToggle: () -> Unit,
    modifier: Modifier = Modifier,
) = Unit

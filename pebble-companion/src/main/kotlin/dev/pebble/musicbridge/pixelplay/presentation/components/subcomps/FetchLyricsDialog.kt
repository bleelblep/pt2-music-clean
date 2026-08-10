package dev.pebble.musicbridge.pixelplay.presentation.components.subcomps

import androidx.compose.runtime.Composable
import dev.pebble.musicbridge.pixelplay.data.model.Song
import dev.pebble.musicbridge.pixelplay.data.repository.LyricsSearchResult
import dev.pebble.musicbridge.pixelplay.presentation.viewmodel.LyricsSearchUiState

/**
 * ADAPTER — not vendored. See LyricsSurfaceStubs.kt for why.
 * Signature matches upstream's FetchLyricsDialog exactly.
 */
@Composable
@Suppress("UNUSED_PARAMETER")
fun FetchLyricsDialog(
    uiState: LyricsSearchUiState,
    currentSong: Song?,
    onConfirm: (Boolean) -> Unit,
    onPickResult: (LyricsSearchResult) -> Unit,
    onManualSearch: (String, String?) -> Unit,
    onDismiss: () -> Unit,
    onImport: () -> Unit,
) = Unit

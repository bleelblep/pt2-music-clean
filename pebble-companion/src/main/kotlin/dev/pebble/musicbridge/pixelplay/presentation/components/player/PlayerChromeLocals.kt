package dev.pebble.musicbridge.pixelplay.presentation.components.player

import androidx.compose.runtime.compositionLocalOf

/**
 * LOCAL ADDITION — not upstream.
 *
 * The lyrics button is rendered deep inside `SongMetadataDisplaySection`, two
 * composables below `FullPlayerContent`. Threading a flag down would mean editing
 * four upstream signatures and two call sites; a CompositionLocal reaches the same
 * two guard points with a far smaller diff, which keeps re-syncing cheap.
 *
 * Defaults to `true`, so upstream's behaviour is unchanged unless a caller opts out.
 */
val LocalShowLyricsButton = compositionLocalOf { true }

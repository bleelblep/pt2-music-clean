package dev.pebble.musicbridge.pixelplay.data.repository

import dev.pebble.musicbridge.pixelplay.data.model.Lyrics

/**
 * ADAPTER — not vendored.
 *
 * Upstream declares this in LyricsRepositoryImpl.kt alongside its LrcLib client.
 * This companion has no lyrics source, so the type exists only to satisfy the
 * signatures of the lyrics surfaces that FullPlayerContent references. `record`
 * is dropped because LrcLibResponse would drag the whole networking layer in.
 */
data class LyricsSearchResult(
    val lyrics: Lyrics,
    val rawLyrics: String,
)

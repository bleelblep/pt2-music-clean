package dev.pebble.musicbridge

import android.content.Context

/**
 * Read-only access to the playback SharedPreferences for the UI layer.
 * The UI *reads* prefs (e.g. to pick the right accent on frame one) but **never writes** —
 * writes always go through [PebblePlaybackService.onUiCommand].
 */
object PlaybackPrefs {
    private const val PREFS_NAME = "dreamwave_playback"
    private const val KEY_THEME = "theme"

    /** Synchronously read the persisted theme index (0..4). Defaults to 3 (Default). */
    fun themeIndex(context: Context): Int =
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            .getInt(KEY_THEME, 3)
            .coerceIn(0, 4)
}

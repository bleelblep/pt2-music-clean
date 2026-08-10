package dev.pebble.musicbridge.ui.theme

import android.content.Context

/** Light, dark, or whatever the phone is set to. */
enum class ThemeMode(val label: String) {
    SYSTEM("System"),
    LIGHT("Light"),
    DARK("Dark"),
}

/**
 * Preferences that belong to the phone UI alone.
 *
 * Deliberately a **separate store** from the playback prefs, and deliberately not a
 * `UiCommand`. Everything in `PlaybackPrefs` is shared state the watch also reads or
 * writes, which is why the UI is only ever allowed to read it — writes go through
 * the service so both ends stay in agreement. Light/dark has no watch meaning at
 * all (the watch has its own theme and its own display), so routing it through the
 * service would add a synced setting that only one end could ever act on.
 */
object UiPrefs {
    private const val PREFS_NAME = "dreamwave_ui"
    private const val KEY_THEME_MODE = "theme_mode"

    fun themeMode(context: Context): ThemeMode {
        val stored = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            .getString(KEY_THEME_MODE, null)
        return ThemeMode.entries.firstOrNull { it.name == stored } ?: ThemeMode.SYSTEM
    }

    fun setThemeMode(context: Context, mode: ThemeMode) {
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            .edit()
            .putString(KEY_THEME_MODE, mode.name)
            .apply()
    }
}

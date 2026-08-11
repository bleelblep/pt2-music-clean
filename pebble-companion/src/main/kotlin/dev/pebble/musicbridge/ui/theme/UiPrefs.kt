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
    private const val KEY_APP_THEME = "app_theme"

    /** [appTheme] value meaning "whatever the watch is set to", the default. */
    const val FOLLOW_WATCH = -1

    /**
     * The phone's own theme, or [FOLLOW_WATCH].
     *
     * The phone used to be strictly slaved to the watch's theme, which is a fine
     * default and a bad rule: the two have completely different displays, and a
     * palette built for a 64-colour reflective LCD in daylight is not automatically
     * the one you want on an OLED phone. This lets them diverge while still keeping
     * every choice on the phone drawn from the watch's own palettes, so picking one
     * here is picking a *watch* theme to wear on the phone, not a separate identity.
     */
    fun appTheme(context: Context): Int =
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            .getInt(KEY_APP_THEME, FOLLOW_WATCH)

    fun setAppTheme(context: Context, theme: Int) {
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            .edit()
            .putInt(KEY_APP_THEME, theme)
            .apply()
    }

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

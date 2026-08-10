package dev.pebble.musicbridge.ui.theme

import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.graphics.Color
import dev.pebble.musicbridge.pixelplay.presentation.viewmodel.ColorSchemePair
import dev.pebble.musicbridge.pixelplay.ui.theme.generateColorSchemeFromSeed
import dev.pebble.musicbridge.pixelplay.ui.theme.generateMonochromeColorSchemeFromSeed

/**
 * The watch's `accent_color()` table. Index matches the persisted `KEY_THEME` int.
 *
 * These are Pebble 64-colour LCD values, not tonal-palette anchors — using one
 * directly as `primary` and leaving the other roles unset is what left the first
 * port with baseline Material greys on a plum background. Instead we hand the
 * accent to PixelPlayer's own scheme generator as a *seed*, which is exactly what
 * upstream does with the colour it extracts from album art. Every role — the five
 * surface container steps, outline, the container pairs — comes out derived and
 * consistent, in the watch's colour.
 */
object WatchAccent {
    val Teal = Color(0xFF00AAAA)
    val Purple = Color(0xFFAA00AA)
    val Sunset = Color(0xFFFF5500)
    val Default = Color(0xFFFF00AA)

    /** The watch's monochrome theme; index 4 has no hue to seed a palette with. */
    const val MONO = 4

    fun seedFor(themeIndex: Int): Color = when (themeIndex) {
        0 -> Teal
        1 -> Purple
        2 -> Sunset
        MONO -> Color(0xFF7A7A7A) // neutral seed; SchemeMonochrome ignores the hue
        else -> Default // 3 = Default
    }
}

/**
 * Builds the light/dark scheme pair for a watch theme index. Cached per index —
 * seed generation runs the HCT quantiser and is not something to redo per frame.
 */
@Composable
fun rememberWatchAccentScheme(themeIndex: Int): ColorSchemePair = remember(themeIndex) {
    val seed = WatchAccent.seedFor(themeIndex)
    if (themeIndex == WatchAccent.MONO) {
        generateMonochromeColorSchemeFromSeed(seed)
    } else {
        generateColorSchemeFromSeed(seed)
    }
}

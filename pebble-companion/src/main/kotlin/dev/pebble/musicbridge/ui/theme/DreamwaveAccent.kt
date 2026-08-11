package dev.pebble.musicbridge.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.graphics.Color
import dev.pebble.musicbridge.pixelplay.presentation.viewmodel.ColorSchemePair
import dev.pebble.musicbridge.pixelplay.ui.theme.generateColorSchemeFromSeed
import dev.pebble.musicbridge.pixelplay.ui.theme.generateMonochromeColorSchemeFromSeed

/**
 * One watch theme, exactly as the watchapp's `bespoke_colors()` defines it. Index
 * matches the `AppTheme` enum in the watchapp and the persisted `KEY_THEME` int.
 *
 * - [ground] is what the watch fills the screen with.
 * - [ink] is the text and glyph colour on that ground.
 * - [accent] fills selected rows, discs and the play button.
 * - [onAccent] is the ink on top of [accent] — black for most, and for Arcade the ground
 *   colour itself, the way the counters inside its digits are the background showing
 *   through.
 */
data class WatchPalette(
    val label: String,
    val ground: Color,
    val ink: Color,
    val accent: Color,
    val onAccent: Color,
)

object WatchAccent {
    val PALETTES = listOf(
        WatchPalette("Dreamwave Teal", Color(0xFF005555), Color.White, Color(0xFF00AAAA), Color.Black),
        WatchPalette("Electric Purple", Color(0xFF550055), Color.White, Color(0xFFAA55AA), Color.White),
        WatchPalette("Sunset", Color(0xFFAA0000), Color.White, Color(0xFFFF5555), Color.Black),
        WatchPalette("Default", Color(0xFFAA0055), Color.White, Color(0xFFFF55FF), Color.Black),
        WatchPalette("Mono", Color(0xFF000000), Color.White, Color(0xFFFFFFFF), Color.Black),
        WatchPalette("Arcade", Color(0xFF550055), Color(0xFF00FFFF), Color(0xFF00FFFF), Color(0xFF550055)),
    )

    /** The watch's monochrome theme; index 4 has no hue to seed a palette with. */
    const val MONO = 4

    fun paletteFor(themeIndex: Int): WatchPalette =
        PALETTES.getOrElse(themeIndex) { PALETTES[3] }

    fun seedFor(themeIndex: Int): Color = when (themeIndex) {
        // Neutral seed; SchemeMonochrome ignores the hue.
        MONO -> Color(0xFF7A7A7A)
        else -> paletteFor(themeIndex).accent
    }
}

private fun mix(a: Color, b: Color, t: Float) = Color(
    red = a.red + (b.red - a.red) * t,
    green = a.green + (b.green - a.green) * t,
    blue = a.blue + (b.blue - a.blue) * t,
    alpha = 1f,
)

/**
 * Repaints a generated scheme in a watch palette.
 *
 * The generator alone could never do this. It runs `SchemeTonalSpot`, where every
 * `on*` role is drawn from Material's **neutral** palette at chroma 6 — the seed's hue
 * is present there only as a tint you cannot see, and tone decides the rest, so in dark
 * mode the ink comes out white no matter what colour is handed in. That is Material
 * working as designed: the seed is meant to move `primary` and the containers while the
 * text roles stay neutral. It is also why the phone looked so much greyer than the
 * watch, which paints saturated ink straight onto a saturated ground.
 *
 * So the generated scheme is kept for its coherence — every exotic role the vendored
 * player reaches for is populated and internally consistent — and the roles that decide
 * what you actually see are overwritten with the watch's own colours, mixed into a
 * surface ramp the same way the watch stacks its ground, cards and accents.
 *
 * The watch has one appearance; the phone has two. Dark wears the palette as-is. Light
 * inverts it — a pale wash of the same hue with deep saturated ink — so light mode is
 * still recognisably the same theme rather than the same grey as every other one.
 */
private fun ColorScheme.wearing(palette: WatchPalette, dark: Boolean): ColorScheme {
    val ground = if (dark) palette.ground else mix(palette.ground, Color.White, 0.88f)
    val ink = if (dark) palette.ink else mix(palette.ground, Color.Black, 0.45f)
    val accent = if (dark) palette.accent else mix(palette.accent, Color.Black, 0.30f)
    val onAccent = if (dark) palette.onAccent else Color.White

    // Cards lift off the ground toward the ink in dark, and settle toward it in light,
    // so the container ramp keeps the same direction of travel as the surface it sits on.
    val lift = if (dark) ink else palette.ground
    fun step(t: Float) = mix(ground, lift, t)

    return copy(
        primary = accent,
        onPrimary = onAccent,
        primaryContainer = if (dark) mix(accent, ground, 0.55f) else mix(accent, ground, 0.70f),
        onPrimaryContainer = ink,
        secondary = accent,
        onSecondary = onAccent,
        secondaryContainer = mix(accent, ground, 0.72f),
        onSecondaryContainer = ink,
        tertiary = accent,
        onTertiary = onAccent,
        tertiaryContainer = mix(accent, ground, 0.72f),
        onTertiaryContainer = ink,
        background = ground,
        onBackground = ink,
        surface = ground,
        onSurface = ink,
        surfaceVariant = step(0.13f),
        onSurfaceVariant = mix(ink, ground, 0.30f),
        surfaceTint = accent,
        surfaceDim = mix(ground, if (dark) Color.Black else palette.ground, 0.15f),
        surfaceBright = step(0.14f),
        surfaceContainerLowest = mix(ground, if (dark) Color.Black else Color.White, 0.10f),
        surfaceContainerLow = step(0.05f),
        surfaceContainer = step(0.09f),
        surfaceContainerHigh = step(0.14f),
        surfaceContainerHighest = step(0.19f),
        outline = mix(ink, ground, 0.55f),
        outlineVariant = mix(ink, ground, 0.75f),
        inverseSurface = ink,
        inverseOnSurface = ground,
        inversePrimary = accent,
    )
}

/**
 * Builds the light/dark scheme pair for a watch theme index. Cached per index —
 * seed generation runs the HCT quantiser and is not something to redo per frame.
 */
@Composable
fun rememberWatchAccentScheme(themeIndex: Int): ColorSchemePair = remember(themeIndex) {
    val seed = WatchAccent.seedFor(themeIndex)
    val base = if (themeIndex == WatchAccent.MONO) {
        generateMonochromeColorSchemeFromSeed(seed)
    } else {
        generateColorSchemeFromSeed(seed)
    }
    val palette = WatchAccent.paletteFor(themeIndex)
    ColorSchemePair(
        light = base.light.wearing(palette, dark = false),
        dark = base.dark.wearing(palette, dark = true),
    )
}

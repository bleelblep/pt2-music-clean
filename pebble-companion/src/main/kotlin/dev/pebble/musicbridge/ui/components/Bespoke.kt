package dev.pebble.musicbridge.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import dev.pebble.musicbridge.R
import dev.pebble.musicbridge.pixelplay.presentation.components.SmartImage
import racra.compose.smooth_corner_rect_library.AbsoluteSmoothCornerShape

/**
 * Shared primitives for the app's own screens, built from the same pieces the
 * vendored PixelPlayer player uses so the two do not read as different apps.
 *
 * The squircle is the load-bearing one: PixelPlayer rounds almost everything with
 * AbsoluteSmoothCornerShape at 60% smoothness rather than a plain RoundedCornerShape,
 * and that continuous curvature is a lot of why its surfaces look the way they do.
 */

/** PixelPlayer's corner treatment: continuous curvature, 60% smoothing. */
fun SquircleShape(corner: Dp): Shape = AbsoluteSmoothCornerShape(
    cornerRadiusTL = corner,
    smoothnessAsPercentTL = 60,
    cornerRadiusTR = corner,
    smoothnessAsPercentTR = 60,
    cornerRadiusBL = corner,
    smoothnessAsPercentBL = 60,
    cornerRadiusBR = corner,
    smoothnessAsPercentBR = 60,
)

/**
 * Depth, as one scale rather than per-call guesses.
 *
 * Material 3 expresses depth tonally — a higher surface is a lighter container, not
 * a drop shadow — and PixelPlayer follows that: its lists and cards are flat, and
 * shadows are reserved for things genuinely floating above the content plane. Using
 * both everywhere is what makes an app look muddy.
 */
enum class Depth {
    /** In-plane: list rows, tiles, section cards. Tonal only, no shadow. */
    Flat,

    /** Lifted within the page: hero cards that should read above the list. */
    Raised,

    /** Genuinely floating over other content: the mini player, dialogs. */
    Floating,
}

private val Depth.shadow: Dp
    get() = when (this) {
        Depth.Flat -> 0.dp
        Depth.Raised -> 2.dp
        Depth.Floating -> 12.dp
    }

/** A squircle surface. Deliberately not Material's Card — depth is [Depth], not elevation overloads. */
@Composable
fun SmoothCard(
    containerColor: Color,
    modifier: Modifier = Modifier,
    corner: Dp = 24.dp,
    depth: Depth = Depth.Flat,
    border: Boolean = false,
    content: @Composable ColumnScope.() -> Unit,
) {
    val shape = SquircleShape(corner)
    Column(
        modifier = modifier
            .fillMaxWidth()
            .then(
                if (depth == Depth.Flat) {
                    Modifier
                } else {
                    // Tinted rather than pure black: a neutral shadow over a plum or
                    // lavender ground reads as grey dirt.
                    Modifier.shadow(
                        elevation = depth.shadow,
                        shape = shape,
                        clip = false,
                        ambientColor = MaterialTheme.colorScheme.scrim,
                        spotColor = MaterialTheme.colorScheme.scrim,
                    )
                }
            )
            .clip(shape)
            .background(containerColor)
            .then(
                if (border) {
                    Modifier.border(1.dp, MaterialTheme.colorScheme.outlineVariant, shape)
                } else {
                    Modifier
                }
            ),
        content = content,
    )
}

/**
 * The one song row, shared by Library, Home and Recently Played. Having three
 * screens grow their own near-identical row is how a UI starts looking assembled
 * rather than designed.
 */
@Composable
fun TrackRow(
    title: String,
    artist: String,
    artworkUrl: String?,
    isCurrent: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    cached: Boolean = false,
    trailing: (@Composable () -> Unit)? = null,
) {
    val scheme = MaterialTheme.colorScheme
    val container = if (isCurrent) scheme.primaryContainer else scheme.surfaceContainer
    val onContainer = if (isCurrent) scheme.onPrimaryContainer else scheme.onSurface
    val onContainerDim =
        if (isCurrent) scheme.onPrimaryContainer.copy(alpha = 0.75f) else scheme.onSurfaceVariant

    SmoothCard(modifier = modifier, corner = 22.dp, containerColor = container) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clickable(onClick = onClick)
                .padding(horizontal = 12.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            TrackArtwork(artworkUrl = artworkUrl, size = 48.dp)
            Spacer(Modifier.width(14.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.titleMedium,
                    color = onContainer,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                Row(verticalAlignment = Alignment.CenterVertically) {
                    if (cached) {
                        OfflineDot(color = onContainerDim)
                        Spacer(Modifier.width(6.dp))
                    }
                    Text(
                        text = artist.ifBlank { "Unknown artist" },
                        style = MaterialTheme.typography.bodySmall,
                        color = onContainerDim,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
            if (trailing != null) {
                Spacer(Modifier.width(8.dp))
                trailing()
            }
        }
    }
}

/**
 * Marks a track as playable without the network.
 *
 * A small filled dot rather than a download icon: every row would otherwise carry a
 * loud glyph, and the useful signal is binary and secondary — "this one is already
 * here". Non-cached rows show nothing at all, so absence reads as the normal case.
 */
@Composable
fun OfflineDot(color: Color, size: Dp = 7.dp) {
    Box(
        modifier = Modifier
            .size(size)
            .clip(androidx.compose.foundation.shape.CircleShape)
            .background(color),
    )
}

/** Square cover with a music-note fallback, clipped to the app's corner treatment. */
@Composable
fun TrackArtwork(artworkUrl: String?, size: Dp, corner: Dp = 14.dp) {
    val scheme = MaterialTheme.colorScheme
    Box(
        modifier = Modifier.size(size).clip(SquircleShape(corner)),
        contentAlignment = Alignment.Center,
    ) {
        if (artworkUrl != null) {
            SmartImage(
                model = artworkUrl,
                contentDescription = null,
                modifier = Modifier.fillMaxSize(),
                contentScale = ContentScale.Crop,
            )
        } else {
            Box(
                modifier = Modifier.fillMaxSize().background(scheme.surfaceContainerHighest),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    painter = painterResource(R.drawable.ic_music_note),
                    contentDescription = null,
                    tint = scheme.onSurfaceVariant,
                    modifier = Modifier.size(size * 0.4f),
                )
            }
        }
    }
}

/** Pill-shaped fill meter, matching the transport buttons' fully-rounded ends. */
@Composable
fun UsageMeter(
    fraction: Float,
    trackColor: Color,
    fillColor: Color,
    modifier: Modifier = Modifier,
    height: Dp = 10.dp,
) {
    Box(
        modifier = modifier
            .fillMaxWidth()
            .height(height)
            .clip(SquircleShape(height))
            .background(trackColor),
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth(fraction.coerceIn(0f, 1f))
                .fillMaxHeight()
                .clip(SquircleShape(height))
                .background(fillColor),
        )
    }
}

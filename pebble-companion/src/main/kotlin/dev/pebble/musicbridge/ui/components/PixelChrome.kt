package dev.pebble.musicbridge.ui.components

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp
import dev.pebble.musicbridge.R
import dev.pebble.musicbridge.pixelplay.presentation.components.subcomps.PlayingEqIcon
import dev.pebble.musicbridge.ui.theme.Motion
import dev.pebble.musicbridge.ui.theme.pressBounce

/**
 * PixelPlayer's chrome vocabulary — the parts every screen is assembled from.
 *
 * The player was vendored from upstream and already looks right; these are the
 * pieces the *rest* of the app needs to read as the same product. Three rules run
 * through all of them, taken from upstream's own screens:
 *
 *  - **Nothing touches an edge.** Bars, rows and the player float on the ground with
 *    a margin, which is why the background colour stays visible everywhere.
 *  - **Circles for actions, pills for choices.** Icon-only affordances are circles;
 *    anything with a label is a full-radius pill.
 *  - **Selection is a fill, not an outline.** The chosen chip, tab or row is a solid
 *    accent shape with inverted content — never a border or a tint shift.
 */

/** Header affordance: a filled circle with an icon in it. Never a bare IconButton. */
@Composable
fun CircleIconButton(
    icon: Int,
    contentDescription: String?,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    size: Dp = 44.dp,
    iconSize: Dp = 21.dp,
    container: Color = MaterialTheme.colorScheme.surfaceContainerHigh,
    contentColor: Color = MaterialTheme.colorScheme.onSurface,
) {
    val interactionSource = remember { MutableInteractionSource() }
    Box(
        modifier = modifier
            .pressBounce(interactionSource, pressedScale = 0.88f)
            .size(size)
            .clip(CircleShape)
            .background(container)
            .clickable(interactionSource = interactionSource, indication = null, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Icon(
            painter = painterResource(icon),
            contentDescription = contentDescription,
            tint = contentColor,
            modifier = Modifier.size(iconSize),
        )
    }
}

/**
 * The small pill upstream parks at the top-left of Home. There it reads "β Beta";
 * here it carries the one piece of status that changes what the buttons below do —
 * where the audio is actually coming out.
 */
@Composable
fun StatusPill(
    icon: Int,
    label: String,
    modifier: Modifier = Modifier,
    container: Color = MaterialTheme.colorScheme.surfaceContainerHigh,
    contentColor: Color = MaterialTheme.colorScheme.onSurface,
    onClick: (() -> Unit)? = null,
) {
    val interactionSource = remember { MutableInteractionSource() }
    Row(
        modifier = modifier
            // Applied unconditionally: without a `clickable` feeding it, the source
            // never emits a press and the scale stays pinned at 1. Making it
            // conditional would put an `animateFloatAsState` behind a branch, which is
            // how you lose a remember slot when the branch flips.
            .pressBounce(interactionSource, pressedScale = 0.94f)
            .clip(CircleShape)
            .background(container)
            .then(
                if (onClick != null) {
                    Modifier.clickable(
                        interactionSource = interactionSource,
                        indication = null,
                        onClick = onClick,
                    )
                } else {
                    Modifier
                }
            )
            .padding(horizontal = 18.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            painter = painterResource(icon),
            contentDescription = null,
            tint = contentColor,
            modifier = Modifier.size(18.dp),
        )
        Spacer(Modifier.width(9.dp))
        Text(
            text = label,
            style = MaterialTheme.typography.titleSmall,
            fontWeight = FontWeight.SemiBold,
            color = contentColor,
        )
    }
}

/**
 * A Library filter chip. Uppercase and tracked-out, selected as a solid primary
 * fill — the loudest single element on that screen, because which slice of the
 * library you are looking at is the thing the whole page hangs on.
 */
@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun CategoryChip(
    label: String,
    selected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val scheme = MaterialTheme.colorScheme
    val container by animateColorAsState(
        targetValue = if (selected) scheme.primary else scheme.surfaceContainerHigh,
        animationSpec = Motion.effects(),
        label = "chipContainer",
    )
    val content by animateColorAsState(
        targetValue = if (selected) scheme.onPrimary else scheme.onSurface,
        animationSpec = Motion.effects(),
        label = "chipContent",
    )
    // Selecting a chip pops it fractionally proud of its unselected neighbours, so
    // the accent fill is not the only thing distinguishing them.
    val selectedScale by animateFloatAsState(
        targetValue = if (selected) 1.05f else 1f,
        animationSpec = Motion.spatialDefault,
        label = "chipScale",
    )
    val interactionSource = remember { MutableInteractionSource() }
    Box(
        modifier = modifier
            .graphicsLayer {
                scaleX = selectedScale
                scaleY = selectedScale
            }
            .pressBounce(interactionSource, pressedScale = 0.94f)
            .clip(CircleShape)
            .background(container)
            .clickable(interactionSource = interactionSource, indication = null, onClick = onClick)
            .padding(horizontal = 26.dp, vertical = 13.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = label.uppercase(),
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Bold,
            letterSpacing = 0.02.em,
            color = content,
            maxLines = 1,
        )
    }
}

/** Labelled action pill — Shuffle, and the toolbar's other verbs. */
@Composable
fun PillButton(
    icon: Int,
    label: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    container: Color = MaterialTheme.colorScheme.tertiaryContainer,
    contentColor: Color = MaterialTheme.colorScheme.onTertiaryContainer,
) {
    val interactionSource = remember { MutableInteractionSource() }
    Row(
        modifier = modifier
            .pressBounce(interactionSource, pressedScale = 0.94f)
            .clip(CircleShape)
            .background(container)
            .clickable(interactionSource = interactionSource, indication = null, onClick = onClick)
            .padding(horizontal = 22.dp, vertical = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            painter = painterResource(icon),
            contentDescription = null,
            tint = contentColor,
            modifier = Modifier.size(20.dp),
        )
        Spacer(Modifier.width(10.dp))
        Text(
            text = label,
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.SemiBold,
            color = contentColor,
            maxLines = 1,
        )
    }
}

/**
 * The list row, everywhere a song appears in a list.
 *
 * Two details carry most of the look and neither is decoration. The cover is a
 * **circle**, which is what stops a scrolling list of squares reading as a grid
 * that lost its second column. And the playing row swaps to a **full pill** on the
 * accent container — a shape change, not just a colour one, so the current track is
 * findable in a flicked scroll without reading a word of it.
 */
@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun SongRow(
    title: String,
    artist: String,
    artworkUrl: String?,
    isCurrent: Boolean,
    isPlaying: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    menuItems: List<RowMenuItem> = emptyList(),
) {
    val scheme = MaterialTheme.colorScheme
    val container by animateColorAsState(
        targetValue = if (isCurrent) scheme.primaryContainer else scheme.surfaceContainer,
        animationSpec = Motion.effects(),
        label = "rowContainer",
    )
    // Spatial: the corner sweeping out to a full pill is the shape change that makes
    // the playing row findable in a flicked scroll, and it overshoots on purpose.
    val corner by animateDpAsState(
        targetValue = if (isCurrent) 40.dp else 18.dp,
        animationSpec = Motion.spatial(),
        label = "rowCorner",
    )
    val onContainer = if (isCurrent) scheme.onPrimaryContainer else scheme.onSurface
    val dim = if (isCurrent) scheme.onPrimaryContainer.copy(alpha = 0.72f) else scheme.onSurfaceVariant
    val interactionSource = remember { MutableInteractionSource() }

    Row(
        modifier = modifier
            .fillMaxWidth()
            .pressBounce(interactionSource, pressedScale = 0.975f)
            .clip(SquircleShape(corner))
            .background(container)
            .clickable(interactionSource = interactionSource, indication = null, onClick = onClick)
            .padding(horizontal = 12.dp, vertical = 11.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(contentAlignment = Alignment.Center) {
            RoundArtwork(artworkUrl = artworkUrl, size = 48.dp)
            if (isCurrent && isPlaying) {
                Box(
                    modifier = Modifier
                        .size(48.dp)
                        .clip(CircleShape)
                        .background(scheme.scrim.copy(alpha = 0.45f)),
                    contentAlignment = Alignment.Center,
                ) {
                    PlayingEqIcon(
                        color = Color.White,
                        isPlaying = true,
                        modifier = Modifier.size(18.dp),
                    )
                }
            }
        }
        Spacer(Modifier.width(14.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = title,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
                color = onContainer,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                text = artist.ifBlank { "<unknown>" },
                style = MaterialTheme.typography.bodyMedium,
                color = dim,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        if (menuItems.isNotEmpty()) {
            Spacer(Modifier.width(6.dp))
            RowOverflow(
                items = menuItems,
                container = if (isCurrent) scheme.onPrimaryContainer else Color.Transparent,
                contentColor = if (isCurrent) scheme.primaryContainer else scheme.onSurfaceVariant,
            )
        }
    }
}

/** One entry in a row's overflow menu. */
data class RowMenuItem(val label: String, val onClick: () -> Unit)

/**
 * The trailing `⋮`. On the playing row it inverts into a filled disc, which is the
 * same fill-don't-outline rule the chips follow.
 */
@Composable
private fun RowOverflow(
    items: List<RowMenuItem>,
    container: Color,
    contentColor: Color,
) {
    var open by remember { mutableStateOf(false) }
    Box {
        CircleIconButton(
            icon = R.drawable.rounded_more_vert_24,
            contentDescription = "More",
            onClick = { open = true },
            size = 38.dp,
            iconSize = 20.dp,
            container = container,
            contentColor = contentColor,
        )
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            items.forEach { entry ->
                DropdownMenuItem(
                    text = { Text(entry.label) },
                    onClick = {
                        open = false
                        entry.onClick()
                    },
                )
            }
        }
    }
}

/** Circular cover, with the same music-note fallback the square one uses. */
@Composable
fun RoundArtwork(artworkUrl: String?, size: Dp, modifier: Modifier = Modifier) {
    val scheme = MaterialTheme.colorScheme
    Box(
        modifier = modifier.size(size).clip(CircleShape).background(scheme.surfaceContainerHighest),
        contentAlignment = Alignment.Center,
    ) {
        if (artworkUrl != null) {
            dev.pebble.musicbridge.pixelplay.presentation.components.SmartImage(
                model = artworkUrl,
                contentDescription = null,
                modifier = Modifier.fillMaxSize(),
                contentScale = androidx.compose.ui.layout.ContentScale.Crop,
            )
        } else {
            Icon(
                painter = painterResource(R.drawable.ic_music_note),
                contentDescription = null,
                tint = scheme.onSurfaceVariant.copy(alpha = 0.7f),
                modifier = Modifier.size(size * 0.42f),
            )
        }
    }
}

/**
 * A screen's title block: oversized name, optional trailing circular buttons.
 * [titleColor] defaults to primary because upstream's Library sets its title in the
 * accent — the page name is the accent's loudest use outside the player.
 */
@Composable
fun ScreenHeader(
    title: String,
    modifier: Modifier = Modifier,
    titleColor: Color = MaterialTheme.colorScheme.primary,
    leading: (@Composable () -> Unit)? = null,
    actions: (@Composable RowScope.() -> Unit)? = null,
) {
    Column(modifier = modifier.fillMaxWidth().padding(horizontal = 20.dp)) {
        if (leading != null) {
            leading()
            Spacer(Modifier.height(14.dp))
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                text = title,
                style = MaterialTheme.typography.displayMedium,
                fontWeight = FontWeight.Bold,
                color = titleColor,
                modifier = Modifier.weight(1f),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            if (actions != null) {
                Spacer(Modifier.width(12.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) { actions() }
            }
        }
    }
}

/**
 * Bottom space the floating chrome covers, measured from the bottom of the window
 * and including the system navigation bar.
 *
 * Scrolling screens add this to their `contentPadding` — they must **not** be
 * shrunk by it. That distinction is the whole difference between a bar that floats
 * and a bar that looks like a footer: if the screen stops above the chrome, the
 * strip behind it is empty page and the bar reads as a solid band welded to the
 * bottom edge. Padding the *content* instead lets the list keep running the full
 * height of the window and slide under the bar, with enough trailing space that the
 * last row can still be scrolled clear of it.
 */
val LocalFloatingChromeInset = compositionLocalOf { 0.dp }

/**
 * The floating bottom bar.
 *
 * Deliberately not `NavigationBar`: Material's version is edge-to-edge and paints
 * its own band across the bottom of the window, which is exactly the thing upstream
 * avoids. This one is an inset pill that the page scrolls underneath — see
 * [LocalFloatingChromeInset] for the half of that which lives in the screens.
 */
@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun FloatingNavBar(
    items: List<NavBarItem>,
    selectedIndex: Int,
    onSelect: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    val scheme = MaterialTheme.colorScheme
    val shape = SquircleShape(30.dp)
    Row(
        modifier = modifier
            .fillMaxWidth()
            // Content passes directly beneath, so the bar needs a real shadow to
            // separate from it — tonal contrast alone is not enough once a cover
            // scrolls under the edge.
            .shadow(
                elevation = 14.dp,
                shape = shape,
                clip = false,
                ambientColor = scheme.scrim,
                spotColor = scheme.scrim,
            )
            .clip(shape)
            .background(scheme.surfaceContainer)
            .padding(vertical = 10.dp),
        horizontalArrangement = Arrangement.SpaceEvenly,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        items.forEachIndexed { index, item ->
            val selected = index == selectedIndex
            // Colour is an effect — critically damped, no overshoot.
            val indicator by animateColorAsState(
                targetValue = if (selected) scheme.secondaryContainer else Color.Transparent,
                animationSpec = Motion.effects(),
                label = "navIndicator",
            )
            val content by animateColorAsState(
                targetValue = if (selected) scheme.primary else scheme.onSurfaceVariant,
                animationSpec = Motion.effects(),
                label = "navContent",
            )
            // Width is spatial, so it overshoots: the pill snaps wide and settles
            // back a hair. A cross-fading indicator alone reads as a light switching
            // on somewhere; growing the shape is what makes the selection feel like
            // it landed where you tapped.
            val indicatorWidth by animateDpAsState(
                targetValue = if (selected) 58.dp else 32.dp,
                animationSpec = Motion.spatial(),
                label = "navIndicatorWidth",
            )
            // The icon rides the same spring a beat behind the pill.
            val iconScale by animateFloatAsState(
                targetValue = if (selected) 1.12f else 1f,
                animationSpec = Motion.spatialFast,
                label = "navIconScale",
            )
            val interactionSource = remember { MutableInteractionSource() }
            Column(
                modifier = Modifier
                    .weight(1f)
                    .pressBounce(interactionSource, pressedScale = 0.9f)
                    .clickable(
                        interactionSource = interactionSource,
                        indication = null,
                    ) { onSelect(index) },
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Box(
                    modifier = Modifier
                        .width(indicatorWidth)
                        .height(32.dp)
                        .clip(CircleShape)
                        .background(indicator),
                    contentAlignment = Alignment.Center,
                ) {
                    Icon(
                        painter = painterResource(item.icon),
                        contentDescription = item.label,
                        tint = content,
                        modifier = Modifier
                            .size(22.dp)
                            .graphicsLayer {
                                scaleX = iconScale
                                scaleY = iconScale
                            },
                    )
                }
                Spacer(Modifier.height(4.dp))
                Text(
                    text = item.label,
                    style = MaterialTheme.typography.labelMedium,
                    fontSize = 12.sp,
                    fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal,
                    color = content,
                )
            }
        }
    }
}

/** One destination in [FloatingNavBar]. */
data class NavBarItem(val label: String, val icon: Int)

/** Height the nav bar occupies, so screens and the player can clear it. */
val FloatingNavBarHeight: Dp = 72.dp

/** Horizontal inset every floating element shares — nav bar, mini player, dialogs. */
val FloatingInset: Dp = 16.dp

/**
 * Says a screen's contents belong to the other music source.
 *
 * The Library, Search and Cache screens are built on the YouTube backend's own history,
 * resolver and disk cache. When Symfonium is the active source those stores are still
 * populated but no longer describe what the watch is playing, so showing them unmarked
 * would be showing stale data as if it were live. Saying so is cheaper — and more honest —
 * than either hiding the screens or faking their contents from Symfonium's library.
 */
@Composable
fun SourceNotice(text: String, modifier: Modifier = Modifier) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .clip(SquircleShape(20.dp))
            .background(MaterialTheme.colorScheme.secondaryContainer)
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = text,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSecondaryContainer,
        )
    }
}

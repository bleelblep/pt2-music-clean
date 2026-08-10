package dev.pebble.musicbridge.ui.home

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.zIndex
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp
import dev.pebble.musicbridge.PlaybackUiState
import dev.pebble.musicbridge.R
import dev.pebble.musicbridge.SearchResultItem
import dev.pebble.musicbridge.UiCommand
import dev.pebble.musicbridge.pixelplay.presentation.components.SmartImage
import dev.pebble.musicbridge.pixelplay.utils.shapes.RoundedStarShape
import dev.pebble.musicbridge.ui.DreamwaveViewModel
import dev.pebble.musicbridge.ui.components.CircleIconButton
import dev.pebble.musicbridge.ui.components.LocalFloatingChromeInset
import dev.pebble.musicbridge.ui.components.RowMenuItem
import dev.pebble.musicbridge.ui.components.SquircleShape
import dev.pebble.musicbridge.ui.components.StatusPill
import dev.pebble.musicbridge.ui.theme.ThemeMode

/**
 * Home, in upstream's idiom rather than a shelf-of-carousels.
 *
 * PixelPlayer's home screen is not a list of rows — it is one big statement: an
 * oversized mix title with a shuffle button, a scattered collage of covers that
 * reads as artwork rather than as data, and exactly one structured card underneath.
 * Reproducing that shape matters more than reproducing its content, because the
 * content upstream shows (daily mixes off a Room database, five streaming-backend
 * dashboards) does not exist in a watch-audio companion.
 *
 * What fills it here: the mix is the phone's own library, the collage is drawn from
 * recent plays, and the card is the recommendation set — which really is "based on
 * history", since [DreamwaveViewModel.refreshRecommendations] seeds it from the
 * library and the play log.
 */
@Composable
fun HomeScreen(
    viewModel: DreamwaveViewModel,
    onOpenCache: () -> Unit,
    onOpenInfo: () -> Unit,
    onOpenLibrary: () -> Unit,
) {
    val recent by viewModel.recentlyPlayed.collectAsState()
    val recommended by viewModel.recommended.collectAsState()
    val cache by viewModel.cacheState.collectAsState()
    val playing by viewModel.uiState.collectAsState()
    val artworkUrls by viewModel.artworkUrls.collectAsState()
    val themeMode by viewModel.themeMode.collectAsState()
    val scheme = MaterialTheme.colorScheme

    LaunchedEffect(Unit) {
        viewModel.refreshCache()
        viewModel.refreshRecentlyPlayed()
        viewModel.refreshRecommendations()
    }

    val topInset = WindowInsets.statusBars.asPaddingValues().calculateTopPadding()

    // The mix is everything playable without asking the network first, newest first,
    // topped up with history so a phone with an empty cache still has something to
    // shuffle.
    val mix = remember(cache.entries, recent) {
        val fromCache = cache.entries.map { SearchResultItem(it.videoId, it.title, it.artist) }
        (fromCache + recent).distinctBy { it.videoId }
    }
    val collageArt = remember(mix, recommended, artworkUrls) {
        (mix + recommended).map { artworkUrls[it.videoId] }.take(5)
    }
    val isPlaying = playing.playbackState == PlaybackUiState.PlaybackState.PLAYING

    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        // Trailing space, not a shorter screen: the collage and the mix card scroll
        // under the mini player and the nav bar, which is what makes those read as
        // floating rather than as a footer.
        contentPadding = PaddingValues(
            top = topInset + 12.dp,
            bottom = LocalFloatingChromeInset.current + 24.dp,
        ),
    ) {
        item {
            Row(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 20.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                // The route is read from the live playback state, not the settings
                // snapshot, so the pill follows watch-side toggles as they happen.
                StatusPill(
                    icon = if (playing.phoneAudio) R.drawable.rounded_mobile_speaker_24 else R.drawable.ic_watch,
                    label = if (playing.phoneAudio) "Phone" else "Watch",
                    onClick = {
                        viewModel.applySetting(UiCommand.SetAudioRoute(toPhone = !playing.phoneAudio))
                    },
                )
                Spacer(Modifier.weight(1f))
                // One overflow holds the phone-local destinations — appearance, the
                // cache viewer, and the pointer to where settings now live — so the
                // header carries a single button instead of a row of them. (Artwork
                // refresh lives on the cache screen, not here.) The menu is the app's
                // own surface: squircle corners, surfaceContainer ground, and each
                // action keeps the icon it had as a header button.
                Box {
                    var menuOpen by remember { mutableStateOf(false) }
                    CircleIconButton(
                        icon = R.drawable.rounded_more_vert_24,
                        contentDescription = "More",
                        onClick = { menuOpen = true },
                    )
                    DropdownMenu(
                        expanded = menuOpen,
                        onDismissRequest = { menuOpen = false },
                        shape = SquircleShape(20.dp),
                        containerColor = scheme.surfaceContainer,
                    ) {
                        @Composable
                        fun MenuAction(icon: Int, label: String, onClick: () -> Unit) {
                            DropdownMenuItem(
                                text = {
                                    Text(
                                        text = label,
                                        style = MaterialTheme.typography.titleSmall,
                                        fontWeight = FontWeight.Medium,
                                        color = scheme.onSurface,
                                    )
                                },
                                leadingIcon = {
                                    Box(
                                        modifier = Modifier
                                            .size(34.dp)
                                            .clip(CircleShape)
                                            .background(scheme.surfaceContainerHigh),
                                        contentAlignment = Alignment.Center,
                                    ) {
                                        Icon(
                                            painter = painterResource(icon),
                                            contentDescription = null,
                                            tint = scheme.onSurfaceVariant,
                                            modifier = Modifier.size(18.dp),
                                        )
                                    }
                                },
                                onClick = {
                                    menuOpen = false
                                    onClick()
                                },
                            )
                        }
                        MenuAction(
                            icon = R.drawable.rounded_palette_24,
                            label = "Appearance: ${themeMode.label}",
                            onClick = {
                                viewModel.setThemeMode(
                                    when (themeMode) {
                                        ThemeMode.SYSTEM -> ThemeMode.LIGHT
                                        ThemeMode.LIGHT -> ThemeMode.DARK
                                        ThemeMode.DARK -> ThemeMode.SYSTEM
                                    }
                                )
                            },
                        )
                        MenuAction(
                            icon = R.drawable.rounded_storage_24,
                            label = "Storage",
                            onClick = onOpenCache,
                        )
                        MenuAction(
                            icon = R.drawable.rounded_info_24,
                            label = "Where did settings go?",
                            onClick = onOpenInfo,
                        )
                    }
                }
            }
        }

        item { Spacer(Modifier.height(52.dp)) }

        item {
            MixHero(
                subtitle = if (mix.isEmpty()) {
                    "Play something to start your mix"
                } else {
                    "${mix.size} tracks ready to play"
                },
                enabled = mix.isNotEmpty(),
                onShuffle = { mix.randomOrNull()?.let { viewModel.sendCommand(UiCommand.Play(it.videoId)) } },
            )
        }

        item { Spacer(Modifier.height(28.dp)) }

        if (collageArt.isNotEmpty()) {
            item { ArtCollage(artworkUrls = collageArt) }
        }

        item { Spacer(Modifier.height(24.dp)) }

        if (recommended.isNotEmpty() || mix.isNotEmpty()) {
            item {
                DailyMixCard(
                    tracks = (recommended.ifEmpty { mix }).take(4),
                    artworkUrls = artworkUrls,
                    currentId = playing.videoId,
                    isPlaying = isPlaying,
                    onPlay = { viewModel.sendCommand(UiCommand.Play(it)) },
                    onOpenAll = onOpenLibrary,
                    modifier = Modifier.padding(horizontal = 16.dp),
                )
            }
        } else {
            item {
                Box(
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 32.dp, vertical = 40.dp),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(
                        text = "Nothing here yet — search for something, or start a song on the watch.",
                        style = MaterialTheme.typography.bodyLarge,
                        color = scheme.onSurfaceVariant,
                    )
                }
            }
        }
    }
}

/**
 * The oversized mix title and its shuffle button.
 *
 * The type is set far larger than any Material scale step and with a line height
 * *under* 1em, so the two words stack into a block rather than reading as a
 * paragraph. That deliberate over-scaling is the screen's whole personality; a
 * headlineLarge here would make it look like every other music app.
 */
@Composable
private fun MixHero(
    subtitle: String,
    enabled: Boolean,
    onShuffle: () -> Unit,
) {
    val scheme = MaterialTheme.colorScheme
    Row(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 20.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = "Your\nMix",
                fontSize = 56.sp,
                lineHeight = 0.98.em,
                fontWeight = FontWeight.Bold,
                color = scheme.onBackground,
            )
            Spacer(Modifier.height(12.dp))
            Text(
                text = subtitle,
                style = MaterialTheme.typography.bodyLarge,
                color = scheme.onSurfaceVariant,
            )
        }
        Spacer(Modifier.width(12.dp))
        Box(
            modifier = Modifier
                .size(92.dp)
                .clip(CircleShape)
                .background(
                    if (enabled) scheme.tertiaryContainer else scheme.surfaceContainerHigh
                )
                .clickable(enabled = enabled, onClick = onShuffle),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                painter = painterResource(R.drawable.rounded_shuffle_24),
                contentDescription = "Shuffle your mix",
                tint = if (enabled) scheme.onTertiaryContainer else scheme.onSurfaceVariant,
                modifier = Modifier.size(36.dp),
            )
        }
    }
}

/**
 * The scattered cover collage.
 *
 * Every slot is hand-placed: five fixed positions, five different shapes, two of
 * them rotated off-axis. The irregularity is the point — a grid of covers is a
 * catalogue, while this reads as a pile of records, which is the impression the
 * screen is going for. Positions are fractions of the available width so the
 * arrangement holds its proportions on any screen.
 */
@Composable
private fun ArtCollage(artworkUrls: List<String?>, modifier: Modifier = Modifier) {
    val slots = remember {
        listOf(
            CollageSlot(xFraction = 0.06f, y = 0.dp, size = 78.dp, shape = CircleShape, rotation = 0f),
            CollageSlot(xFraction = 0.26f, y = 4.dp, size = 196.dp, shape = SquircleShape(88.dp), rotation = -14f),
            CollageSlot(xFraction = 0.76f, y = 132.dp, size = 70.dp, shape = CircleShape, rotation = 0f),
            CollageSlot(xFraction = 0.13f, y = 226.dp, size = 84.dp, shape = SquircleShape(30.dp), rotation = 9f),
            CollageSlot(xFraction = 0.60f, y = 208.dp, size = 116.dp, shape = RoundedStarShape(sides = 9, curve = 0.09, rotation = 12f), rotation = 0f),
        )
    }
    // Tall enough for the lowest slot plus its height; the collage never scrolls
    // internally, so this has to be an honest bound.
    BoxWithWidth(modifier = modifier.fillMaxWidth().height(340.dp)) { width ->
        artworkUrls.forEachIndexed { index, url ->
            val slot = slots.getOrNull(index) ?: return@forEachIndexed
            CollageTile(artworkUrl = url, slot = slot, x = width * slot.xFraction)
        }
    }
}

private data class CollageSlot(
    val xFraction: Float,
    val y: Dp,
    val size: Dp,
    val shape: Shape,
    val rotation: Float,
)

@Composable
private fun CollageTile(artworkUrl: String?, slot: CollageSlot, x: Dp) {
    val scheme = MaterialTheme.colorScheme
    Box(
        modifier = Modifier
            .offset(x = x, y = slot.y)
            .size(slot.size)
            .rotate(slot.rotation)
            .clip(slot.shape),
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
            // A missing cover shows the note alone on the background rather than a
            // filled placeholder tile: upstream's collage does the same, and it keeps
            // the gaps in the pile from turning into extra grey shapes.
            Icon(
                painter = painterResource(R.drawable.ic_music_note),
                contentDescription = null,
                tint = scheme.onSurfaceVariant.copy(alpha = 0.45f),
                modifier = Modifier.size(slot.size * 0.62f),
            )
        }
    }
}

/** Box that hands its resolved width to the content, so slots can be placed by fraction. */
@Composable
private fun BoxWithWidth(modifier: Modifier = Modifier, content: @Composable (Dp) -> Unit) {
    androidx.compose.foundation.layout.BoxWithConstraints(modifier = modifier) {
        content(maxWidth)
    }
}

/**
 * The one structured card on the page: a gradient masthead over a plain list.
 *
 * The gradient runs primary -> tertiary, which is the only place in the app two
 * accent roles meet. It works because both come out of the same seed, so it reads
 * as one colour shifting rather than two colours fighting.
 */
@Composable
private fun DailyMixCard(
    tracks: List<SearchResultItem>,
    artworkUrls: Map<String, String>,
    currentId: String?,
    isPlaying: Boolean,
    onPlay: (String) -> Unit,
    onOpenAll: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val scheme = MaterialTheme.colorScheme
    Column(modifier = modifier.fillMaxWidth().clip(SquircleShape(26.dp))) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(Brush.horizontalGradient(listOf(scheme.primary, scheme.tertiary)))
                .padding(horizontal = 20.dp, vertical = 18.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = "DAILY MIX",
                    style = MaterialTheme.typography.headlineSmall,
                    fontWeight = FontWeight.ExtraBold,
                    letterSpacing = 0.02.em,
                    color = scheme.onPrimary,
                )
                Text(
                    text = "Based on History",
                    style = MaterialTheme.typography.bodyMedium,
                    color = scheme.onPrimary.copy(alpha = 0.8f),
                )
            }
            StackedThumbs(
                artworkUrls = tracks.map { artworkUrls[it.videoId] },
                ringColor = scheme.onPrimary,
            )
        }

        Column(
            modifier = Modifier.fillMaxWidth().background(scheme.surfaceContainer).padding(8.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            tracks.forEach { track ->
                MixRow(
                    track = track,
                    isCurrent = track.videoId == currentId,
                    isPlaying = isPlaying,
                    onClick = { onPlay(track.videoId) },
                )
            }
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(SquircleShape(18.dp))
                    .clickable(onClick = onOpenAll)
                    .padding(horizontal = 14.dp, vertical = 16.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    text = "Check all of Daily Mix",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold,
                    color = scheme.onSurface,
                    modifier = Modifier.weight(1f),
                )
                Icon(
                    painter = painterResource(R.drawable.rounded_arrow_forward_24),
                    contentDescription = null,
                    tint = scheme.onSurface,
                    modifier = Modifier.size(22.dp),
                )
            }
        }
    }
}

/**
 * Rows inside the mix card carry no cover — the card's masthead already showed the
 * artwork, and repeating it would make four more circles competing with the collage
 * directly above.
 */
@Composable
private fun MixRow(
    track: SearchResultItem,
    isCurrent: Boolean,
    isPlaying: Boolean,
    onClick: () -> Unit,
) {
    val scheme = MaterialTheme.colorScheme
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(SquircleShape(18.dp))
            .background(if (isCurrent) scheme.primaryContainer else scheme.surfaceContainerHigh)
            .clickable(onClick = onClick)
            .padding(horizontal = 16.dp, vertical = 13.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = track.title,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
                color = if (isCurrent) scheme.onPrimaryContainer else scheme.onSurface,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                text = track.artist.ifBlank { "<unknown>" },
                style = MaterialTheme.typography.bodyMedium,
                color = if (isCurrent) {
                    scheme.onPrimaryContainer.copy(alpha = 0.72f)
                } else {
                    scheme.onSurfaceVariant
                },
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        Spacer(Modifier.width(10.dp))
        Icon(
            painter = painterResource(
                if (isCurrent && isPlaying) R.drawable.ic_pause else R.drawable.ic_play
            ),
            contentDescription = null,
            tint = if (isCurrent) scheme.onPrimaryContainer else scheme.onSurfaceVariant,
            modifier = Modifier.size(20.dp),
        )
    }
}

/** Three covers fanned out, each ringed in the masthead's own ink so they separate. */
@Composable
private fun StackedThumbs(artworkUrls: List<String?>, ringColor: Color) {
    val scheme = MaterialTheme.colorScheme
    val shown = artworkUrls.take(3)
    Row(horizontalArrangement = Arrangement.spacedBy((-16).dp)) {
        shown.forEachIndexed { index, url ->
            Box(
                modifier = Modifier
                    .size(52.dp)
                    .clip(CircleShape)
                    .background(ringColor)
                    .padding(2.5.dp)
                    .clip(CircleShape)
                    .background(scheme.surfaceContainerHighest)
                    // Later thumbs sit on top, so the fan reads left-to-right.
                    .zIndex(index.toFloat()),
                contentAlignment = Alignment.Center,
            ) {
                if (url != null) {
                    SmartImage(
                        model = url,
                        contentDescription = null,
                        modifier = Modifier.fillMaxSize(),
                        contentScale = ContentScale.Crop,
                    )
                } else {
                    Icon(
                        painter = painterResource(R.drawable.ic_music_note),
                        contentDescription = null,
                        tint = scheme.onSurfaceVariant,
                        modifier = Modifier.size(22.dp),
                    )
                }
            }
        }
    }
}

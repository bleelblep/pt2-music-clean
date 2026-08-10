package dev.pebble.musicbridge.ui.library

import androidx.activity.compose.BackHandler
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
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.pebble.musicbridge.CachedSongEntry
import dev.pebble.musicbridge.PlaybackUiState
import dev.pebble.musicbridge.PlaylistInfo
import dev.pebble.musicbridge.R
import dev.pebble.musicbridge.SearchResultItem
import dev.pebble.musicbridge.UiCommand
import dev.pebble.musicbridge.pixelplay.presentation.components.SmartImage
import dev.pebble.musicbridge.ui.DreamwaveViewModel
import dev.pebble.musicbridge.ui.components.AddToPlaylistDialog
import dev.pebble.musicbridge.ui.components.CategoryChip
import dev.pebble.musicbridge.ui.components.CircleIconButton
import dev.pebble.musicbridge.ui.components.CreatePlaylistDialog
import dev.pebble.musicbridge.ui.components.LocalFloatingChromeInset
import dev.pebble.musicbridge.ui.components.PillButton
import dev.pebble.musicbridge.ui.components.RowMenuItem
import dev.pebble.musicbridge.ui.components.ScreenHeader
import dev.pebble.musicbridge.ui.components.SongRow
import dev.pebble.musicbridge.ui.components.SquircleShape

/** The slices of the library you can look at. Upstream's chip row, with our nouns. */
enum class LibraryTab(val label: String) {
    SONGS("Songs"),
    FAVORITES("Favorites"),
    PLAYLISTS("Playlists"),
    RECENT("Recent"),
}

/** How the song list is ordered. [RECENT] is the service's own order, so it costs nothing. */
enum class LibrarySort(val label: String) {
    RECENT("Recently added"),
    TITLE("Title"),
    ARTIST("Artist"),
    SIZE("Size"),
}

private fun List<CachedSongEntry>.sortedForLibrary(sort: LibrarySort): List<CachedSongEntry> =
    when (sort) {
        LibrarySort.RECENT -> this
        LibrarySort.TITLE -> sortedBy { it.title.lowercase() }
        LibrarySort.ARTIST -> sortedWith(compareBy({ it.artist.lowercase() }, { it.title.lowercase() }))
        LibrarySort.SIZE -> sortedByDescending { it.cachedBytes }
    }

/**
 * Library — everything already on disk, the favorites, plus the play log.
 *
 * The structure is upstream's exactly: accent-coloured page title, a scrolling row
 * of category chips, then a toolbar band holding shuffle on the left and the view
 * controls on the right. Only the categories differ. Upstream offers
 * Songs/Albums/Artists/Playlists off a full music database; this app has cached
 * files, a favorites store and a play log, so it offers Songs/Favorites/Recent and
 * does not invent the two it cannot fill.
 */
@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun LibraryScreen(viewModel: DreamwaveViewModel) {
    val cache by viewModel.cacheState.collectAsState()
    val recent by viewModel.recentlyPlayed.collectAsState()
    val favorites by viewModel.favorites.collectAsState()
    val playing by viewModel.uiState.collectAsState()
    val artworkUrls by viewModel.artworkUrls.collectAsState()
    val cachedIds by viewModel.cachedIds.collectAsState()
    val scheme = MaterialTheme.colorScheme

    var tabName by rememberSaveable { mutableStateOf(LibraryTab.SONGS.name) }
    var sortName by rememberSaveable { mutableStateOf(LibrarySort.RECENT.name) }
    var gridMode by rememberSaveable { mutableStateOf(false) }
    // The Playlists tab's detail subscreen: which playlist is open, if any.
    var openPlaylistId by rememberSaveable { mutableStateOf<String?>(null) }
    var showCreatePlaylist by remember { mutableStateOf(false) }
    // Song the "Add to playlist" picker is targeting, when open.
    var addToPlaylistTarget by remember { mutableStateOf<String?>(null) }
    // Safe parsing: state saved by an older version may name a tab that no longer
    // exists (e.g. the removed Artists tab).
    val tab = runCatching { LibraryTab.valueOf(tabName) }.getOrDefault(LibraryTab.SONGS)
    val sort = runCatching { LibrarySort.valueOf(sortName) }.getOrDefault(LibrarySort.RECENT)
    val playlists by viewModel.playlists.collectAsState()
    val playlistSongs by viewModel.playlistSongs.collectAsState()

    LaunchedEffect(Unit) {
        viewModel.refreshCache()
        viewModel.refreshRecentlyPlayed()
        viewModel.refreshFavorites()
        viewModel.refreshPlaylists()
    }
    LaunchedEffect(cache.entries) {
        viewModel.resolveArtworkForIds(cache.entries.map { it.videoId })
    }
    // Back from a playlist's detail returns to the playlists list, not to Home.
    BackHandler(enabled = openPlaylistId != null) { openPlaylistId = null }

    val songs = remember(cache.entries, sort) { cache.entries.sortedForLibrary(sort) }
    val isPlaying = playing.playbackState == PlaybackUiState.PlaybackState.PLAYING
    val topInset = WindowInsets.statusBars.asPaddingValues().calculateTopPadding()

    fun play(videoId: String) = viewModel.sendCommand(UiCommand.Play(videoId))
    fun menuFor(videoId: String): List<RowMenuItem> = buildList {
        add(RowMenuItem("Play now") { play(videoId) })
        add(RowMenuItem("Add to playlist") { addToPlaylistTarget = videoId })
        if (tab == LibraryTab.FAVORITES) {
            add(RowMenuItem("Remove from favorites") {
                viewModel.sendCommand(UiCommand.ToggleFavorite(videoId))
                viewModel.refreshFavorites()
            })
        }
        if (videoId in cachedIds) {
            add(RowMenuItem("Remove from phone") {
                viewModel.sendCommand(UiCommand.DeleteCachedSong(videoId))
                viewModel.refreshCache()
            })
        }
    }

    // One list for the whole page, header included. The title, chips and toolbar are
    // items rather than a fixed Column above the list, so they scroll away with the
    // content instead of holding a third of the screen while you page through songs.
    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(
            top = topInset + 16.dp,
            bottom = LocalFloatingChromeInset.current + 24.dp,
        ),
    ) {
        item(key = "header") {
            ScreenHeader(title = "Library")
            Spacer(Modifier.height(18.dp))
        }

        item(key = "chips") {
            LazyRow(
                contentPadding = PaddingValues(horizontal = 16.dp),
                horizontalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                items(LibraryTab.entries, key = { it.name }) { entry ->
                    CategoryChip(
                        label = entry.label,
                        selected = tab == entry,
                        onClick = { tabName = entry.name },
                    )
                }
            }
            Spacer(Modifier.height(14.dp))
        }

        // Playlists manage their own header (back + play-all), so the sort/shuffle
        // toolbar only exists for the flat song lists.
        if (tab != LibraryTab.PLAYLISTS) {
            item(key = "toolbar") {
                // Shuffle draws from the tab being viewed.
                val shufflePool = when (tab) {
                    LibraryTab.SONGS -> songs.map { it.videoId }
                    LibraryTab.FAVORITES -> favorites.map { it.videoId }
                    LibraryTab.RECENT -> recent.map { it.videoId }
                    LibraryTab.PLAYLISTS -> emptyList()
                }
                LibraryToolbar(
                    sort = sort,
                    gridMode = gridMode,
                    shuffleEnabled = shufflePool.isNotEmpty(),
                    onShuffle = { shufflePool.randomOrNull()?.let(::play) },
                    onSort = { sortName = it.name },
                    onToggleGrid = { gridMode = !gridMode },
                    showViewControls = true,
                )
                Spacer(Modifier.height(10.dp))
            }
        }

        when (tab) {
            LibraryTab.SONGS -> songItems(
                songs = songs.map { SearchResultItem(it.videoId, it.title, it.artist) },
                artworkUrls = artworkUrls,
                currentId = playing.videoId,
                isPlaying = isPlaying,
                gridMode = gridMode,
                emptyMessage = "Nothing cached yet — play something and it stays here.",
                onPlay = ::play,
                menuFor = ::menuFor,
            )

            LibraryTab.FAVORITES -> songItems(
                songs = favorites,
                artworkUrls = artworkUrls,
                currentId = playing.videoId,
                isPlaying = isPlaying,
                gridMode = gridMode,
                emptyMessage = "No favorites yet — tap the heart while a song plays.",
                onPlay = ::play,
                menuFor = ::menuFor,
            )

            LibraryTab.PLAYLISTS -> {
                val openId = openPlaylistId
                if (openId == null) {
                    playlistCards(
                        playlists = playlists,
                        artworkUrls = artworkUrls,
                        onOpen = { id ->
                            openPlaylistId = id
                            viewModel.openPlaylist(id)
                        },
                        onCreate = { showCreatePlaylist = true },
                        onDelete = { id -> viewModel.deletePlaylist(id) },
                    )
                } else {
                    val open = playlists.firstOrNull { it.id == openId }
                    playlistDetailItems(
                        name = open?.name ?: "Playlist",
                        songs = playlistSongs,
                        artworkUrls = artworkUrls,
                        currentId = playing.videoId,
                        isPlaying = isPlaying,
                        onBack = { openPlaylistId = null },
                        onPlayAll = {
                            viewModel.sendCommand(UiCommand.PlayPlaylist(openId))
                        },
                        onPlayFrom = { videoId ->
                            viewModel.sendCommand(UiCommand.PlayPlaylist(openId, videoId))
                        },
                        onRemove = { videoId ->
                            viewModel.removeFromPlaylist(openId, videoId)
                            viewModel.openPlaylist(openId)
                        },
                    )
                }
            }

            LibraryTab.RECENT -> songItems(
                songs = recent,
                artworkUrls = artworkUrls,
                currentId = playing.videoId,
                isPlaying = isPlaying,
                gridMode = gridMode,
                emptyMessage = "No play history yet.",
                onPlay = ::play,
                menuFor = ::menuFor,
            )
        }
    }

    if (showCreatePlaylist) {
        CreatePlaylistDialog(
            onCreate = { name ->
                viewModel.createPlaylist(name)
                showCreatePlaylist = false
            },
            onDismiss = { showCreatePlaylist = false },
        )
    }

    addToPlaylistTarget?.let { videoId ->
        AddToPlaylistDialog(
            playlists = playlists,
            onSelect = { playlistId ->
                viewModel.addToPlaylist(playlistId, videoId)
                addToPlaylistTarget = null
            },
            onCreateNew = { name ->
                viewModel.createPlaylist(name, addVideoId = videoId)
                addToPlaylistTarget = null
            },
            onDismiss = { addToPlaylistTarget = null },
        )
    }
}

/**
 * The band under the chips: shuffle on the left, view controls on the right.
 * No band background of its own — it sits on the page like the rows below it.
 */
@Composable
private fun LibraryToolbar(
    sort: LibrarySort,
    gridMode: Boolean,
    shuffleEnabled: Boolean,
    showViewControls: Boolean,
    onShuffle: () -> Unit,
    onSort: (LibrarySort) -> Unit,
    onToggleGrid: () -> Unit,
) {
    val scheme = MaterialTheme.colorScheme
    var sortOpen by remember { mutableStateOf(false) }

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        PillButton(
            icon = R.drawable.rounded_shuffle_24,
            label = "Shuffle",
            onClick = { if (shuffleEnabled) onShuffle() },
            container = if (shuffleEnabled) scheme.tertiaryContainer else scheme.surfaceContainerHigh,
            contentColor = if (shuffleEnabled) scheme.onTertiaryContainer else scheme.onSurfaceVariant,
        )
        Spacer(Modifier.weight(1f))
        if (showViewControls) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Box(
                    modifier = Modifier
                        .size(44.dp)
                        .clip(SquircleShape(14.dp))
                        .background(
                            if (gridMode) scheme.secondaryContainer else scheme.surfaceContainerHigh
                        )
                        .clickable(onClick = onToggleGrid),
                    contentAlignment = Alignment.Center,
                ) {
                    Icon(
                        painter = painterResource(
                            if (gridMode) R.drawable.rounded_list_24 else R.drawable.rounded_grid_view_24
                        ),
                        contentDescription = if (gridMode) "Show as list" else "Show as grid",
                        tint = if (gridMode) scheme.onSecondaryContainer else scheme.onSurfaceVariant,
                        modifier = Modifier.size(21.dp),
                    )
                }
                Box {
                    Box(
                        modifier = Modifier
                            .size(44.dp)
                            .clip(SquircleShape(14.dp))
                            .background(scheme.surfaceContainerHigh)
                            .clickable { sortOpen = true },
                        contentAlignment = Alignment.Center,
                    ) {
                        Icon(
                            painter = painterResource(R.drawable.rounded_sort_24),
                            contentDescription = "Sort",
                            tint = scheme.onSurfaceVariant,
                            modifier = Modifier.size(21.dp),
                        )
                    }
                    DropdownMenu(expanded = sortOpen, onDismissRequest = { sortOpen = false }) {
                        LibrarySort.entries.forEach { option ->
                            DropdownMenuItem(
                                text = {
                                    Text(
                                        text = option.label,
                                        fontWeight = if (option == sort) FontWeight.Bold else FontWeight.Normal,
                                        color = if (option == sort) scheme.primary else scheme.onSurface,
                                    )
                                },
                                onClick = {
                                    sortOpen = false
                                    onSort(option)
                                },
                            )
                        }
                    }
                }
            }
        }
    }
}

/**
 * The list body, in either of its two shapes.
 *
 * A `LazyListScope` extension rather than a composable owning its own `LazyColumn`:
 * the page is one list now, and a nested lazy list would both break the shared
 * scroll and force a fixed height on the inner one.
 */
private fun LazyListScope.songItems(
    songs: List<SearchResultItem>,
    artworkUrls: Map<String, String>,
    currentId: String?,
    isPlaying: Boolean,
    gridMode: Boolean,
    emptyMessage: String,
    onPlay: (String) -> Unit,
    menuFor: (String) -> List<RowMenuItem>,
) {
    if (songs.isEmpty()) {
        item(key = "empty") { EmptyBody(emptyMessage) }
        return
    }
    if (gridMode) {
        // Two hand-paired columns rather than LazyVerticalGrid, which cannot be nested
        // in a LazyColumn without being given a fixed height.
        itemsIndexed(songs.chunked(2), key = { _, pair -> pair.first().videoId }) { _, pair ->
            Row(
                modifier = Modifier.padding(horizontal = 16.dp).padding(bottom = 16.dp),
                horizontalArrangement = Arrangement.spacedBy(14.dp),
            ) {
                pair.forEach { song ->
                    SongTile(
                        modifier = Modifier.weight(1f),
                        song = song,
                        artworkUrl = artworkUrls[song.videoId],
                        isCurrent = song.videoId == currentId,
                        onClick = { onPlay(song.videoId) },
                    )
                }
                if (pair.size == 1) Spacer(Modifier.weight(1f))
            }
        }
    } else {
        items(songs, key = { it.videoId }) { song ->
            SongRow(
                title = song.title,
                artist = song.artist,
                artworkUrl = artworkUrls[song.videoId],
                isCurrent = song.videoId == currentId,
                isPlaying = isPlaying,
                onClick = { onPlay(song.videoId) },
                menuItems = menuFor(song.videoId),
                modifier = Modifier.padding(horizontal = 16.dp).padding(bottom = 8.dp),
            )
        }
    }
}

/**
 * The playlists list: a "New playlist" card first, then one card per playlist.
 * Cards reuse SongRow — a playlist row is a title, an "N songs" subtitle and the
 * first song's cover, which is exactly SongRow's shape.
 */
private fun LazyListScope.playlistCards(
    playlists: List<PlaylistInfo>,
    artworkUrls: Map<String, String>,
    onOpen: (String) -> Unit,
    onCreate: () -> Unit,
    onDelete: (String) -> Unit,
) {
    item(key = "newPlaylist") {
        val scheme = MaterialTheme.colorScheme
        Row(
            modifier = Modifier
                .padding(horizontal = 16.dp).padding(bottom = 8.dp)
                .fillMaxWidth()
                .clip(SquircleShape(18.dp))
                .background(scheme.secondaryContainer)
                .clickable(onClick = onCreate)
                .padding(horizontal = 12.dp, vertical = 11.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(
                modifier = Modifier
                    .size(48.dp)
                    .clip(CircleShape)
                    .background(scheme.onSecondaryContainer.copy(alpha = 0.14f)),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    painter = painterResource(R.drawable.ic_add),
                    contentDescription = null,
                    tint = scheme.onSecondaryContainer,
                    modifier = Modifier.size(22.dp),
                )
            }
            Spacer(Modifier.width(14.dp))
            Text(
                text = "New playlist",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
                color = scheme.onSecondaryContainer,
            )
        }
    }
    if (playlists.isEmpty()) {
        item(key = "empty") {
            EmptyBody("No playlists yet — make one, then add songs from their ⋮ menu.")
        }
        return
    }
    items(playlists, key = { it.id }) { playlist ->
        SongRow(
            title = playlist.name,
            artist = if (playlist.songCount == 1) "1 song" else "${playlist.songCount} songs",
            artworkUrl = playlist.firstVideoId?.let { artworkUrls[it] },
            isCurrent = false,
            isPlaying = false,
            onClick = { onOpen(playlist.id) },
            menuItems = listOf(
                RowMenuItem("Delete playlist") { onDelete(playlist.id) },
            ),
            modifier = Modifier.padding(horizontal = 16.dp).padding(bottom = 8.dp),
        )
    }
}

/**
 * One playlist's songs: a small back-and-play header, then the plain song list.
 * Playing starts the playlist as an ordered queue at the tapped song.
 */
private fun LazyListScope.playlistDetailItems(
    name: String,
    songs: List<SearchResultItem>,
    artworkUrls: Map<String, String>,
    currentId: String?,
    isPlaying: Boolean,
    onBack: () -> Unit,
    onPlayAll: () -> Unit,
    onPlayFrom: (String) -> Unit,
    onRemove: (String) -> Unit,
) {
    item(key = "detailHeader") {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp)
                .padding(bottom = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CircleIconButton(
                icon = R.drawable.rounded_arrow_back_24,
                contentDescription = "Back to playlists",
                onClick = onBack,
            )
            Spacer(Modifier.width(14.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = name,
                    style = MaterialTheme.typography.headlineSmall,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.onBackground,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                Text(
                    text = if (songs.size == 1) "1 song" else "${songs.size} songs",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            if (songs.isNotEmpty()) {
                PillButton(
                    icon = R.drawable.ic_play,
                    label = "Play",
                    onClick = onPlayAll,
                )
            }
        }
    }
    if (songs.isEmpty()) {
        item(key = "empty") {
            EmptyBody("No songs yet — add some from their ⋮ menu.")
        }
        return
    }
    items(songs, key = { it.videoId }) { song ->
        SongRow(
            title = song.title,
            artist = song.artist,
            artworkUrl = artworkUrls[song.videoId],
            isCurrent = song.videoId == currentId,
            isPlaying = isPlaying,
            onClick = { onPlayFrom(song.videoId) },
            menuItems = listOf(
                RowMenuItem("Remove from playlist") { onRemove(song.videoId) },
            ),
            modifier = Modifier.padding(horizontal = 16.dp).padding(bottom = 8.dp),
        )
    }
}

/** Grid cell: square cover, name underneath. Squares here, circles in the list. */
@Composable
private fun SongTile(
    song: SearchResultItem,
    artworkUrl: String?,
    isCurrent: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val scheme = MaterialTheme.colorScheme
    Column(modifier = modifier.clickable(onClick = onClick)) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .aspectRatio(1f)
                .clip(SquircleShape(22.dp))
                .background(scheme.surfaceContainerHighest),
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
                Icon(
                    painter = painterResource(R.drawable.ic_music_note),
                    contentDescription = null,
                    tint = scheme.onSurfaceVariant,
                    modifier = Modifier.size(40.dp),
                )
            }
        }
        Spacer(Modifier.height(9.dp))
        Text(
            text = song.title,
            style = MaterialTheme.typography.titleSmall,
            fontWeight = FontWeight.SemiBold,
            color = if (isCurrent) scheme.primary else scheme.onSurface,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        Text(
            text = song.artist.ifBlank { "<unknown>" },
            style = MaterialTheme.typography.bodySmall,
            color = scheme.onSurfaceVariant,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
private fun EmptyBody(message: String) {
    Box(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 40.dp, vertical = 60.dp),
        contentAlignment = Alignment.TopCenter,
    ) {
        Text(
            text = message,
            style = MaterialTheme.typography.bodyLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

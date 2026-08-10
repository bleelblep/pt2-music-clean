package dev.pebble.musicbridge.ui.search

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
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import dev.pebble.musicbridge.PlaybackUiState
import dev.pebble.musicbridge.Protocol
import dev.pebble.musicbridge.R
import dev.pebble.musicbridge.UiCommand
import dev.pebble.musicbridge.ui.DreamwaveViewModel
import dev.pebble.musicbridge.ui.components.AddToPlaylistDialog
import dev.pebble.musicbridge.ui.components.CategoryChip
import dev.pebble.musicbridge.ui.components.RowMenuItem
import dev.pebble.musicbridge.ui.components.LocalFloatingChromeInset
import dev.pebble.musicbridge.ui.components.ScreenHeader
import dev.pebble.musicbridge.ui.components.SongRow

/**
 * Search, over the same YouTube Music / PipePipe resolver the watch uses.
 *
 * It is its own tab rather than a button inside Library, because that is where
 * upstream puts it and because searching is a mode you enter, not an action you
 * take from somewhere else. Cached hits come first and are labelled: anything
 * already on disk plays instantly and costs no data.
 */
@Composable
fun SearchScreen(viewModel: DreamwaveViewModel) {
    val query by viewModel.searchQuery.collectAsState()
    val results by viewModel.searchResults.collectAsState()
    val cached by viewModel.cachedMatches.collectAsState()
    val searching by viewModel.searching.collectAsState()
    val playing by viewModel.uiState.collectAsState()
    val artworkUrls by viewModel.artworkUrls.collectAsState()
    val searchMode by viewModel.searchMode.collectAsState()
    val playlists by viewModel.playlists.collectAsState()
    var addToPlaylistTarget by remember { mutableStateOf<String?>(null) }
    val scheme = MaterialTheme.colorScheme
    val keyboard = LocalSoftwareKeyboardController.current

    val topInset = WindowInsets.statusBars.asPaddingValues().calculateTopPadding()
    val isPlaying = playing.playbackState == PlaybackUiState.PlaybackState.PLAYING

    Column(modifier = Modifier.fillMaxSize().padding(top = topInset)) {
        Spacer(Modifier.height(16.dp))
        ScreenHeader(title = "Search")
        Spacer(Modifier.height(18.dp))

        SearchField(
            query = query,
            onQueryChange = viewModel::onSearchQueryChanged,
            onSubmit = {
                keyboard?.hide()
                viewModel.submitSearch()
            },
        )

        Spacer(Modifier.height(12.dp))

        // The same three modes the watch offers, over the same protocol values:
        // songs, an artist to shuffle (Artist Radio), or a song that seeds a radio
        // queue (Song Radio). Radio results play as queues, not single tracks.
        // Horizontally scrollable (the library chips' idiom) so the labels never
        // clip on narrow screens.
        LazyRow(
            contentPadding = PaddingValues(horizontal = 16.dp),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            items(
                listOf(
                    Protocol.searchModeSong to "Songs",
                    Protocol.searchModeArtist to "Artist Radio",
                    Protocol.searchModeSongRadio to "Song Radio",
                ),
                key = { it.first },
            ) { (mode, label) ->
                CategoryChip(
                    label = label,
                    selected = searchMode == mode,
                    onClick = { viewModel.setSearchMode(mode) },
                )
            }
        }

        Spacer(Modifier.height(14.dp))

        when {
            searching -> Box(
                modifier = Modifier.fillMaxSize(),
                contentAlignment = Alignment.TopCenter,
            ) {
                CircularProgressIndicator(
                    color = scheme.primary,
                    modifier = Modifier.padding(top = 60.dp),
                )
            }

            results.isEmpty() && cached.isEmpty() -> Box(
                modifier = Modifier.fillMaxSize().padding(horizontal = 40.dp),
                contentAlignment = Alignment.TopCenter,
            ) {
                Text(
                    text = if (query.isBlank()) {
                        "Search for something to play."
                    } else {
                        "Nothing on this phone matches — press search to look online."
                    },
                    style = MaterialTheme.typography.bodyLarge,
                    color = scheme.onSurfaceVariant,
                    modifier = Modifier.padding(top = 60.dp),
                )
            }

            else -> LazyColumn(
                contentPadding = PaddingValues(
                    start = 16.dp, end = 16.dp,
                    bottom = LocalFloatingChromeInset.current + 24.dp,
                ),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                // Cached matches are song matches — they mean nothing in radio modes.
                if (cached.isNotEmpty() && searchMode == Protocol.searchModeSong) {
                    item { SectionLabel("On this phone") }
                    items(cached, key = { "cached_${it.videoId}" }) { item ->
                        SongRow(
                            title = item.title,
                            artist = item.artist,
                            artworkUrl = artworkUrls[item.videoId],
                            isCurrent = item.videoId == playing.videoId,
                            isPlaying = isPlaying,
                            onClick = {
                                keyboard?.hide()
                                viewModel.sendCommand(UiCommand.Play(item.videoId))
                            },
                            menuItems = listOf(
                                RowMenuItem("Add to playlist") {
                                    addToPlaylistTarget = item.videoId
                                },
                                RowMenuItem("Remove from phone") {
                                    viewModel.sendCommand(UiCommand.DeleteCachedSong(item.videoId))
                                    viewModel.refreshCache()
                                },
                            ),
                        )
                    }
                }
                if (results.isNotEmpty()) {
                    item {
                        SectionLabel(
                            when (searchMode) {
                                Protocol.searchModeArtist -> "Artists"
                                Protocol.searchModeSongRadio -> "Pick a song to start its radio"
                                else -> "YouTube Music"
                            },
                        )
                    }
                    items(results, key = { it.videoId }) { item ->
                        SongRow(
                            title = item.title,
                            artist = item.artist,
                            artworkUrl = artworkUrls[item.videoId],
                            isCurrent = item.videoId == playing.videoId,
                            isPlaying = isPlaying,
                            onClick = {
                                keyboard?.hide()
                                viewModel.sendCommand(UiCommand.Play(item.videoId))
                            },
                            menuItems = listOf(
                                RowMenuItem("Add to playlist") {
                                    addToPlaylistTarget = item.videoId
                                },
                            ),
                        )
                    }
                }
            }
        }
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
 * The query field, as a pill.
 *
 * `BasicTextField` rather than `TextField` because Material's version brings its own
 * container, indicator line and 56dp minimum with it — three things that would each
 * have to be neutralised to end up back at a plain pill.
 */
@Composable
private fun SearchField(
    query: String,
    onQueryChange: (String) -> Unit,
    onSubmit: () -> Unit,
) {
    val scheme = MaterialTheme.colorScheme
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp)
            .clip(CircleShape)
            .background(scheme.surfaceContainerHigh)
            .padding(horizontal = 20.dp, vertical = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            painter = painterResource(R.drawable.rounded_search_24),
            contentDescription = null,
            tint = scheme.onSurfaceVariant,
            modifier = Modifier.size(22.dp),
        )
        Spacer(Modifier.width(14.dp))
        Box(modifier = Modifier.weight(1f), contentAlignment = Alignment.CenterStart) {
            if (query.isEmpty()) {
                Text(
                    text = "Songs, artists…",
                    style = MaterialTheme.typography.bodyLarge,
                    color = scheme.onSurfaceVariant,
                )
            }
            BasicTextField(
                value = query,
                onValueChange = onQueryChange,
                singleLine = true,
                textStyle = LocalTextStyle.current.merge(
                    MaterialTheme.typography.bodyLarge.copy(color = scheme.onSurface)
                ),
                cursorBrush = SolidColor(scheme.primary),
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
                keyboardActions = KeyboardActions(onSearch = { onSubmit() }),
                modifier = Modifier.fillMaxWidth(),
            )
        }
        if (query.isNotEmpty()) {
            Spacer(Modifier.width(10.dp))
            Box(
                modifier = Modifier
                    .size(26.dp)
                    .clip(CircleShape)
                    .background(scheme.onSurfaceVariant.copy(alpha = 0.18f))
                    .clickable { onQueryChange("") },
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    painter = painterResource(R.drawable.ic_delete),
                    contentDescription = "Clear",
                    tint = scheme.onSurfaceVariant,
                    modifier = Modifier.size(15.dp),
                )
            }
        }
    }
}

@Composable
private fun SectionLabel(text: String) {
    Text(
        text = text,
        style = MaterialTheme.typography.titleSmall,
        fontWeight = FontWeight.Bold,
        letterSpacing = 0.04.em,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        modifier = Modifier.padding(start = 8.dp, top = 12.dp, bottom = 2.dp),
    )
}

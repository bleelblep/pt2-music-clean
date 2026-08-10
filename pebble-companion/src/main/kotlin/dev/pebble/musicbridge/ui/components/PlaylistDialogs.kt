package dev.pebble.musicbridge.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import dev.pebble.musicbridge.PlaylistInfo
import dev.pebble.musicbridge.R

/**
 * The two playlist dialogs, in the app's dialog idiom (squircle, surfaceContainerHigh)
 * — shared by every "Add to playlist" overflow entry and the Playlists tab's own
 * create button.
 */

/** Name-entry dialog for a new playlist. */
@Composable
fun CreatePlaylistDialog(
    onCreate: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    val scheme = MaterialTheme.colorScheme
    var name by remember { mutableStateOf("") }
    AlertDialog(
        onDismissRequest = onDismiss,
        shape = SquircleShape(28.dp),
        containerColor = scheme.surfaceContainerHigh,
        title = { Text("New playlist") },
        text = {
            BasicTextField(
                value = name,
                onValueChange = { name = it },
                singleLine = true,
                textStyle = LocalTextStyle.current.merge(
                    MaterialTheme.typography.bodyLarge.copy(color = scheme.onSurface)
                ),
                cursorBrush = SolidColor(scheme.primary),
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(SquircleShape(14.dp))
                    .background(scheme.surfaceContainer)
                    .padding(horizontal = 16.dp, vertical = 12.dp),
                decorationBox = { inner ->
                    Box {
                        if (name.isEmpty()) {
                            Text(
                                text = "Playlist name",
                                style = MaterialTheme.typography.bodyLarge,
                                color = scheme.onSurfaceVariant,
                            )
                        }
                        inner()
                    }
                },
            )
        },
        confirmButton = {
            TextButton(
                enabled = name.isNotBlank(),
                onClick = { onCreate(name) },
            ) { Text("Create") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        },
    )
}

/**
 * Picks which playlist a song joins. Lists every playlist plus a "New playlist" row
 * that swaps the dialog into name entry without leaving it.
 */
@Composable
fun AddToPlaylistDialog(
    playlists: List<PlaylistInfo>,
    onSelect: (playlistId: String) -> Unit,
    onCreateNew: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    val scheme = MaterialTheme.colorScheme
    var creating by remember { mutableStateOf(false) }

    if (creating) {
        CreatePlaylistDialog(
            onCreate = { name ->
                onCreateNew(name)
                onDismiss()
            },
            onDismiss = onDismiss,
        )
        return
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        shape = SquircleShape(28.dp),
        containerColor = scheme.surfaceContainerHigh,
        title = { Text("Add to playlist") },
        text = {
            LazyColumn(modifier = Modifier.heightIn(max = 320.dp)) {
                items(playlists, key = { it.id }) { playlist ->
                    PlaylistPickerRow(
                        icon = R.drawable.rounded_queue_music_24,
                        label = playlist.name,
                        detail = if (playlist.songCount == 1) "1 song" else "${playlist.songCount} songs",
                        onClick = { onSelect(playlist.id) },
                    )
                }
                item(key = "new") {
                    PlaylistPickerRow(
                        icon = R.drawable.ic_add,
                        label = "New playlist",
                        detail = null,
                        onClick = { creating = true },
                    )
                }
            }
        },
        confirmButton = {},
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        },
    )
}

@Composable
private fun PlaylistPickerRow(
    icon: Int,
    label: String,
    detail: String?,
    onClick: () -> Unit,
) {
    val scheme = MaterialTheme.colorScheme
    androidx.compose.foundation.layout.Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(SquircleShape(14.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 8.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            modifier = Modifier
                .size(34.dp)
                .clip(CircleShape)
                .background(scheme.surfaceContainer),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                painter = painterResource(icon),
                contentDescription = null,
                tint = scheme.onSurfaceVariant,
                modifier = Modifier.size(18.dp),
            )
        }
        Column(modifier = Modifier.padding(start = 12.dp)) {
            Text(
                text = label,
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.Medium,
                color = scheme.onSurface,
            )
            if (detail != null) {
                Text(
                    text = detail,
                    style = MaterialTheme.typography.bodySmall,
                    color = scheme.onSurfaceVariant,
                )
            }
        }
    }
}

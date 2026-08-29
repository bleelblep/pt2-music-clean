package dev.pebble.musicbridge.ui.cache

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
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
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import dev.pebble.musicbridge.CachedSongEntry
import dev.pebble.musicbridge.R
import dev.pebble.musicbridge.UiCommand
import dev.pebble.musicbridge.ui.DreamwaveViewModel
import dev.pebble.musicbridge.ui.components.CircleIconButton
import dev.pebble.musicbridge.ui.components.SourceNotice
import dev.pebble.musicbridge.ui.components.Depth
import dev.pebble.musicbridge.ui.components.LocalFloatingChromeInset
import dev.pebble.musicbridge.ui.components.SmoothCard
import dev.pebble.musicbridge.ui.components.SquircleShape
import dev.pebble.musicbridge.ui.components.UsageMeter

/**
 * Cache management, drawn in the same language as the player: squircles, the
 * surface-container ramp, and the pill controls PixelPlayer uses everywhere.
 */
@Composable
fun CacheScreen(viewModel: DreamwaveViewModel, onBack: () -> Unit) {
    val cache by viewModel.cacheState.collectAsState()
    val isSymfonium by viewModel.isSymfonium.collectAsState()
    val uiState by viewModel.uiState.collectAsState()
    var deleteTarget by remember { mutableStateOf<CachedSongEntry?>(null) }
    var clearAllConfirm by remember { mutableStateOf(false) }

    LaunchedEffect(Unit) { viewModel.refreshCache() }

    val scheme = MaterialTheme.colorScheme
    val topInset = WindowInsets.statusBars.asPaddingValues().calculateTopPadding()

    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(
            start = 16.dp,
            end = 16.dp,
            top = topInset + 16.dp,
            bottom = LocalFloatingChromeInset.current + 24.dp,
        ),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        if (isSymfonium) {
            item {
                SourceNotice(
                    text = "Symfonium is the active source and streams nothing through " +
                        "this cache. What's here belongs to the YouTube backend.",
                )
            }
        }
        item {
            // This screen is reached from two places and had no way out of either;
            // system back was the only exit, which is most of why back felt broken.
            Column(modifier = Modifier.padding(start = 6.dp, bottom = 6.dp)) {
                CircleIconButton(
                    icon = R.drawable.rounded_arrow_back_24,
                    contentDescription = "Back",
                    onClick = onBack,
                )
                Spacer(Modifier.height(14.dp))
                Text(
                    text = "Storage",
                    style = MaterialTheme.typography.displayMedium,
                    fontWeight = FontWeight.Bold,
                    color = scheme.onBackground,
                )
            }
        }

        // Hero: how full the budget is. The single number people actually want.
        item {
            SmoothCard(corner = 28.dp, containerColor = scheme.primaryContainer, depth = Depth.Raised) {
                Column(modifier = Modifier.padding(20.dp)) {
                    Text(
                        text = "STORAGE USED",
                        style = MaterialTheme.typography.labelSmall,
                        color = scheme.onPrimaryContainer.copy(alpha = 0.7f),
                    )
                    Spacer(Modifier.height(8.dp))
                    Row(verticalAlignment = Alignment.Bottom) {
                        Text(
                            text = formatBytes(cache.usedBytes),
                            style = MaterialTheme.typography.displayMedium,
                            color = scheme.onPrimaryContainer,
                        )
                        Spacer(Modifier.width(8.dp))
                        Text(
                            text = "of ${formatBytes(cache.budgetBytes)}",
                            style = MaterialTheme.typography.bodyLarge,
                            color = scheme.onPrimaryContainer.copy(alpha = 0.7f),
                            modifier = Modifier.padding(bottom = 6.dp),
                        )
                    }
                    Spacer(Modifier.height(14.dp))
                    UsageMeter(
                        fraction = if (cache.budgetBytes > 0) {
                            (cache.usedBytes.toFloat() / cache.budgetBytes).coerceIn(0f, 1f)
                        } else 0f,
                        trackColor = scheme.onPrimaryContainer.copy(alpha = 0.18f),
                        fillColor = scheme.onPrimaryContainer,
                    )
                }
            }
        }

        item {
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                StatTile(
                    modifier = Modifier.weight(1f),
                    label = "SONGS",
                    value = "${cache.songsCached}",
                )
                StatTile(
                    modifier = Modifier.weight(1f),
                    label = "DEVICE FREE",
                    value = formatBytes(cache.deviceFreeBytes),
                )
            }
        }

        item {
            ActionPill(
                icon = R.drawable.rounded_broken_image_24,
                label = "Refresh album art",
                detail = "Re-fetch every cover from YouTube",
                onClick = { viewModel.refreshArtwork() },
            )
        }

        if (cache.entries.isNotEmpty()) {
            item {
                ActionPill(
                    icon = R.drawable.ic_delete,
                    label = "Remove all cached songs",
                    detail = "${cache.songsCached} songs, ${formatBytes(cache.usedBytes)} — keeps the one playing",
                    destructive = true,
                    onClick = { clearAllConfirm = true },
                )
            }
        }

        if (cache.entries.isEmpty()) {
            item {
                Box(
                    modifier = Modifier.fillMaxWidth().padding(vertical = 48.dp),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(
                        text = "No cached songs",
                        style = MaterialTheme.typography.titleMedium,
                        color = scheme.onSurfaceVariant,
                    )
                }
            }
        } else {
            item {
                Text(
                    text = "CACHED SONGS",
                    style = MaterialTheme.typography.labelSmall,
                    color = scheme.onSurfaceVariant,
                    modifier = Modifier.padding(start = 10.dp, top = 10.dp, bottom = 2.dp),
                )
            }
            items(cache.entries, key = { it.videoId }) { entry ->
                CachedSongRow(
                    entry = entry,
                    isCurrent = entry.videoId == uiState.videoId,
                    onDelete = { deleteTarget = entry },
                )
            }
        }
    }

    deleteTarget?.let { entry ->
        AlertDialog(
            onDismissRequest = { deleteTarget = null },
            shape = SquircleShape(28.dp),
            containerColor = scheme.surfaceContainerHigh,
            title = { Text("Delete cached song?") },
            text = { Text("${entry.title} — ${formatBytes(entry.cachedBytes)} will be freed.") },
            confirmButton = {
                TextButton(onClick = {
                    viewModel.sendCommand(UiCommand.DeleteCachedSong(entry.videoId))
                    viewModel.refreshCache()
                    deleteTarget = null
                }) { Text("Delete") }
            },
            dismissButton = {
                TextButton(onClick = { deleteTarget = null }) { Text("Cancel") }
            },
        )
    }

    if (clearAllConfirm) {
        AlertDialog(
            onDismissRequest = { clearAllConfirm = false },
            shape = SquircleShape(28.dp),
            containerColor = scheme.surfaceContainerHigh,
            title = { Text("Remove all cached songs?") },
            text = {
                Text(
                    "${cache.songsCached} songs (${formatBytes(cache.usedBytes)}) will be " +
                        "removed. The currently playing song is kept, and your history and " +
                        "favorites are not affected.",
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    viewModel.sendCommand(UiCommand.ClearCache)
                    viewModel.refreshCache()
                    clearAllConfirm = false
                }) { Text("Remove all", color = scheme.error) }
            },
            dismissButton = {
                TextButton(onClick = { clearAllConfirm = false }) { Text("Cancel") }
            },
        )
    }
}

@Composable
private fun StatTile(label: String, value: String, modifier: Modifier = Modifier) {
    val scheme = MaterialTheme.colorScheme
    SmoothCard(
        modifier = modifier,
        corner = 24.dp,
        containerColor = scheme.surfaceContainerHigh,
    ) {
        Column(modifier = Modifier.padding(horizontal = 18.dp, vertical = 16.dp)) {
            Text(
                text = label,
                style = MaterialTheme.typography.labelSmall,
                color = scheme.onSurfaceVariant,
            )
            Spacer(Modifier.height(6.dp))
            Text(
                text = value,
                style = MaterialTheme.typography.headlineSmall,
                color = scheme.onSurface,
                maxLines = 1,
            )
        }
    }
}

@Composable
private fun ActionPill(
    icon: Int,
    label: String,
    detail: String,
    onClick: () -> Unit,
    destructive: Boolean = false,
) {
    val scheme = MaterialTheme.colorScheme
    val container = if (destructive) scheme.errorContainer else scheme.secondaryContainer
    val content = if (destructive) scheme.onErrorContainer else scheme.onSecondaryContainer
    SmoothCard(corner = 24.dp, containerColor = container) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clickable(onClick = onClick)
                .padding(horizontal = 18.dp, vertical = 16.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(
                modifier = Modifier
                    .size(40.dp)
                    .clip(CircleShape)
                    .background(content.copy(alpha = 0.14f)),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    painter = painterResource(icon),
                    contentDescription = null,
                    tint = content,
                    modifier = Modifier.size(22.dp),
                )
            }
            Spacer(Modifier.width(14.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = label,
                    style = MaterialTheme.typography.titleMedium,
                    color = content,
                )
                Text(
                    text = detail,
                    style = MaterialTheme.typography.bodySmall,
                    color = content.copy(alpha = 0.7f),
                )
            }
        }
    }
}

@Composable
private fun CachedSongRow(
    entry: CachedSongEntry,
    isCurrent: Boolean,
    onDelete: () -> Unit,
) {
    val scheme = MaterialTheme.colorScheme
    SmoothCard(
        corner = 22.dp,
        containerColor = if (isCurrent) scheme.tertiaryContainer else scheme.surfaceContainer,
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(start = 18.dp, end = 8.dp, top = 12.dp, bottom = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = entry.title,
                    style = MaterialTheme.typography.titleMedium,
                    color = if (isCurrent) scheme.onTertiaryContainer else scheme.onSurface,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                Text(
                    text = listOf(entry.artist, formatBytes(entry.cachedBytes))
                        .filter { it.isNotBlank() }
                        .joinToString(" · "),
                    style = MaterialTheme.typography.bodySmall,
                    color = if (isCurrent) {
                        scheme.onTertiaryContainer.copy(alpha = 0.75f)
                    } else {
                        scheme.onSurfaceVariant
                    },
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
            Box(
                modifier = Modifier
                    .size(38.dp)
                    .clip(CircleShape)
                    .clickable(onClick = onDelete),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    painter = painterResource(R.drawable.ic_delete),
                    contentDescription = "Delete ${entry.title}",
                    tint = if (isCurrent) {
                        scheme.onTertiaryContainer.copy(alpha = 0.8f)
                    } else {
                        scheme.onSurfaceVariant
                    },
                    modifier = Modifier.size(20.dp),
                )
            }
        }
    }
}

private fun formatBytes(bytes: Long): String = when {
    bytes >= 1024L * 1024L * 1024L -> "%.1f GB".format(bytes / (1024.0 * 1024.0 * 1024.0))
    bytes >= 1024L * 1024L -> "%.0f MB".format(bytes / (1024.0 * 1024.0))
    bytes >= 1024L -> "%.0f KB".format(bytes / 1024.0)
    else -> "$bytes B"
}

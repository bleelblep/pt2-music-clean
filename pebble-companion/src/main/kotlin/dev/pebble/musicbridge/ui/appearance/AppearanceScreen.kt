package dev.pebble.musicbridge.ui.appearance

import androidx.compose.foundation.background
import androidx.compose.foundation.border
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
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import dev.pebble.musicbridge.R
import dev.pebble.musicbridge.ui.DreamwaveViewModel
import dev.pebble.musicbridge.ui.components.CircleIconButton
import dev.pebble.musicbridge.ui.components.Depth
import dev.pebble.musicbridge.ui.components.LocalFloatingChromeInset
import dev.pebble.musicbridge.ui.components.SmoothCard
import dev.pebble.musicbridge.ui.theme.ThemeMode
import dev.pebble.musicbridge.ui.theme.UiPrefs
import dev.pebble.musicbridge.ui.theme.WatchAccent

/**
 * Appearance: the phone's own theme and its light/dark preference.
 *
 * Both settings on this screen are phone-only. Nothing here is sent to the watch — the
 * watch keeps its own theme in its own Advanced menu — which is the whole point of the
 * screen: before it, the phone simply wore whatever the watch was wearing, and there
 * was no way to say otherwise.
 *
 * The themes offered are the watch's, though. Each row shows the two colours that
 * theme actually paints on the watch (its ground and its accent), and the phone builds
 * its Material scheme by seeding from that same accent, so choosing "Arcade" here means
 * the phone is wearing the watch's Arcade, not a separate phone palette that happens to
 * share a name.
 */
@Composable
fun AppearanceScreen(viewModel: DreamwaveViewModel, onBack: () -> Unit) {
    val settings by viewModel.settings.collectAsState()
    val appTheme by viewModel.appTheme.collectAsState()
    val themeMode by viewModel.themeMode.collectAsState()

    val watchTheme = settings.theme.takeIf { it in WatchAccent.PALETTES.indices }
        ?: viewModel.initialTheme
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
        item {
            Column(modifier = Modifier.padding(start = 6.dp, bottom = 6.dp)) {
                CircleIconButton(
                    icon = R.drawable.rounded_arrow_back_24,
                    contentDescription = "Back",
                    onClick = onBack,
                )
                Spacer(Modifier.height(14.dp))
                Text(
                    text = "Appearance",
                    style = MaterialTheme.typography.displayMedium,
                    fontWeight = FontWeight.Bold,
                    color = scheme.onBackground,
                )
                Spacer(Modifier.height(6.dp))
                Text(
                    text = "This phone only. The watch keeps its own theme.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = scheme.onSurfaceVariant,
                )
            }
        }

        item { SectionLabel("THEME") }

        item {
            val follow = WatchAccent.paletteFor(watchTheme)
            ThemeRow(
                label = "Match the watch",
                detail = follow.label,
                ground = follow.ground,
                accent = follow.accent,
                selected = appTheme == UiPrefs.FOLLOW_WATCH,
                onClick = { viewModel.setAppTheme(UiPrefs.FOLLOW_WATCH) },
            )
        }

        itemsIndexed(WatchAccent.PALETTES) { index, palette ->
            ThemeRow(
                label = palette.label,
                detail = if (index == watchTheme) "On the watch now" else null,
                ground = palette.ground,
                accent = palette.accent,
                selected = appTheme == index,
                onClick = { viewModel.setAppTheme(index) },
            )
        }

        item { Spacer(Modifier.height(10.dp)) }
        item { SectionLabel("LIGHT & DARK") }

        item {
            SmoothCard(corner = 24.dp, containerColor = scheme.surfaceContainerHigh) {
                Column(modifier = Modifier.padding(vertical = 4.dp)) {
                    ThemeMode.entries.forEach { mode ->
                        ModeRow(
                            label = mode.label,
                            selected = themeMode == mode,
                            onClick = { viewModel.setThemeMode(mode) },
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun SectionLabel(text: String) {
    Text(
        text = text,
        style = MaterialTheme.typography.labelSmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        modifier = Modifier.padding(start = 10.dp, top = 10.dp, bottom = 2.dp),
    )
}

/**
 * One theme choice. The swatch is the theme as the watch draws it: the ground filled,
 * with the accent as the disc a selected row or the play button would use.
 */
@Composable
private fun ThemeRow(
    label: String,
    detail: String?,
    ground: Color,
    accent: Color,
    selected: Boolean,
    onClick: () -> Unit,
) {
    val scheme = MaterialTheme.colorScheme
    SmoothCard(
        corner = 24.dp,
        containerColor = if (selected) scheme.primaryContainer else scheme.surfaceContainerHigh,
        depth = if (selected) Depth.Raised else Depth.Flat,
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clickable(onClick = onClick)
                .padding(horizontal = 16.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(
                modifier = Modifier
                    .size(42.dp)
                    .clip(RoundedCornerShape(14.dp))
                    .background(ground)
                    .border(1.dp, scheme.outlineVariant, RoundedCornerShape(14.dp)),
                contentAlignment = Alignment.Center,
            ) {
                Box(
                    modifier = Modifier
                        .size(20.dp)
                        .clip(CircleShape)
                        .background(accent),
                )
            }
            Spacer(Modifier.width(14.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = label,
                    style = MaterialTheme.typography.titleMedium,
                    color = if (selected) scheme.onPrimaryContainer else scheme.onSurface,
                )
                if (detail != null) {
                    Text(
                        text = detail,
                        style = MaterialTheme.typography.bodySmall,
                        color = if (selected) {
                            scheme.onPrimaryContainer.copy(alpha = 0.7f)
                        } else {
                            scheme.onSurfaceVariant
                        },
                    )
                }
            }
            if (selected) {
                Icon(
                    painter = painterResource(R.drawable.rounded_check_24),
                    contentDescription = "Selected",
                    tint = scheme.onPrimaryContainer,
                    modifier = Modifier.size(22.dp),
                )
            }
        }
    }
}

@Composable
private fun ModeRow(label: String, selected: Boolean, onClick: () -> Unit) {
    val scheme = MaterialTheme.colorScheme
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(horizontal = 18.dp, vertical = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.titleMedium,
            color = scheme.onSurface,
            modifier = Modifier.weight(1f),
        )
        if (selected) {
            Icon(
                painter = painterResource(R.drawable.rounded_check_24),
                contentDescription = "Selected",
                tint = scheme.primary,
                modifier = Modifier.size(22.dp),
            )
        }
    }
}

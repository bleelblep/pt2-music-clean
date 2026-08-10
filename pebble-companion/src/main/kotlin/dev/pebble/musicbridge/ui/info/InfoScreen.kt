package dev.pebble.musicbridge.ui.info

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import dev.pebble.musicbridge.R
import dev.pebble.musicbridge.ui.components.CircleIconButton
import dev.pebble.musicbridge.ui.components.LocalFloatingChromeInset
import dev.pebble.musicbridge.ui.components.ScreenHeader
import dev.pebble.musicbridge.ui.components.SquircleShape

/**
 * Where settings live, now that the phone app has none of its own.
 *
 * Every shared setting is owned by the watch — its settings blob is authoritative and
 * overwrote whatever a phone editor set on the next sync, so the phone's settings
 * screen was removed rather than mirrored. This screen exists so the removal is
 * discoverable instead of confusing.
 */
@Composable
fun InfoScreen(onBack: () -> Unit) {
    val topInset = WindowInsets.statusBars.asPaddingValues().calculateTopPadding()

    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(
            top = topInset + 16.dp,
            bottom = LocalFloatingChromeInset.current + 32.dp,
        ),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        item {
            ScreenHeader(
                title = "Settings",
                titleColor = MaterialTheme.colorScheme.onBackground,
                leading = {
                    CircleIconButton(
                        icon = R.drawable.rounded_arrow_back_24,
                        contentDescription = "Back",
                        onClick = onBack,
                        container = MaterialTheme.colorScheme.surfaceContainerHigh,
                    )
                },
            )
            Spacer(Modifier.height(18.dp))
        }

        item {
            InfoCard(
                title = "On the watch",
                body = "Open Dreamwave on the watch, then Settings. Input, output, " +
                    "volumes and the progress bar are right there; everything else " +
                    "(theme, caching, cover art, stream quality, library options) is " +
                    "under Settings → Advanced. Advanced unlocks by pressing SELECT on " +
                    "About → Version seven times.",
            )
        }
        item {
            InfoCard(
                title = "From the phone",
                body = "The same settings are in the Pebble app: open Dreamwave's " +
                    "watchapp page and tap Settings. Changes there sync to the watch " +
                    "and this app automatically.",
            )
        }
        item {
            InfoCard(
                title = "On this screen",
                body = "This app keeps only what is local to the phone: light/dark " +
                    "appearance and the cache viewer (both on the Home screen), plus " +
                    "the audio-route toggle during playback.",
            )
        }
    }
}

@Composable
private fun InfoCard(title: String, body: String) {
    val scheme = MaterialTheme.colorScheme
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp)
            .clip(SquircleShape(20.dp))
            .background(scheme.surfaceContainer),
    ) {
        Column(modifier = Modifier.fillMaxWidth().padding(horizontal = 18.dp, vertical = 15.dp)) {
            Text(
                text = title,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
                color = scheme.onSurface,
            )
            Spacer(Modifier.height(6.dp))
            Text(
                text = body,
                style = MaterialTheme.typography.bodyMedium,
                lineHeight = 1.35.em,
                color = scheme.onSurfaceVariant,
            )
        }
    }
}

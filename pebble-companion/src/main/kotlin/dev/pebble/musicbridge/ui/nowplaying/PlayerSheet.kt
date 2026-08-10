package dev.pebble.musicbridge.ui.nowplaying

import androidx.compose.animation.core.Animatable
import androidx.compose.foundation.MutatorMutex
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.MotionScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.util.VelocityTracker
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import dev.pebble.musicbridge.PlaybackUiState
import dev.pebble.musicbridge.UiCommand
import dev.pebble.musicbridge.pixelplay.data.model.Song
import dev.pebble.musicbridge.pixelplay.presentation.components.MiniPlayerContentInternal
import dev.pebble.musicbridge.pixelplay.presentation.components.MiniPlayerHeight
import dev.pebble.musicbridge.pixelplay.presentation.components.scoped.SheetMotionController
import dev.pebble.musicbridge.pixelplay.presentation.components.scoped.SheetVerticalDragGestureHandler
import dev.pebble.musicbridge.pixelplay.presentation.components.scoped.playerSheetVerticalDragGesture
import dev.pebble.musicbridge.pixelplay.presentation.viewmodel.PlayerSheetState
import dev.pebble.musicbridge.ui.DreamwaveViewModel
import dev.pebble.musicbridge.ui.components.FloatingInset
import dev.pebble.musicbridge.ui.components.SquircleShape

/**
 * The player, as a sheet: a mini player parked above the navigation bar that drags
 * up into the full player — how PixelPlayer's own player behaves.
 *
 * Upstream does this with `UnifiedPlayerSheetV2` plus a 29-file `scoped/` package of
 * state controllers, around 4300 lines wired into its navigation graph, cast sheet,
 * offline dialogs and predictive-back handling. Almost none of that applies to a
 * two-endpoint companion, so the *container* here is ours while both things it
 * contains — [MiniPlayerContentInternal] and `FullPlayerContent` — remain upstream's,
 * vendored and unmodified.
 *
 * The motion, though, is upstream's verbatim: `SheetMotionController` keeps the
 * sheet's translationY and expansionFraction animatables in sync under a mutex, and
 * `SheetVerticalDragGestureHandler` runs the drag math, fling thresholds and
 * collapse springs exactly as `UnifiedPlayerSheetV2` drives them — including the
 * release-velocity carry-over that a plain `animateFloatAsState` sheet loses. That
 * fraction is also what drives FullPlayerContent's own expansion animations.
 */
@Composable
fun DreamwavePlayerSheet(
    viewModel: DreamwaveViewModel,
    expanded: Boolean,
    onExpandedChange: (Boolean) -> Unit,
    restingBottomPadding: Dp,
    modifier: Modifier = Modifier,
) {
    val state by viewModel.uiState.collectAsState()
    val artworkUrls by viewModel.artworkUrls.collectAsState()
    val artworkUrl by viewModel.artworkUrl.collectAsState()
    val videoId = state.videoId ?: return

    val isPlaying = state.playbackState == PlaybackUiState.PlaybackState.PLAYING

    val miniSong = remember(videoId, state.title, state.artist, artworkUrls, artworkUrl) {
        Song.emptySong().copy(
            id = videoId,
            title = state.title ?: "Unknown title",
            artist = state.artist.orEmpty(),
            albumArtUriString = artworkUrls[videoId] ?: artworkUrl,
        )
    }

    SheetContainer(
        modifier = modifier,
        expanded = expanded,
        onExpandedChange = onExpandedChange,
        restingBottomPadding = restingBottomPadding,
        collapsed = {
            MiniPlayerContentInternal(
                song = miniSong,
                isPlaying = isPlaying,
                isCastConnecting = false,
                isPreparingPlayback = state.playbackState == PlaybackUiState.PlaybackState.BUFFERING,
                onPlayPause = {
                    viewModel.sendCommand(if (isPlaying) UiCommand.Pause else UiCommand.Resume)
                },
                onPrevious = { viewModel.sendCommand(UiCommand.Previous) },
                // Vestigial at our vendored commit (declared, never read); 12dp matches
                // the corner language of the sheet around it if a re-sync revives it.
                cornerRadiusAlb = 12.dp,
                onNext = { viewModel.sendCommand(UiCommand.Next) },
            )
        },
        expandedContent = { provider ->
            DreamwavePlayer(
                viewModel = viewModel,
                expansionProvider = provider,
                onCollapse = { onExpandedChange(false) },
            )
        },
    )
}

@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
private fun SheetContainer(
    expanded: Boolean,
    onExpandedChange: (Boolean) -> Unit,
    restingBottomPadding: Dp,
    collapsed: @Composable () -> Unit,
    expandedContent: @Composable (expansionProvider: () -> Float) -> Unit,
    modifier: Modifier = Modifier,
) {
    BoxWithConstraints(modifier = modifier.fillMaxSize()) {
        val density = LocalDensity.current
        val scope = rememberCoroutineScope()
        val fullHeightPx = with(density) { maxHeight.toPx() }
        val miniHeightPx = with(density) { MiniPlayerHeight.toPx() }
        // The sheet's travel: expandedY is 0 (full player covering the window),
        // collapsedY parks it one mini-player height short of the bottom edge.
        val collapsedY = (fullHeightPx - miniHeightPx).coerceAtLeast(1f)

        // Vendored upstream motion structure (see UnifiedPlayerSheetV2): a translationY
        // and an expansionFraction animatable kept in lockstep by SheetMotionController,
        // on the expressive spatial spec.
        val motionScheme = remember { MotionScheme.expressive() }
        val sheetAnimationSpec = remember { motionScheme.defaultSpatialSpec<Float>() }
        val translationY = remember { Animatable(if (expanded) 0f else collapsedY) }
        val expansionFraction = remember { Animatable(if (expanded) 1f else 0f) }
        val visualOvershootScaleY = remember { Animatable(1f) }
        val sheetMutex = remember { MutatorMutex() }
        val motionController = remember {
            SheetMotionController(
                translationY = translationY,
                expansionFraction = expansionFraction,
                mutex = sheetMutex,
                defaultAnimationSpec = sheetAnimationSpec,
                expandedY = 0f,
            )
        }

        // Committed-state changes (mini-player tap, collapse button, back press).
        LaunchedEffect(expanded) {
            motionController.animateTo(
                targetExpanded = expanded,
                canExpand = true,
                collapsedY = collapsedY,
            )
        }
        // Keep the mini player anchored when the window size changes under it.
        LaunchedEffect(collapsedY) { motionController.syncToExpansion(collapsedY) }

        val currentExpanded by rememberUpdatedState(expanded)
        val currentCollapsedY by rememberUpdatedState(collapsedY)
        val currentMiniHeightPx by rememberUpdatedState(miniHeightPx)
        val currentOnExpandedChange by rememberUpdatedState(onExpandedChange)
        val dragHandler = remember {
            SheetVerticalDragGestureHandler(
                scope = scope,
                velocityTracker = VelocityTracker(),
                densityProvider = { density },
                sheetMotionController = motionController,
                playerContentExpansionFraction = expansionFraction,
                currentSheetTranslationY = translationY,
                expandedYProvider = { 0f },
                collapsedYProvider = { currentCollapsedY },
                miniHeightPxProvider = { currentMiniHeightPx },
                currentSheetStateProvider = {
                    if (currentExpanded) PlayerSheetState.EXPANDED else PlayerSheetState.COLLAPSED
                },
                visualOvershootScaleY = visualOvershootScaleY,
                onDraggingChange = {},
                onDraggingPlayerAreaChange = {},
                onAnimateSheet = { targetExpanded, animationSpec, initialVelocity ->
                    if (animationSpec == null) {
                        motionController.animateTo(
                            targetExpanded = targetExpanded,
                            canExpand = true,
                            collapsedY = currentCollapsedY,
                            initialVelocity = initialVelocity,
                        )
                    } else {
                        motionController.animateTo(
                            targetExpanded = targetExpanded,
                            canExpand = true,
                            collapsedY = currentCollapsedY,
                            animationSpec = animationSpec,
                            initialVelocity = initialVelocity,
                        )
                    }
                },
                onExpandSheetState = { currentOnExpandedChange(true) },
                onCollapseSheetState = { currentOnExpandedChange(false) },
            )
        }
        val dragModifier = Modifier.playerSheetVerticalDragGesture(enabled = true, handler = dragHandler)

        // NOTE: nothing here may fill the screen while collapsed. An invisible
        // full-size layer still takes touches — Modifier.alpha(0f) hides a composable
        // but does not stop it hit-testing — which is what previously swallowed every
        // tap and scroll in the tabs underneath.
        val expansion = expansionFraction.value
        if (expansion > 0.001f) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .graphicsLayer {
                        this.translationY = translationY.value
                        // Fade the last stretch so the mini player is not visible
                        // ghosting behind a nearly-open sheet.
                        alpha = (expansionFraction.value * 2.2f).coerceIn(0f, 1f)
                    }
                    // The sheet has to paint its own ground. FullPlayerContent draws no
                    // background of its own — upstream renders it inside a sheet
                    // surface that provides one — so without this the tabs underneath
                    // showed straight through the open player.
                    .background(MaterialTheme.colorScheme.background)
                    .then(dragModifier),
            ) {
                expandedContent { expansionFraction.value }
            }
        }

        if (expansion < 0.999f) {
            Box(
                modifier = Modifier
                    .align(Alignment.BottomCenter)
                    .fillMaxWidth()
                    .padding(horizontal = FloatingInset)
                    .padding(bottom = restingBottomPadding)
                    .height(MiniPlayerHeight)
                    .alpha((1f - expansion * 2.4f).coerceIn(0f, 1f))
                    // Collapse settle squash, driven by the vendored drag handler.
                    .graphicsLayer { scaleY = visualOvershootScaleY.value }
                    // primaryContainer is the ground MiniPlayerContentInternal was
                    // written against, not a free choice: its marquee fades to
                    // `gradientEdgeColor = primaryContainer` and its skip buttons are
                    // onPrimary circles. On surfaceContainerHighest the card came out
                    // near-white and the marquee's fade edges did not match it.
                    .shadow(
                        elevation = 12.dp,
                        shape = SquircleShape(26.dp),
                        clip = false,
                        ambientColor = MaterialTheme.colorScheme.scrim,
                        spotColor = MaterialTheme.colorScheme.scrim,
                    )
                    .clip(SquircleShape(26.dp))
                    .background(MaterialTheme.colorScheme.primaryContainer)
                    .then(dragModifier)
                    .clickable(
                        interactionSource = remember { MutableInteractionSource() },
                        indication = null,
                    ) { onExpandedChange(true) },
            ) {
                collapsed()
            }
        }
    }
}

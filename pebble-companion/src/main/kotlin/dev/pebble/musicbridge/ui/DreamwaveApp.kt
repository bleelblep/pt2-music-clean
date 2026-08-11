package dev.pebble.musicbridge.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import dev.pebble.musicbridge.R
import dev.pebble.musicbridge.pixelplay.presentation.components.LocalMaterialTheme
import dev.pebble.musicbridge.pixelplay.presentation.components.MiniPlayerHeight
import dev.pebble.musicbridge.pixelplay.presentation.navigation.TRANSITION_DURATION
import dev.pebble.musicbridge.pixelplay.presentation.navigation.enterTransition
import dev.pebble.musicbridge.pixelplay.presentation.navigation.exitTransition
import dev.pebble.musicbridge.pixelplay.presentation.navigation.popEnterTransition
import dev.pebble.musicbridge.pixelplay.presentation.navigation.popExitTransition
import dev.pebble.musicbridge.pixelplay.ui.theme.PixelPlayTheme
import dev.pebble.musicbridge.ui.cache.CacheScreen
import dev.pebble.musicbridge.ui.components.FloatingInset
import dev.pebble.musicbridge.ui.components.FloatingNavBar
import dev.pebble.musicbridge.ui.components.FloatingNavBarHeight
import dev.pebble.musicbridge.ui.components.LocalFloatingChromeInset
import dev.pebble.musicbridge.ui.components.NavBarItem
import dev.pebble.musicbridge.ui.home.HomeScreen
import dev.pebble.musicbridge.ui.info.InfoScreen
import dev.pebble.musicbridge.ui.library.LibraryScreen
import dev.pebble.musicbridge.ui.nowplaying.DreamwavePlayerSheet
import dev.pebble.musicbridge.ui.search.SearchScreen
import dev.pebble.musicbridge.ui.appearance.AppearanceScreen
import dev.pebble.musicbridge.ui.theme.ThemeMode
import dev.pebble.musicbridge.ui.theme.UiPrefs
import dev.pebble.musicbridge.ui.theme.WatchAccent
import dev.pebble.musicbridge.ui.theme.rememberWatchAccentScheme

/** Bottom-bar destinations — three, as upstream has them. */
enum class DreamwaveTab(val label: String, val icon: Int) {
    HOME("Home", R.drawable.rounded_home_24),
    SEARCH("Search", R.drawable.rounded_search_24),
    LIBRARY("Library", R.drawable.rounded_queue_music_24),
}

/**
 * Screens pushed over a tab: the cache viewer and the settings-directions page.
 * Settings themselves are owned by the watch (watch UI + Clay) and deliberately have
 * no phone editor: the watch's settings blob is authoritative and overwrote whatever
 * the phone UI set on every sync, so a phone-side editor could only ever diverge.
 */
private enum class Subscreen { NONE, CACHE, INFO, APPEARANCE }

/** rememberSaveable stores enum names; a removed value must not crash a state restore. */
private inline fun <reified T : Enum<T>> safeEnumValueOf(name: String, fallback: T): T =
    runCatching { enumValueOf<T>(name) }.getOrDefault(fallback)

@Composable
fun DreamwaveApp(
    viewModel: DreamwaveViewModel = viewModel(),
    openPlayerRequest: kotlinx.coroutines.flow.MutableStateFlow<Boolean>? = null,
) {
    // Track the live theme so changing the accent in Settings recolours immediately.
    // initialTheme is the synchronously-read prefs value, used only until the service
    // binds and publishes the real one — otherwise frame one would flash the default.
    val settings by viewModel.settings.collectAsState()

    // The phone's theme is its own choice now, defaulting to whatever the watch is
    // wearing. Only that fall-back path reads the watch's value, so a theme picked in
    // Appearance no longer moves when the watch's theme moves.
    val appTheme by viewModel.appTheme.collectAsState()
    val watchTheme = settings.theme.takeIf { it in WatchAccent.PALETTES.indices }
        ?: viewModel.initialTheme
    val themeIndex = if (appTheme == UiPrefs.FOLLOW_WATCH) watchTheme else appTheme

    // PixelPlayer's own theme, unmodified. Upstream feeds it a scheme derived from
    // album artwork; we feed it one derived from the watch's accent through the
    // same `colorSchemePairOverride` hook, so phone and watch agree and every
    // Material role stays internally consistent.
    val accentScheme = rememberWatchAccentScheme(themeIndex)

    // Light/dark is the phone's own preference — the watch has its own display and
    // its own theme, so this one deliberately does not sync. Both halves of the
    // scheme pair are generated from the same accent, so switching only swaps which
    // half is in play; the colour identity stays the same.
    val themeMode by viewModel.themeMode.collectAsState()
    val darkTheme = when (themeMode) {
        ThemeMode.SYSTEM -> isSystemInDarkTheme()
        ThemeMode.LIGHT -> false
        ThemeMode.DARK -> true
    }

    PixelPlayTheme(darkTheme = darkTheme, colorSchemePairOverride = accentScheme) {
      // No app-level motion-scheme override: upstream's chrome runs on the theme's
      // default (tween-based) scheme, and the vendored player creates its own
      // MotionScheme.expressive() internally where it wants springs. Screen
      // transitions and the player sheet use vendored PixelPlayer animation code
      // (presentation/navigation/Transitions.kt, scoped/SheetMotionController.kt).
      run {
        var currentTab by rememberSaveable { mutableStateOf(DreamwaveTab.HOME.name) }
        var currentSub by rememberSaveable { mutableStateOf(Subscreen.NONE.name) }
        // Safe parsing: a state restore from an older app version could name a
        // subscreen that no longer exists (e.g. the removed Settings screen).
        val tab = safeEnumValueOf(currentTab, DreamwaveTab.HOME)
        val sub = safeEnumValueOf(currentSub, Subscreen.NONE)

        // Back pops exactly one level.
        BackHandler(enabled = sub != Subscreen.NONE) {
            currentSub = Subscreen.NONE.name
        }
        BackHandler(enabled = sub == Subscreen.NONE && tab != DreamwaveTab.HOME) {
            currentTab = DreamwaveTab.HOME.name
        }

        // The vendored player reads its colours from LocalMaterialTheme rather than
        // MaterialTheme directly — upstream uses it to hand the album-art scheme to
        // the player subtree. Provide the same scheme so those reads resolve.
        CompositionLocalProvider(LocalMaterialTheme provides MaterialTheme.colorScheme) {
            // Surface paints colorScheme.background. Without it the window fell
            // through to the platform theme, which is why the plum never appeared.
            Surface(
                modifier = Modifier.fillMaxSize(),
                color = MaterialTheme.colorScheme.background,
            ) {
                val playing by viewModel.uiState.collectAsState()
                val hasTrack = playing.videoId != null
                var expanded by rememberSaveable { mutableStateOf(false) }

                // Auto-expand the player sheet when launched from the media notification.
                if (openPlayerRequest != null) {
                    val shouldOpen by openPlayerRequest.collectAsState()
                    LaunchedEffect(shouldOpen) {
                        if (shouldOpen && hasTrack) {
                            expanded = true
                            openPlayerRequest.value = false
                        }
                    }
                }

                // Collapse the sheet before the tab-level back handlers get a look in.
                BackHandler(enabled = expanded) { expanded = false }

                val systemBar = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
                val navBarVisible = sub == Subscreen.NONE
                // Everything that floats stacks upward from the system bar: nav bar
                // first, then the mini player above it.
                val navBarBottom = systemBar + 10.dp
                val playerResting =
                    if (navBarVisible) navBarBottom + FloatingNavBarHeight + 8.dp else systemBar + 12.dp
                // How far up the window the chrome reaches. Screens get this as
                // trailing *content* padding and keep their full height — they are
                // never shrunk to sit above the bars, or nothing would ever pass
                // behind them and the bars would stop reading as floating at all.
                val chromeInset = when {
                    hasTrack -> playerResting + MiniPlayerHeight + 12.dp
                    navBarVisible -> navBarBottom + FloatingNavBarHeight + 12.dp
                    else -> systemBar
                }

                Box(modifier = Modifier.fillMaxSize()) {
                    AnimatedContent(
                        targetState = tab to sub,
                        // PixelPlayer's own navigation transitions, vendored verbatim
                        // (presentation/navigation/Transitions.kt): emphasized-curve
                        // slide + scale + fade. Pushing a subscreen is a forward
                        // navigation; popping it — or going back to Home from another
                        // tab — is the pop pair.
                        transitionSpec = {
                            val pushing = targetState.second != Subscreen.NONE
                            val popping = initialState.second != Subscreen.NONE &&
                                targetState.second == Subscreen.NONE
                            val backToHome = !pushing && !popping &&
                                initialState.first != DreamwaveTab.HOME &&
                                targetState.first == DreamwaveTab.HOME
                            if (pushing) {
                                enterTransition() togetherWith exitTransition()
                            } else if (popping || backToHome) {
                                popEnterTransition() togetherWith popExitTransition()
                            } else {
                                enterTransition() togetherWith exitTransition()
                            }
                        },
                        label = "screen",
                        modifier = Modifier.fillMaxSize(),
                    ) { (targetTab, targetSub) ->
                      CompositionLocalProvider(LocalFloatingChromeInset provides chromeInset) {
                        when (targetSub) {
                            Subscreen.CACHE -> CacheScreen(
                                viewModel = viewModel,
                                onBack = { currentSub = Subscreen.NONE.name },
                            )

                            Subscreen.INFO -> InfoScreen(
                                onBack = { currentSub = Subscreen.NONE.name },
                            )

                            Subscreen.APPEARANCE -> AppearanceScreen(
                                viewModel = viewModel,
                                onBack = { currentSub = Subscreen.NONE.name },
                            )

                            Subscreen.NONE -> when (targetTab) {
                                DreamwaveTab.HOME -> HomeScreen(
                                    viewModel = viewModel,
                                    onOpenCache = { currentSub = Subscreen.CACHE.name },
                                    onOpenInfo = { currentSub = Subscreen.INFO.name },
                                    onOpenAppearance = { currentSub = Subscreen.APPEARANCE.name },
                                    onOpenLibrary = { currentTab = DreamwaveTab.LIBRARY.name },
                                )

                                DreamwaveTab.SEARCH -> SearchScreen(viewModel)

                                DreamwaveTab.LIBRARY -> LibraryScreen(viewModel)
                            }
                        }
                      }
                    }

                    // The player floats above the bar rather than beside it, and only
                    // exists once there is something to play — an empty mini player is
                    // just a bar stealing height from every screen.
                    if (hasTrack) {
                        DreamwavePlayerSheet(
                            viewModel = viewModel,
                            expanded = expanded,
                            onExpandedChange = { expanded = it },
                            restingBottomPadding = playerResting,
                        )
                    }

                    // Hidden while the player is open, so the full player owns the
                    // screen the way it does upstream. Specs mirror the vendored
                    // Transitions.kt (350 ms, FastOutSlowInEasing) rather than reading
                    // the ambient motion scheme.
                    AnimatedVisibility(
                        visible = navBarVisible && !expanded,
                        enter = slideInVertically(tween(TRANSITION_DURATION, easing = FastOutSlowInEasing)) { it } +
                            fadeIn(tween(TRANSITION_DURATION, easing = FastOutSlowInEasing)),
                        exit = slideOutVertically(tween(TRANSITION_DURATION / 2, easing = FastOutSlowInEasing)) { it } +
                            fadeOut(tween(TRANSITION_DURATION / 2, easing = FastOutSlowInEasing)),
                        modifier = Modifier.align(Alignment.BottomCenter),
                    ) {
                        FloatingNavBar(
                            items = remember {
                                DreamwaveTab.entries.map { NavBarItem(it.label, it.icon) }
                            },
                            selectedIndex = DreamwaveTab.entries.indexOf(tab),
                            onSelect = { index ->
                                currentSub = Subscreen.NONE.name
                                currentTab = DreamwaveTab.entries[index].name
                            },
                            modifier = Modifier
                                .padding(horizontal = FloatingInset)
                                .padding(bottom = navBarBottom),
                        )
                    }
                }
            }
        }
      }
    }
}

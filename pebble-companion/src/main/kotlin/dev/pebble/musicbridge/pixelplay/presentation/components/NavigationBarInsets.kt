package dev.pebble.musicbridge.pixelplay.presentation.components

import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/**
 * Lifted verbatim from PixelPlayer's PlayerInternalNavigationBar.kt. The rest of
 * that file is the app's bottom navigation bar, which this companion does not
 * use; only this helper is referenced by UnifiedPlayerSheetShared.
 */

// Some OEM freeform/floating-window modes can report a bottom inset close to the whole window height.
internal val MaxNavigationBarBottomInset = 96.dp

internal fun sanitizeNavigationBarBottomInset(systemNavBarInset: Dp): Dp {
    if (!systemNavBarInset.value.isFinite()) return 0.dp
    return systemNavBarInset.coerceIn(0.dp, MaxNavigationBarBottomInset)
}

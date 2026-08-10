package dev.pebble.musicbridge.pixelplay.utils

/**
 * TRIMMED from PixelPlayer's Formats.kt.
 *
 * The other formatters in that file resolve strings through
 * `PixelPlayApplication.instance` — upstream's Application singleton, which this
 * module does not have. FullPlayerContent only calls [formatDuration], which
 * needs no Context and is reproduced verbatim.
 */

fun formatDuration(milliseconds: Long): String {
    if (milliseconds <= 0L) return "00:00"

    val totalSeconds = milliseconds / 1000
    val hours = totalSeconds / 3600
    val minutes = (totalSeconds % 3600) / 60
    val seconds = totalSeconds % 60

    return if (hours > 0) {
        String.format("%02d:%02d:%02d", hours, minutes, seconds)
    } else {
        String.format("%02d:%02d", minutes, seconds)
    }
}

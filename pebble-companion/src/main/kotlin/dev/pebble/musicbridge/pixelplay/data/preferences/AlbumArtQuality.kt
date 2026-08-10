package dev.pebble.musicbridge.pixelplay.data.preferences

/**
 * Lifted verbatim from PixelPlayer's UserPreferencesRepository.kt — the rest of
 * that file is DataStore plumbing for settings this app does not have.
 */
enum class AlbumArtQuality(val maxSize: Int, val label: String) {
    LOW(256, "Low (256px) - Better performance"),
    MEDIUM(512, "Medium (512px) - Balanced"),
    HIGH(800, "High (800px) - Best quality"),
    ORIGINAL(0, "Original - Maximum quality")
}

package dev.pebble.musicbridge

import android.content.Context

/**
 * The active music source and its epoch, shared by both playback backends, the watch
 * bridge and the Compose UI.
 *
 * This is the one genuinely two-way setting in the app. Everything else on the watch's
 * Settings/Advanced menus is watch-owned and arrives via commandSyncSettings, because the
 * watch re-sends that whole blob on *every* state snapshot - a phone-side write to any of
 * those fields is clobbered within seconds, which is why the phone's old settings screen
 * was removed rather than mirrored (see PebblePlaybackService.onUiCommand).
 *
 * The source escapes that fate the same way the audio route does: a monotonic [epoch].
 * Whoever changes the source bumps the epoch first, and a receiver applies an incoming
 * value only when its epoch is newer than the one it already holds. On an equal epoch with
 * a differing value the **watch wins**, matching the route tie-break in the watch app, so
 * there is a single rule for both settings rather than two subtly different ones.
 *
 * Unlike [PlaybackPrefs] the UI may not write here directly - writes go through
 * [PebbleMusicService.setMusicSource], which owns the backend teardown/handover that has
 * to happen alongside the pref change.
 */
object MusicSourcePrefs {
    private const val PREFS_NAME = "dreamwave_source"
    private const val KEY_SOURCE = "music_source"
    private const val KEY_EPOCH = "source_epoch"

    private fun prefs(context: Context) =
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    /** The active source; defaults to the YouTube backend the app shipped with. */
    fun source(context: Context): Int =
        prefs(context).getInt(KEY_SOURCE, Protocol.sourceYouTube)

    fun epoch(context: Context): Int = prefs(context).getInt(KEY_EPOCH, 0)

    fun isSymfonium(context: Context): Boolean =
        source(context) == Protocol.sourceSymfonium

    /** Stores [source] at [epoch] unconditionally. Callers apply the epoch rule first. */
    internal fun store(context: Context, source: Int, epoch: Int) {
        prefs(context).edit()
            .putInt(KEY_SOURCE, source)
            .putInt(KEY_EPOCH, epoch)
            .apply()
    }

    /**
     * Applies a source received from the watch under the epoch rule. Returns true when the
     * value was adopted (and so a backend handover is due).
     *
     * The equal-epoch case is deliberately *not* a no-op: it means both sides changed at
     * once, and the watch wins - so the phone adopts the watch's value even though its own
     * epoch is no older.
     */
    internal fun adoptFromWatch(context: Context, source: Int, epoch: Int): Boolean {
        val currentEpoch = epoch(context)
        val currentSource = source(context)
        if (epoch < currentEpoch) return false
        if (epoch == currentEpoch && source == currentSource) return false
        store(context, source, epoch)
        return source != currentSource
    }

    /**
     * Bumps the epoch for a phone-initiated change and stores [source]. Returns the new
     * epoch so the caller can put it on the wire.
     */
    internal fun setFromPhone(context: Context, source: Int): Int {
        val next = epoch(context) + 1
        store(context, source, next)
        return next
    }
}

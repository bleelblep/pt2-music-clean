package dev.pebble.musicbridge

import android.content.Context
import android.util.Log
import io.rebble.pebblekit2.client.DefaultPebbleSender
import io.rebble.pebblekit2.common.model.PebbleDictionaryItem
import io.rebble.pebblekit2.common.model.TransmissionResult
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

internal class PebbleAudioTransport(
    context: Context,
    private val scope: CoroutineScope,
    private val targetAppUuid: java.util.UUID = Protocol.appUuid,
) {
    private val sender = DefaultPebbleSender(context)
    private val channelLock = Any()
    private var activeChannel: Channel<ShortArray>? = null
    private var sendingJob: Job? = null
    // When set, the audio streaming loop stops sending chunks so a cover-art transfer can
    // use the single-in-flight watch inbox without contention (audio chunks would otherwise
    // starve the cover's sends and its retries would exhaust). The watch's speaker buffer
    // covers the brief pause. See holdAudioForCover()/releaseAudioForCover().
    @Volatile private var coverPriority = false

    /**
     * Invoked from the sender coroutine when the watch stops acking chunks and the
     * stream is abandoned. The service uses it to tell the watch and the phone UI,
     * instead of both sitting on a "playing" state over a dead stream.
     */
    var onStreamDied: (() -> Unit)? = null

    fun holdAudioForCover() { coverPriority = true }
    fun releaseAudioForCover() { coverPriority = false }

    suspend fun start(generation: Int) {
        stop()
        val channel = Channel<ShortArray>(capacity = 32)
        synchronized(channelLock) { activeChannel = channel }
        sendingJob = scope.launch(Dispatchers.IO) {
            sendEvent(Protocol.eventAudioStart, "${Protocol.sampleRate} Hz mono ADPCM", generation)
            val encoder = ImaAdpcmEncoder()
            val aggregate = ShortArray(Protocol.samplesPerChunk)
            var aggregateSize = 0
            var sequence = 0
            var consecutiveFailures = 0
            try {
                for (pcm in channel) {
                    var offset = 0
                    while (offset < pcm.size) {
                        val copied = minOf(Protocol.samplesPerChunk - aggregateSize, pcm.size - offset)
                        pcm.copyInto(aggregate, aggregateSize, offset, offset + copied)
                        aggregateSize += copied
                        offset += copied
                        if (aggregateSize == Protocol.samplesPerChunk) {
                            // Yield the link to an in-progress cover-art transfer.
                            while (coverPriority) delay(20)
                            // The watch enforces strictly consecutive sequence numbers and
                            // kills the stream on any gap, so a chunk's sequence number is
                            // consumed only once the watch has actually acked it. A chunk
                            // that keeps failing is dropped (a ~64 ms gap) rather than
                            // desynchronizing the whole stream.
                            if (sendChunk(encoder.encode(aggregate), sequence, generation)) {
                                sequence++
                                consecutiveFailures = 0
                            } else if (++consecutiveFailures >= MAX_CONSECUTIVE_CHUNK_FAILURES) {
                                Log.w("PT2Music", "Watch unreachable; stopping audio stream")
                                onStreamDied?.invoke()
                                return@launch
                            }
                            aggregateSize = 0
                        }
                    }
                }
            } catch (cancelled: CancellationException) {
                throw cancelled
            }
        }
    }

    // PCM buffers dropped because the 32-chunk channel was full (BLE backpressure).
    // Logged rate-limited: a saturated link otherwise fails invisibly in the logs even
    // though the watch hears the gaps.
    private val droppedPcmChunks = java.util.concurrent.atomic.AtomicInteger(0)

    fun offer(pcm: ShortArray) {
        if (pcm.isEmpty()) return
        val channel = synchronized(channelLock) { activeChannel } ?: return
        if (channel.trySend(pcm).isFailure) {
            val drops = droppedPcmChunks.incrementAndGet()
            if (drops == 1 || drops % 200 == 0) {
                Log.w("PT2Music", "Audio channel backpressured; dropped PCM chunks: $drops")
            }
        }
    }

    suspend fun stop() {
        val channel = synchronized(channelLock) {
            activeChannel.also { activeChannel = null }
        }
        channel?.close()
        sendingJob?.cancelAndJoin()
        sendingJob = null
    }

    /**
     * Tears the transport down. Every step is guarded, because this runs from
     * Service.onDestroy and an exception escaping there kills the whole process.
     *
     * That is not hypothetical: DefaultPebbleSender.close() unbinds its connection to the
     * Pebble app, and when that connection was never registered (or the framework already
     * tore it down as part of the same shutdown) unbindService throws
     * IllegalArgumentException: "Service not registered". Seen in logcat taking the
     * process down from SymfoniumPlaybackService.onDestroy, which killed
     * PebblePlaybackService, its MediaSession and its media notification along with it -
     * the app then restarted with no session at all. Shutdown is best-effort by nature;
     * nothing here is worth a crash.
     */
    fun close() {
        runCatching {
            synchronized(channelLock) {
                activeChannel?.close()
                activeChannel = null
            }
        }.onFailure { Log.w("PT2Music", "Closing audio channel failed", it) }
        runCatching {
            sendingJob?.cancel()
            sendingJob = null
        }.onFailure { Log.w("PT2Music", "Cancelling send job failed", it) }
        runCatching { sender.close() }
            .onFailure { Log.w("PT2Music", "Closing Pebble sender failed", it) }
    }

    suspend fun sendEvent(command: Int, status: String? = null, generation: Int? = null) {
        val message = mutableMapOf<UInt, PebbleDictionaryItem>(
            Protocol.keyCommand to PebbleDictionaryItem.Int32(command),
        )
        status?.let { message[Protocol.keyStatus] = PebbleDictionaryItem.Text(it.toWatchText()) }
        generation?.let { message[Protocol.keyGeneration] = PebbleDictionaryItem.Int32(it) }
        sendReliably(message)
    }

    suspend fun sendSearchResult(
        index: Int,
        videoId: String,
        title: String,
        artist: String,
        requestId: Int,
    ) {
        sendReliably(
            mapOf(
                Protocol.keyCommand to PebbleDictionaryItem.Int32(Protocol.eventSearchResult),
                Protocol.keyResultIndex to PebbleDictionaryItem.Int32(index),
                Protocol.keyVideoId to PebbleDictionaryItem.Text(videoId),
                Protocol.keyTitle to PebbleDictionaryItem.Text(title.toWatchText()),
                Protocol.keyArtist to PebbleDictionaryItem.Text(artist.toWatchText()),
                Protocol.keySearchRequestId to PebbleDictionaryItem.Int32(requestId),
            ),
        )
    }

    /**
     * Closes a search. [total], when known, is how many matches exist behind a paged
     * search - the watch needs it to know where the list ends. Omitted entirely for
     * unpaged searches so the YouTube backend's messages are unchanged on the wire.
     */
    suspend fun sendSearchComplete(requestId: Int, total: Int? = null) {
        val message = mutableMapOf<UInt, PebbleDictionaryItem>(
            Protocol.keyCommand to PebbleDictionaryItem.Int32(Protocol.eventSearchComplete),
            Protocol.keySearchRequestId to PebbleDictionaryItem.Int32(requestId),
        )
        total?.let { message[Protocol.keySearchTotal] = PebbleDictionaryItem.Int32(it) }
        sendReliably(message)
    }

    suspend fun sendSearchError(requestId: Int, status: String) {
        sendReliably(
            mapOf(
                Protocol.keyCommand to PebbleDictionaryItem.Int32(Protocol.eventError),
                Protocol.keyStatus to PebbleDictionaryItem.Text(status.toWatchText()),
                Protocol.keySearchRequestId to PebbleDictionaryItem.Int32(requestId),
            ),
        )
    }

    suspend fun sendLibraryItem(
        libraryType: Int,
        index: Int,
        videoId: String,
        title: String,
        artist: String,
    ) {
        sendReliably(
            mapOf(
                Protocol.keyCommand to PebbleDictionaryItem.Int32(Protocol.eventLibraryItem),
                Protocol.keyLibraryType to PebbleDictionaryItem.Int32(libraryType),
                Protocol.keyResultIndex to PebbleDictionaryItem.Int32(index),
                Protocol.keyVideoId to PebbleDictionaryItem.Text(videoId),
                Protocol.keyTitle to PebbleDictionaryItem.Text(title.toWatchText()),
                Protocol.keyArtist to PebbleDictionaryItem.Text(artist.toWatchText()),
            ),
        )
    }

    /**
     * [total] is the length of the whole library list, sent only for a paged request -
     * it is what tells the watch where the list ends. Omitted for the unpaged types,
     * where the rows it just received *are* the whole list.
     */
    suspend fun sendLibraryComplete(libraryType: Int, total: Int? = null) {
        sendReliably(
            buildMap {
                put(Protocol.keyCommand, PebbleDictionaryItem.Int32(Protocol.eventLibraryComplete))
                put(Protocol.keyLibraryType, PebbleDictionaryItem.Int32(libraryType))
                if (total != null) put(Protocol.keyLibraryTotal, PebbleDictionaryItem.Int32(total))
            },
        )
    }

    suspend fun sendQueueItem(
        index: Int,
        videoId: String,
        title: String,
        artist: String,
    ) {
        sendReliably(
            mapOf(
                Protocol.keyCommand to PebbleDictionaryItem.Int32(Protocol.eventQueueItem),
                Protocol.keyResultIndex to PebbleDictionaryItem.Int32(index),
                Protocol.keyVideoId to PebbleDictionaryItem.Text(videoId),
                Protocol.keyTitle to PebbleDictionaryItem.Text(title.toWatchText()),
                Protocol.keyArtist to PebbleDictionaryItem.Text(artist.toWatchText()),
            ),
        )
    }

    suspend fun sendQueueComplete() {
        sendReliably(
            mapOf(
                Protocol.keyCommand to PebbleDictionaryItem.Int32(Protocol.eventQueueComplete),
            ),
        )
    }

    suspend fun sendPlaybackInfo(durationMs: Long, generation: Int) {
        sendReliably(
            mapOf(
                Protocol.keyCommand to PebbleDictionaryItem.Int32(Protocol.eventPlaybackInfo),
                Protocol.keyDuration to PebbleDictionaryItem.Int32(durationMs.coerceAtMost(Int.MAX_VALUE.toLong()).toInt()),
                Protocol.keyGeneration to PebbleDictionaryItem.Int32(generation),
            ),
        )
    }

    suspend fun sendPlaybackPosition(positionMs: Long, generation: Int) {
        // Position ticks are ephemeral; a lost one is superseded by the next.
        sendMessage(
            mapOf(
                Protocol.keyCommand to PebbleDictionaryItem.Int32(Protocol.eventPlaybackPosition),
                Protocol.keyPosition to PebbleDictionaryItem.Int32(
                    positionMs.coerceIn(0, Int.MAX_VALUE.toLong()).toInt(),
                ),
                Protocol.keyGeneration to PebbleDictionaryItem.Int32(generation),
            ),
        )
    }

    suspend fun sendStateSnapshot(
        playbackState: Int,
        generation: Int,
        videoId: String?,
        title: String?,
        artist: String?,
        durationMs: Long,
        positionMs: Long,
        phoneAudio: Boolean,
        volume: Int,
        loopEnabled: Boolean,
        loopMode: Int,
        shuffleEnabled: Boolean,
        isFavorite: Boolean,
        theme: Int,
        routeEpoch: Int,
    ) {
        val message = mutableMapOf<UInt, PebbleDictionaryItem>(
            Protocol.keyCommand to PebbleDictionaryItem.Int32(Protocol.eventStateSnapshot),
            Protocol.keyProtocolVersion to PebbleDictionaryItem.Int32(Protocol.version),
            Protocol.keyCapabilities to PebbleDictionaryItem.Int32(Protocol.capabilities),
            Protocol.keyPlaybackState to PebbleDictionaryItem.Int32(playbackState),
            Protocol.keyGeneration to PebbleDictionaryItem.Int32(generation),
            Protocol.keyDuration to PebbleDictionaryItem.Int32(durationMs.coerceIn(0, Int.MAX_VALUE.toLong()).toInt()),
            Protocol.keyPosition to PebbleDictionaryItem.Int32(positionMs.coerceIn(0, Int.MAX_VALUE.toLong()).toInt()),
            Protocol.keyAudioRoute to PebbleDictionaryItem.Int32(
                if (phoneAudio) Protocol.audioRoutePhone else Protocol.audioRouteWatch,
            ),
            Protocol.keyRouteEpoch to PebbleDictionaryItem.Int32(routeEpoch),
            Protocol.keyVolume to PebbleDictionaryItem.Int32(volume.coerceIn(0, 100)),
            Protocol.keyLoopEnabled to PebbleDictionaryItem.Int32(if (loopEnabled) 1 else 0),
            Protocol.keyLoopMode to PebbleDictionaryItem.Int32(loopMode),
            Protocol.keyShuffleEnabled to PebbleDictionaryItem.Int32(if (shuffleEnabled) 1 else 0),
            Protocol.keyIsFavorite to PebbleDictionaryItem.Int32(if (isFavorite) 1 else 0),
            Protocol.keyTheme to PebbleDictionaryItem.Int32(theme),
        )
        videoId?.let { message[Protocol.keyVideoId] = PebbleDictionaryItem.Text(it) }
        title?.let { message[Protocol.keyTitle] = PebbleDictionaryItem.Text(it.toWatchText()) }
        artist?.let { message[Protocol.keyArtist] = PebbleDictionaryItem.Text(it.toWatchText()) }
        sendReliably(message)
    }

    /**
     * Announces a phone-initiated music-source change. The watch applies it only if
     * [epoch] is newer than the one it holds (see Protocol.keySourceEpoch), then
     * re-handshakes - which is what repopulates its screen from the incoming backend, so
     * nothing here has to push state as well.
     */
    suspend fun sendSourceChanged(source: Int, epoch: Int) {
        sendReliably(
            mapOf(
                Protocol.keyCommand to PebbleDictionaryItem.Int32(Protocol.eventSourceChanged),
                Protocol.keyConfigMusicSource to PebbleDictionaryItem.Int32(source),
                Protocol.keySourceEpoch to PebbleDictionaryItem.Int32(epoch),
            ),
        )
    }

    suspend fun sendFavoriteState(videoId: String, isFavorite: Boolean, generation: Int) {
        sendReliably(
            mapOf(
                Protocol.keyCommand to PebbleDictionaryItem.Int32(Protocol.eventFavoriteState),
                Protocol.keyVideoId to PebbleDictionaryItem.Text(videoId),
                Protocol.keyIsFavorite to PebbleDictionaryItem.Int32(if (isFavorite) 1 else 0),
                Protocol.keyGeneration to PebbleDictionaryItem.Int32(generation),
            ),
        )
    }

    /**
     * Opens a cover transfer. Carries the videoId so the watch can accept the image on
     * the strength of *which track it is for* rather than on an exact stream-generation
     * match: the watch bumps its generation on next/previous/resume/route-toggle/back,
     * and a cover already in flight when that happens was being discarded silently.
     *
     * Returns whether the watch took it - there is no point streaming 41 chunks at a
     * watch that never saw the header.
     */
    suspend fun sendCoverArtStart(
        width: Int,
        height: Int,
        totalBytes: Int,
        generation: Int,
        videoId: String,
    ): Boolean =
        sendReliably(
            mapOf(
                Protocol.keyCommand to PebbleDictionaryItem.Int32(Protocol.eventCoverArtStart),
                Protocol.keyImageWidth to PebbleDictionaryItem.Int32(width),
                Protocol.keyImageHeight to PebbleDictionaryItem.Int32(height),
                Protocol.keyImageTotalBytes to PebbleDictionaryItem.Int32(totalBytes),
                Protocol.keyGeneration to PebbleDictionaryItem.Int32(generation),
                Protocol.keyVideoId to PebbleDictionaryItem.Text(videoId),
            ),
        )

    suspend fun sendCoverArtChunk(chunk: ByteArray, sequence: Int, generation: Int): Boolean =
        sendReliably(
            mapOf(
                Protocol.keyCommand to PebbleDictionaryItem.Int32(Protocol.eventCoverArtChunk),
                Protocol.keyImageSequence to PebbleDictionaryItem.Int32(sequence),
                Protocol.keyImageData to PebbleDictionaryItem.Bytes(chunk),
                Protocol.keyGeneration to PebbleDictionaryItem.Int32(generation),
            ),
            attempts = COVER_CHUNK_SEND_ATTEMPTS,
        )

    suspend fun sendCoverArtClear(generation: Int) {
        sendReliably(
            mapOf(
                Protocol.keyCommand to PebbleDictionaryItem.Int32(Protocol.eventCoverArtClear),
                Protocol.keyGeneration to PebbleDictionaryItem.Int32(generation),
            ),
        )
    }

    private suspend fun sendChunk(chunk: ByteArray, sequence: Int, generation: Int): Boolean =
        sendReliably(
            mapOf(
                Protocol.keyCommand to PebbleDictionaryItem.Int32(Protocol.eventAudioChunk),
                Protocol.keySequence to PebbleDictionaryItem.Int32(sequence),
                Protocol.keyAudio to PebbleDictionaryItem.Bytes(chunk),
                Protocol.keyGeneration to PebbleDictionaryItem.Int32(generation),
            ),
            attempts = CHUNK_SEND_ATTEMPTS,
        )

    /** Sends a message once; true when at least one watch acked it. */
    private suspend fun sendMessage(message: Map<UInt, PebbleDictionaryItem>): Boolean =
        runCatching { sender.sendDataToPebble(targetAppUuid, message) }
            .onFailure { Log.w("PT2Music", "Send to watch failed", it) }
            .getOrNull()
            ?.values
            ?.any { it is TransmissionResult.Success } == true

    /**
     * Sends a message, retrying briefly on failure. The watch's AppMessage inbox is
     * only large enough for a single in-flight message, so transient nacks are normal
     * (e.g. while the watch is busy rendering); dropping data instead of retrying is
     * what used to leave holes in list screens and kill audio streams.
     */
    private suspend fun sendReliably(
        message: Map<UInt, PebbleDictionaryItem>,
        attempts: Int = DEFAULT_SEND_ATTEMPTS,
    ): Boolean {
        repeat(attempts) { attempt ->
            if (sendMessage(message)) return true
            if (attempt < attempts - 1) delay(RETRY_DELAY_MS)
        }
        return false
    }

    private companion object {
        const val DEFAULT_SEND_ATTEMPTS = 3
        const val CHUNK_SEND_ATTEMPTS = 2
        // Cover chunks are the opposite trade-off to audio chunks. A dropped audio chunk
        // is a ~64 ms gap the listener may not even notice, so it is cheap to give up on;
        // a dropped cover chunk forfeits the whole image and forces a restart from chunk
        // zero, so it is worth pushing harder here.
        const val COVER_CHUNK_SEND_ATTEMPTS = 5
        const val RETRY_DELAY_MS = 100L
        // Consecutive undeliverable audio chunks tolerated before concluding the
        // watch is unreachable and stopping the stream.
        const val MAX_CONSECUTIVE_CHUNK_FAILURES = 8
        // Text tuples must survive the watch's 81-byte buffers even after UTF-8
        // encoding, and must never be cut mid-codepoint (the watch renders invalid
        // UTF-8 as garbage).
        const val MAX_TEXT_BYTES = 80

        fun String.toWatchText(maxBytes: Int = MAX_TEXT_BYTES): String {
            val bytes = toByteArray(Charsets.UTF_8)
            if (bytes.size <= maxBytes) return this
            var end = maxBytes
            while (end > 0 && (bytes[end].toInt() and 0xC0) == 0x80) end--
            return String(bytes, 0, end, Charsets.UTF_8)
        }
    }
}

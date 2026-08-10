package dev.pebble.musicbridge

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.sin

class ImaAdpcmEncoderTest {
    @Test
    fun encodesFixedSizeBlock() {
        val samples = ShortArray(Protocol.samplesPerChunk) { index ->
            (sin(index * 2.0 * PI * 440.0 / Protocol.sampleRate) * 12_000).toInt().toShort()
        }
        val encoded = ImaAdpcmEncoder().encode(samples)

        assertEquals(Protocol.chunkBytes, encoded.size)
        assertEquals(samples[0].toInt(), (encoded[0].toInt() and 0xff) or (encoded[1].toInt() shl 8))
        assertTrue(encoded.drop(4).any { it.toInt() != 0 })
    }
}

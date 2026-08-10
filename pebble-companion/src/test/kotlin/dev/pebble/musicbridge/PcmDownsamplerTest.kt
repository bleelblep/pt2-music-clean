package dev.pebble.musicbridge

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder

class PcmDownsamplerTest {
    @Test
    fun converts16kStereoTo16kMono() {
        val input = ByteBuffer.allocate(8 * Short.SIZE_BYTES).order(ByteOrder.nativeOrder())
        repeat(4) { frame ->
            val sample = (frame * 256).toShort()
            input.putShort(sample)
            input.putShort(sample)
        }
        input.flip()

        val output = PcmDownsampler().convert(input, sampleRate = 16_000, channelCount = 2)

        assertArrayEquals(shortArrayOf(0, 256, 512, 768), output)
    }

    @Test
    fun preservesRateAcrossBuffers() {
        val downsampler = PcmDownsampler()
        var samples = 0
        repeat(10) {
            val input = ByteBuffer.allocate(441 * Short.SIZE_BYTES).order(ByteOrder.nativeOrder())
            repeat(441) { input.putShort(0) }
            input.flip()
            samples += downsampler.convert(input, sampleRate = 44_100, channelCount = 1).size
        }

        assertEquals(1600, samples)
    }
}

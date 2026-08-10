package dev.pebble.musicbridge

import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.roundToInt

internal class PcmDownsampler {
    private var sourceFrame = 0L
    private var nextOutputFrame = 0.0

    fun reset() {
        sourceFrame = 0L
        nextOutputFrame = 0.0
    }

    fun convert(input: ByteBuffer, sampleRate: Int, channelCount: Int): ShortArray {
        require(sampleRate > 0 && channelCount > 0)
        val pcm = input.duplicate().order(ByteOrder.nativeOrder())
        val frameCount = pcm.remaining() / (Short.SIZE_BYTES * channelCount)
        if (frameCount == 0) return shortArrayOf()

        val output = ShortArray(((frameCount * Protocol.sampleRate.toDouble() / sampleRate) + 2).roundToInt())
        var outputSize = 0
        val outputStep = sampleRate.toDouble() / Protocol.sampleRate

        repeat(frameCount) { localFrame ->
            var sum = 0
            repeat(channelCount) {
                sum += pcm.short.toInt()
            }
            val absoluteFrame = sourceFrame + localFrame
            if (absoluteFrame >= nextOutputFrame) {
                val mono = sum / channelCount
                output[outputSize++] = mono.coerceIn(Short.MIN_VALUE.toInt(), Short.MAX_VALUE.toInt()).toShort()
                nextOutputFrame += outputStep
            }
        }
        sourceFrame += frameCount
        return output.copyOf(outputSize)
    }
}

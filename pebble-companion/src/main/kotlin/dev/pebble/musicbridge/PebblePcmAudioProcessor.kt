package dev.pebble.musicbridge

import androidx.media3.common.C
import androidx.media3.common.audio.AudioProcessor
import androidx.media3.common.audio.BaseAudioProcessor
import java.nio.ByteBuffer

internal class PebblePcmAudioProcessor(
    private val onPcm: (ShortArray) -> Unit,
) : BaseAudioProcessor() {
    private val downsampler = PcmDownsampler()

    override fun onConfigure(inputAudioFormat: AudioProcessor.AudioFormat): AudioProcessor.AudioFormat {
        check(inputAudioFormat.encoding == C.ENCODING_PCM_16BIT) {
            "Expected PCM16 from Media3, got encoding ${inputAudioFormat.encoding}"
        }
        return inputAudioFormat
    }

    override fun queueInput(inputBuffer: ByteBuffer) {
        if (!inputBuffer.hasRemaining()) return
        val mono = downsampler.convert(inputBuffer, inputAudioFormat.sampleRate, inputAudioFormat.channelCount)
        onPcm(mono)

        val output = replaceOutputBuffer(inputBuffer.remaining())
        output.put(inputBuffer)
        output.flip()
    }

    override fun onFlush() {
        downsampler.reset()
    }

    override fun onReset() {
        downsampler.reset()
    }
}

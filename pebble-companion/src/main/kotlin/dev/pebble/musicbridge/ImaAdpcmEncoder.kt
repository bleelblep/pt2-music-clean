package dev.pebble.musicbridge

internal class ImaAdpcmEncoder {
    private var stepIndex = 0

    fun reset() {
        stepIndex = 0
    }

    fun encode(samples: ShortArray): ByteArray {
        require(samples.size == Protocol.samplesPerChunk)
        val output = ByteArray(Protocol.chunkBytes)
        var predictor = samples[0].toInt()
        output[0] = predictor.toByte()
        output[1] = (predictor shr 8).toByte()
        output[2] = stepIndex.toByte()
        output[3] = 0

        var outputIndex = 4
        var lowNibble = true
        for (i in 1 until samples.size) {
            val nibble = encodeSample(samples[i].toInt(), predictor)
            predictor = nibble.predictor
            if (lowNibble) {
                output[outputIndex] = nibble.code.toByte()
            } else {
                output[outputIndex] = (output[outputIndex].toInt() or (nibble.code shl 4)).toByte()
                outputIndex++
            }
            lowNibble = !lowNibble
        }
        return output
    }

    private fun encodeSample(sample: Int, currentPredictor: Int): EncodedNibble {
        val step = STEP_TABLE[stepIndex]
        var difference = sample - currentPredictor
        var code = 0
        if (difference < 0) {
            code = 8
            difference = -difference
        }

        var delta = step shr 3
        if (difference >= step) {
            code = code or 4
            difference -= step
            delta += step
        }
        if (difference >= step shr 1) {
            code = code or 2
            difference -= step shr 1
            delta += step shr 1
        }
        if (difference >= step shr 2) {
            code = code or 1
            delta += step shr 2
        }

        val predictor = if (code and 8 != 0) currentPredictor - delta else currentPredictor + delta
        stepIndex = (stepIndex + INDEX_TABLE[code and 7]).coerceIn(0, STEP_TABLE.lastIndex)
        return EncodedNibble(code, predictor.coerceIn(Short.MIN_VALUE.toInt(), Short.MAX_VALUE.toInt()))
    }

    private data class EncodedNibble(val code: Int, val predictor: Int)

    companion object {
        val INDEX_TABLE = intArrayOf(-1, -1, -1, -1, 2, 4, 6, 8)
        val STEP_TABLE = intArrayOf(
            7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
            34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
            157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
            598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878,
            2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
            6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
            18500, 20350, 22385, 24623, 27086, 29794, 32767,
        )
    }
}

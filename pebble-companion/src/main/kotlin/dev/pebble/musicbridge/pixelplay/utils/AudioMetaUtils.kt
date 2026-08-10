package dev.pebble.musicbridge.pixelplay.utils

import java.util.Locale

/**
 * TRIMMED from PixelPlayer's AudioMetaUtils.kt.
 *
 * Upstream also reads bitrate/sample-rate off local files via MediaMetadataRetriever
 * and caches them through a Room `MusicDao`. This app streams from the watch and has
 * no local files or database, and FullPlayerContent only ever calls
 * [mimeTypeToFormat] — which is reproduced below verbatim.
 */
object AudioMetaUtils {


    fun mimeTypeToFormat(mimeType: String?): String {
        val normalized = mimeType
            ?.trim()
            ?.lowercase(Locale.ROOT)
            ?.substringBefore(';')
            ?: return "-"

        if (normalized.isBlank()) return "-"

        return when {
            normalized == "audio/mpeg" ||
                normalized == "audio/mp3" ||
                normalized == "audio/x-mp3" ||
                normalized == "audio/mpeg3" -> "mp3"

            normalized == "audio/flac" ||
                normalized == "audio/x-flac" -> "flac"

            normalized == "audio/wav" ||
                normalized == "audio/x-wav" ||
                normalized == "audio/wave" ||
                normalized == "audio/vnd.wave" -> "wav"

            normalized == "audio/ogg" ||
                normalized == "application/ogg" ||
                normalized == "audio/vorbis" ||
                normalized == "audio/x-vorbis" -> "ogg"

            normalized == "audio/opus" ||
                normalized == "audio/x-opus" -> "opus"

            normalized == "audio/mp4" ||
                normalized == "audio/m4a" ||
                normalized == "audio/x-m4a" ||
                normalized == "audio/mp4a-latm" -> "m4a"

            normalized == "audio/aac" ||
                normalized == "audio/aacp" -> "aac"

            normalized == "audio/amr" ||
                normalized == "audio/amr-wb" ||
                normalized == "audio/3gpp" -> "amr"

            normalized == "audio/evrc" ||
                normalized == "audio/x-evrc" -> "evrc"

            normalized == "audio/qcelp" ||
                normalized == "audio/x-qcelp" -> "qcelp"

            normalized == "audio/x-ima-adpcm" ||
                normalized == "audio/ima-adpcm" -> "ima"

            normalized == "audio/alac" ||
                normalized == "audio/x-alac" -> "alac"

            normalized == "audio/aiff" ||
                normalized == "audio/x-aiff" ||
                normalized == "audio/aif" ||
                normalized == "audio/x-aifc" -> "aiff"

            normalized == "audio/x-ms-wma" ||
                normalized == "audio/wma" -> "wma"

            normalized == "audio/ac3" ||
                normalized == "audio/eac3" ||
                normalized == "audio/eac3-joc" -> "ac3"

            normalized == "audio/vnd.dts" ||
                normalized == "audio/vnd.dts.hd" -> "dts"

            normalized == "audio/midi" ||
                normalized == "audio/x-midi" ||
                normalized == "audio/sp-midi" ||
                normalized == "audio/x-mid" -> "midi"

            normalized.contains("mp4a") -> "m4a"
            normalized.contains("flac") -> "flac"
            normalized.contains("opus") -> "opus"
            normalized.contains("vorbis") || normalized.contains("ogg") -> "ogg"
            normalized.contains("wav") || normalized.contains("wave") -> "wav"
            normalized.contains("aac") -> "aac"
            normalized.contains("mpeg") || normalized.contains("mp3") -> "mp3"
            normalized.contains("amr") -> "amr"
            normalized.contains("alac") -> "alac"
            normalized.contains("aiff") || normalized.contains("aif") -> "aiff"
            normalized.contains("wma") -> "wma"
            normalized.contains("dts") -> "dts"
            normalized.contains("eac3") || normalized.contains("ac3") -> "ac3"
            normalized.contains("midi") || normalized.contains("x-mid") -> "midi"
            normalized.startsWith("audio/") -> normalized.substringAfter("audio/").ifBlank { "-" }
            else -> "-"
        }
    }
}

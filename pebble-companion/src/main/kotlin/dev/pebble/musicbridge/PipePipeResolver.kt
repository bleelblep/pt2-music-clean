package dev.pebble.musicbridge

import org.schabi.newpipe.extractor.NewPipe
import org.schabi.newpipe.extractor.ServiceList
import org.schabi.newpipe.extractor.downloader.CancellableCall
import org.schabi.newpipe.extractor.downloader.Downloader
import org.schabi.newpipe.extractor.downloader.Request
import org.schabi.newpipe.extractor.downloader.Response
import org.schabi.newpipe.extractor.exceptions.ReCaptchaException
import org.schabi.newpipe.extractor.search.SearchInfo
import org.schabi.newpipe.extractor.services.youtube.search.filter.YoutubeFilters
import org.schabi.newpipe.extractor.stream.StreamInfo
import org.schabi.newpipe.extractor.stream.StreamInfoItem
import okhttp3.OkHttpClient
import okhttp3.RequestBody.Companion.toRequestBody
import java.io.IOException

internal object PipePipeResolver {
    data class SearchResult(
        val videoId: String,
        val title: String,
        val artist: String,
    )

    private val downloader = PipePipeDownloader()

    init {
        NewPipe.init(downloader)
    }

    fun resolve(videoId: String, maxBitrateKbps: Int? = null): String? {
        val streamInfo = StreamInfo.getInfo(
            ServiceList.YouTube,
            "https://music.youtube.com/watch?v=$videoId",
        )
        val candidates = streamInfo.audioStreams.asSequence().filter { it.isUrl }
        // Prefer the best stream at or under the ceiling; if nothing qualifies (every
        // available format exceeds it), fall back to the highest bitrate available
        // rather than failing playback just to respect a quality cap.
        val capped = maxBitrateKbps?.let { cap ->
            candidates.filter { it.averageBitrate in 1..(cap * 1000) }.maxByOrNull { it.averageBitrate }
        }
        return (capped ?: candidates.maxByOrNull { it.averageBitrate })?.content
    }

    fun search(query: String, limit: Int = 5): List<SearchResult> {
        val youtube = ServiceList.YouTube
        val factory = youtube.searchQHFactory
        val videoFilter = factory.availableContentFilter.filterGroups
            .asSequence()
            .flatMap { it.filterItems.asSequence() }
            .first { it.name == YoutubeFilters.VIDEOS }
        val handler = factory.fromQuery(query, listOf(videoFilter), null)
        return SearchInfo.getInfo(youtube, handler).relatedItems
            .filterIsInstance<StreamInfoItem>()
            .take(limit)
            .map {
                SearchResult(
                    videoId = youtube.streamLHFactory.getId(it.url),
                    title = it.name,
                    artist = it.uploaderName.orEmpty(),
                )
            }
    }
}

private class PipePipeDownloader : Downloader() {
    private val client = OkHttpClient()

    override fun execute(request: Request): Response {
        val response = client.newCall(request.toOkHttp()).execute()
        if (response.code == 429) {
            response.close()
            throw ReCaptchaException("YouTube returned HTTP 429", request.url())
        }
        return response.toPipePipe()
    }

    override fun executeAsync(request: Request, callback: AsyncCallback?): CancellableCall {
        val call = client.newCall(request.toOkHttp())
        val cancellable = CancellableCall(call)
        call.enqueue(object : okhttp3.Callback {
            override fun onFailure(call: okhttp3.Call, exception: IOException) {
                cancellable.setFinished()
                callback?.onError(exception)
            }

            override fun onResponse(call: okhttp3.Call, response: okhttp3.Response) {
                try {
                    if (response.code == 429) {
                        response.close()
                        callback?.onError(ReCaptchaException("YouTube returned HTTP 429", request.url()))
                    } else {
                        callback?.onSuccess(response.toPipePipe())
                    }
                } catch (exception: Exception) {
                    callback?.onError(exception)
                } finally {
                    cancellable.setFinished()
                }
            }
        })
        return cancellable
    }

    private fun Request.toOkHttp(): okhttp3.Request {
        val builder = okhttp3.Request.Builder()
            .url(url())
            .method(httpMethod(), dataToSend()?.toRequestBody())
            .header("User-Agent", "Mozilla/5.0 (Linux; Android 14) AppleWebKit/537.36 Chrome/131 Mobile Safari/537.36")
        headers().forEach { (name, values) ->
            builder.removeHeader(name)
            values.forEach { builder.addHeader(name, it) }
        }
        return builder.build()
    }

    private fun okhttp3.Response.toPipePipe(): Response {
        val bytes = body?.bytes() ?: byteArrayOf()
        return Response(
            code,
            message,
            headers.toMultimap(),
            bytes.toString(Charsets.UTF_8),
            bytes,
            request.url.toString(),
        )
    }
}

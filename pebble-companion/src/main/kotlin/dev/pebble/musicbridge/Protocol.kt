package dev.pebble.musicbridge

import java.util.UUID

internal object Protocol {
    // The one watch app. Both backends (YouTube and Symfonium) talk to it; which one
    // answers is a runtime setting, not a separate app identity - see keyConfigMusicSource.
    // There used to be a second "music-src" watchapp (uuid cb83734d-...) forked purely so
    // PebbleMusicService could route to the Symfonium backend by UUID; keeping a diverged
    // copy of a 6000-line main.c in step was not worth it, so the fork was dropped and the
    // choice moved onto the wire.
    val appUuid: UUID = UUID.fromString("38d45a52-e0f8-4db0-92ad-1fc852703e69")

    const val keyCommand = 0u
    const val keyQuery = 1u
    const val keyVideoId = 2u
    const val keyAudio = 3u
    const val keySequence = 4u
    const val keyStatus = 5u
    const val keyTitle = 6u
    const val keyArtist = 7u
    const val keyResultIndex = 8u
    const val keyDuration = 9u
    const val keyGeneration = 10u
    const val keyAudioRoute = 11u
    const val keyVolume = 12u
    const val keyPosition = 13u
    const val keyProtocolVersion = 14u
    const val keyCapabilities = 15u
    const val keyPlaybackState = 16u
    const val keyLoopEnabled = 17u
    const val keySearchRequestId = 18u
    const val keyConfigAudioRoute = 19u
    const val keyConfigWatchVolume = 20u
    const val keyConfigPhoneVolume = 21u
    const val keyConfigInputMode = 22u
    const val keyLibraryType = 23u
    const val keyLibraryLimit = 24u
    const val keyIsFavorite = 25u
    const val keySearchLimit = 26u
    const val keyConfigShowProgress = 27u
    const val keyCacheEnabled = 28u
    const val keyCacheSizeMb = 29u
    const val keyConfigCacheEnabled = 30u
    const val keyConfigCacheSizeMb = 31u
    const val keyShuffleEnabled = 32u
    const val keyLoopMode = 33u
    const val keyConfigCoverArtBackground = 34u
    const val keyImageData = 35u
    const val keyImageWidth = 36u
    const val keyImageHeight = 37u
    const val keyImageSequence = 38u
    const val keyImageTotalBytes = 39u
    const val keyTheme = 40u
    const val keyConfigWatchAudioQuality = 41u
    const val keyConfigPhoneAudioQuality = 42u
    const val keySearchMode = 43u
    const val keyConfigCacheRadio = 44u
    // Monotonic audio-route epoch: whoever changes the route bumps it, and a route
    // carried by any message is applied only when its epoch is newer than the last one
    // applied locally. Absent on old firmware — treated as "always apply" (pre-epoch
    // behavior).
    const val keyRouteEpoch = 45u

    // Which backend serves this watch app: sourceYouTube or sourceSymfonium. Carried in
    // commandSyncSettings (watch -> phone) and eventSourceChanged (phone -> watch).
    const val keyConfigMusicSource = 46u

    // Monotonic epoch for keyConfigMusicSource, working exactly like keyRouteEpoch: the
    // side that changes the source bumps it first, and a source is applied only when its
    // epoch is newer than the last one applied locally. Without this the watch's
    // commandSyncSettings - which it re-sends on *every* state snapshot - would clobber a
    // phone-side change within seconds, which is precisely why the phone's old settings
    // screen was deleted rather than mirrored (see PebblePlaybackService.onUiCommand).
    const val keySourceEpoch = 47u

    // Paged search (Symfonium only, "Results: Unlimited"). The watch holds a sliding
    // window of results rather than the whole list, so a search request names the first
    // global index it wants and keySearchLimit becomes the page size. keyResultIndex on
    // each eventSearchResult is the *global* index, not a position within the page.
    const val keySearchOffset = 48u

    // Total matches behind a paged search, sent with eventSearchComplete so the watch
    // knows where the list ends without walking off it. Absent means "unknown", which the
    // watch treats as "there may be more" until a page comes back short.
    const val keySearchTotal = 49u

    // Paged library, the same shape as the two above and for the same reason: Favorites
    // has no natural ceiling, and the watch's 60-row array used to be the whole list it
    // could ever see. The request names the first global index it wants and
    // keyLibraryLimit becomes the page size; keyResultIndex on each eventLibraryItem is
    // the global index. Sent by the watch only for library types it pages (currently
    // Favorites, and only against the Symfonium backend).
    const val keyLibraryOffset = 50u

    // Rows the library list actually has, sent with eventLibraryComplete. Absent means
    // "unknown", which is what an older companion answering an unpaged request sends.
    const val keyLibraryTotal = 51u

    // Start playlists shuffled (Symfonium only). Carried in commandSyncSettings, and the
    // watch owns it outright - it is the only side with UI for it, so unlike the route
    // and the source it needs no epoch to arbitrate between two writers.
    const val keyConfigSymfoniumAutoShuffle = 52u

    const val sourceYouTube = 0
    const val sourceSymfonium = 1

    const val version = 4
    const val capabilityStateSnapshot = 1
    const val capabilitySearchRequestId = 1 shl 1
    const val capabilities = capabilityStateSnapshot or capabilitySearchRequestId

    const val commandHello = 1
    const val commandSearch = 2
    const val commandPlay = 3
    const val commandStop = 4
    const val commandPause = 5
    const val commandResume = 6
    const val commandToggleLoop = 7
    const val commandSetAudioRoute = 8
    const val commandSetVolume = 9
    const val commandRequestState = 10
    const val commandRequestLibrary = 11
    const val commandToggleFavorite = 12
    const val commandSeek = 13
    const val commandSyncSettings = 14
    const val commandToggleShuffle = 15
    const val commandPrevious = 16
    const val commandNext = 17
    const val commandDeleteCached = 18
    const val commandRequestQueue = 19
    const val commandQueueJump = 20

    const val audioRouteWatch = 0
    const val audioRoutePhone = 1

    const val eventReady = 100
    const val eventSearchResult = 101
    const val eventAudioStart = 102
    const val eventAudioChunk = 103
    const val eventAudioEnd = 104
    const val eventError = 105
    const val eventSearchComplete = 106
    const val eventPaused = 107
    const val eventPlaybackInfo = 108
    const val eventPlaybackPosition = 109
    const val eventStateSnapshot = 110
    const val eventLibraryItem = 111
    const val eventLibraryComplete = 112
    const val eventCoverArtStart = 113
    const val eventCoverArtChunk = 114
    const val eventCoverArtClear = 115

    const val libraryRecent = 0
    const val libraryCached = 1
    const val libraryFavorites = 2
    const val libraryContinue = 3
    const val libraryRecentSearches = 4
    // Phone-owned playlists; the watch lists and plays them but never edits them.
    const val libraryPlaylists = 5
    // Symfonium only: backed by the user's "most played" smart playlist. Appended -
    // never renumber existing values; the watch shares these numbers.
    const val libraryMostPlayed = 6

    // Values carried by keySearchMode. Song Radio returns real tracks, each marked so
    // play starts a radio seeded from it; Artist Radio returns synthetic rows whose
    // videoId carries the artist browse id.
    const val searchModeSong = 0
    const val searchModeArtist = 1
    const val searchModeSongRadio = 2
    // Plain album search (used by the Symfonium source). Appended - never renumber
    // existing values; the watch shares these numbers.
    const val searchModeAlbum = 3

    // Broadcast alongside state snapshots / a dedicated favorite event so the watch
    // can render the heart indicator for the current track.
    const val eventFavoriteState = 116
    const val eventQueueItem = 117
    const val eventQueueComplete = 118

    // Phone-initiated music-source change, carrying keyConfigMusicSource + keySourceEpoch.
    const val eventSourceChanged = 119

    const val playbackIdle = 0
    const val playbackBuffering = 1
    const val playbackPlaying = 2
    const val playbackPaused = 3

    const val sampleRate = 16_000
    const val chunkBytes = 512
    const val samplesPerChunk = 1_017
}

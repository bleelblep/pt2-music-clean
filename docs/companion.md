# Companion app

`pebble-companion/` is a standalone Android app (`dev.pebble.musicbridge`, minSdk 26) that does the heavy lifting the watch can't: resolving and streaming YouTube Music audio, encoding cover art, and bridging it all to the watch over PebbleKit. It also has a full on-phone player UI, so it works as a music player in its own right.

## Architecture

| Piece | Role |
|---|---|
| **`PebbleMusicService`** | PebbleKit receiver service (`io.rebble.pebblekit2.RECEIVE_DATA_FROM_WATCH`) — the watch's front door. Routes incoming watch messages to the playback backend and answers with state. |
| **`PebblePlaybackService`** | A Media3 `MediaSessionService` and the heart of the app: stream resolution, playback, queueing, caching, library/favorites/playlists, state publishing, and the watch audio transport. Runs as a foreground media-playback service with notification controls. |
| **`PipePipeResolver`** | Resolves tracks to audio stream URLs (OkHttp + PipePipe Extractor). |
| **`innertube/`** (module) | YouTube Music API client — search, suggestions, home/library pages, artist & song radio seeds. Taken from Metrolist (GPL-3.0). |
| **`PebbleAudioTransport`** | The BLE channel to the watch for audio streaming. |
| **`PcmDownsampler` / `ImaAdpcmEncoder` / `PebblePcmAudioProcessor`** | The watch-audio pipeline: PCM → downsampled → IMA-ADPCM, framed for the Time 2's speaker. |
| **`watchimagebridge`** | Go library (source in `watchimage-bridge/`, bound with gomobile, checked in as `pebble-companion/libs/watchimagebridge.aar`) that encodes cover art into the watch's image format. |
| **`Protocol`** | The shared wire protocol (version 4): message keys, commands, capabilities and the route-epoch rule, mirrored by the watchapp. |

### Wire protocol highlights

- **Versioned** (`version = 4`) with a **capabilities** bitmap (state snapshots, search request IDs) so old watchapps and new companions interoperate.
- Commands cover search, play/pause/resume/stop, loop, audio route, volume, state/library requests, favorites, playlists and config sync.
- **Route epoch**: every audio-route change bumps a monotonic counter; a route carried by any message is applied only if its epoch is newer — so the watch and phone can never fight over the route.
- Cover art travels as sequenced chunks (sequence / total-bytes / width / height keys) and is validated and cached on both sides.

## Audio routes

| Route | What the companion does |
|---|---|
| **Phone** | Plays locally through the Media3 session (speaker/headphones/BT as Android routes it). |
| **Watch** | Feeds the player's PCM through the downsampler → ADPCM encoder → BLE transport to the watch speaker. |

Per-route quality settings (watch: Efficient/Balanced, phone: Data Saver/High) pick the stream bitrate cap.

## Library & data

- **Recently played** — recorded on every playback.
- **Favorites** — toggled from the watch or the phone.
- **Playlists** — created and edited on the phone (playlist dialogs), browsable from the watch.
- **Continue / resume positions** — playback positions are recorded and cleared on completion, so long tracks resume where you left off.
- **Recent searches** — synced to the watch.
- **Stream cache** — a Media3 `SimpleCache` stores played streams; radio caching is optional (cache radio). The **Cache screen** shows usage, and per-track or full-cache deletion.

## Cover art pipeline

1. The track's thumbnail URL is normalized to a square crop.
2. Downloaded bytes are validated and re-encoded by `watchimagebridge` (Go) into the watch's format, sized to the watch screen (with a mono variant for non-color targets).
3. Results are cached on disk (with pruning) so repeated tracks don't re-download or re-encode.
4. The encoded image is chunked over PebbleKit to the watch.

## On-phone UI

A full player interface, vendored from **PixelPlayer** (MIT, pinned at the last MIT-licensed commit — see `pixelplay/VENDORING.md`) and extended with dreamwave's own screens:

- **Home** — recent/favorites entry points and playback access.
- **Library** — the same sections the watch shows.
- **Search** — YouTube Music search with the three modes (song / artist radio / song radio).
- **Now Playing** — the PixelPlayer expressive player sheet (squircle artwork, palette-driven colors, animated controls).
- **Cache** — cache usage, per-track delete, clear all.
- **Info** — about/links screen.

Theming follows the watch's accent (`WatchAccent`: Teal, Purple, Sunset, Default, plus mono) with a light/dark **theme mode** stored in a dedicated UI-prefs store, kept deliberately separate from the playback prefs the watch syncs.

## Permissions

`INTERNET`, `WAKE_LOCK`, `POST_NOTIFICATIONS`, `FOREGROUND_SERVICE` + `FOREGROUND_SERVICE_MEDIA_PLAYBACK`, `REQUEST_IGNORE_BATTERY_OPTIMIZATIONS` (prompted so streaming isn't killed in the background).

## Build

```bash
./gradlew :pebble-companion:assembleDebug   # debug
./gradlew :pebble-companion:assembleRelease # release (signed per signingConfigs)
```

Output: `pebble-companion/build/outputs/apk/{debug,release}/`.

`pebble-companion/libs/watchimagebridge.aar` is checked in as a build input. To rebuild it from source you need Go and `gomobile`:

```bash
cd watchimage-bridge
gomobile bind -target=android/arm64 -androidapi 26 \
  -javapkg dev.pebble.watchimagebridge \
  -o ../pebble-companion/libs/watchimagebridge.aar watchimagebridge
```

Unit tests (encoder/downsampler) live in `pebble-companion/src/test/`:

```bash
./gradlew :pebble-companion:testDebugUnitTest
```

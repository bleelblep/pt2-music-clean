# Companion app

`pebble-companion/` is **music-src**, a standalone Android app (`dev.pebble.musicbridge`, minSdk 26) that does the heavy lifting the watch can't: resolving and streaming YouTube Music audio, encoding cover art, and bridging it all to the watch over PebbleKit. It also has a full on-phone player UI, so it works as a music player in its own right.

## Architecture

| Piece | Role |
|---|---|
| **`PebbleMusicService`** | PebbleKit receiver service (`io.rebble.pebblekit2.RECEIVE_DATA_FROM_WATCH`) — the watch's front door, and the owner of the music-source setting. Routes each watch message to whichever backend is currently selected. |
| **`PebblePlaybackService`** | A Media3 `MediaSessionService` and the heart of the YouTube source: stream resolution, playback, queueing, caching, library/favorites/playlists, state publishing, and the watch audio transport. Runs as a foreground media-playback service with notification controls. |
| **`SymfoniumPlaybackService`** | The Symfonium source: binds Symfonium's exported `MediaBrowserService` and mirrors *its* playback to the watch, translating watch buttons into standard `Player` calls. While that source is active it also backs the phone's Search and Library screens (search plays the best match and reports the queue it built; library reads come from its browse tree). See [Music sources](#music-sources). |
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
- **Paged search** (Symfonium only): a search request may carry an offset, each result's index is then its position in the whole result list rather than in the page, and the completion carries the total. The watch keeps one search request id across the pages of a single search, which is also the key the backend holds the ranked list under — so every page is a slice of one ranking rather than of a fresh search.

## Music sources

One watch app, two backends. Which one serves it is a runtime setting, changeable from
**either** end — Settings → Music source on the watch, or the source pill on the phone's
Home screen.

| Source | Backend | Audio | Library | Search |
|---|---|---|---|---|
| **YouTube** | `PebblePlaybackService` | Phone **or** watch speaker | Cached / favorites / playlists / history | Songs, Artist Radio, Song Radio |
| **Symfonium** | `SymfoniumPlaybackService` | Phone only | Recently played / favourites / playlists, read from Symfonium | Songs / albums / artists |

Symfonium has **no watch-speaker route**: nothing on Android lets one app pull another
app's decoded audio, so that source always plays through Symfonium's own player. The watch
hides Output, watch volume, the audio-quality rows, Cached Music and the two radio search
modes while it is active, rather than offering switches that would do nothing.

**Source epoch.** The source is the one genuinely two-way setting in the app. Everything
else on the watch's Settings/Advanced menus is watch-owned and arrives via
`commandSyncSettings`; a phone-side write to any of those is overwritten by the watch's
next sync, which is why the phone's old settings screen was removed rather than mirrored.
The source escapes that the same way the audio route does — a monotonic epoch. Whoever
changes it bumps the epoch first, and a receiver applies an incoming value only if its
epoch is newer. On an equal epoch the **watch wins**, matching the route tie-break.

Known limitations of the Symfonium source: the favourite **heart never lights** (Symfonium
exposes a toggle action but not per-track favourite state — the toggle itself works), and
**album name** never reaches the watch because the protocol has no album key for any
source. Media ids longer than the watch's 81-byte text buffer travel as short synthetic
tokens (`s:<n>`) so they survive the round trip.

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

Under the Symfonium source the source of the image differs — artwork comes from the session's metadata (usually embedded `artworkData`, sometimes a `content://` URI on Symfonium's Auto provider) and there is no disk cache — but the rest is shared. Two rules matter there:

- **The watch's Hello forces a re-send.** A relaunched watchapp holds no art while the track is unchanged, so the "already sent this one" guard has to be reset by the handshake, not by a track change.
- **Art is chased, not awaited.** Symfonium publishes a small stand-in cover before the real one and does not reliably republish metadata once it lands, so an attempt that finds a placeholder (or fails to fetch, encode or transfer) is retried a few times under an overall deadline. Only a track that genuinely carries no artwork is recorded as settled.

## On-phone UI

A full player interface, vendored from **PixelPlayer** (MIT, pinned at the last MIT-licensed commit — see `pixelplay/VENDORING.md`) and extended with music-src's own screens:

- **Home** — recent/favorites entry points and playback access.
- **Library** — the same sections the watch shows. While Symfonium is the active source the sections are its own (recently played / favorites / playlists, read from its browse tree); Recently Played prefers a "recently played" smart playlist when one exists (see `SymfoniumPlaybackService.recentSongs`), falling back to the browse-tree album walk otherwise, and the watch additionally offers a "most played" smart-playlist row. The on-disk Songs tab, sorting and playlist editing are YouTube-backend features and hide.
- **Search** — searches the active source: YouTube Music with the three modes (song / artist radio / song radio), or Symfonium's library (song / album / artist). Symfonium search is real server-side-ranked search over its offline copy of the library, via `MediaBrowserCompat.search()` — Media3's own `search()` is never called, because its legacy bridge parcels a support-library `ResultReceiver` Symfonium cannot unmarshal (it crashes Symfonium's main thread; Media3's `getItem()` is avoided for the same reason). Playback is never touched until a result is picked, and picks play through `playFromMediaId`, exactly like an Android Auto row tap.
- **Now Playing** — the PixelPlayer expressive player sheet (squircle artwork, palette-driven colors, animated controls).
- **Cache** — cache usage, per-track delete, clear all.
- **Info** — about/links screen.
- **Appearance** — the phone's theme and its light/dark preference.

## Appearance

The phone and the watch pick themes independently. Both settings live in a dedicated
UI-prefs store, deliberately separate from the playback prefs the watch syncs: neither
is ever sent to the watch, so changing the phone's look cannot disturb the watch's.

The choices are the *watch's* themes. `WatchAccent.PALETTES` mirrors the watchapp's
`bespoke_colors()` table — ground, ink, accent and on-accent — so each row on the
Appearance screen shows the colors that theme actually paints on the watch, and the app
wears them literally. Picking "Arcade" on the phone means wearing the watch's Arcade, not a
separate phone palette of the same name.

Seeding Material from the watch accent is not enough on its own, and this is worth
knowing before touching `DreamwaveAccent.kt`. The generator runs `SchemeTonalSpot`, where
every `on*` role comes from Material's **neutral** palette at chroma 6: the seed's hue
survives only as an invisible tint and tone decides the rest, so the ink lands on white in
dark mode whatever color you seed with. That is Material working as intended — the seed
moves `primary` and the containers, the text roles stay neutral — and it is why the phone
read so much greyer than the watch. So the generated scheme is kept for its coherence
(every exotic role the vendored player reaches for stays populated and consistent) and
`ColorScheme.wearing()` overwrites the roles that decide what you see: ground, ink, the
surface container ramp, the accent pairs and the outlines.

The watch has one appearance; the phone has two. Dark wears the palette as-is; light
inverts it into a pale wash of the same hue with deep saturated ink, so light mode is
still recognisably that theme rather than the same grey as every other one.

**Match the watch** is the default and keeps the old behavior — the phone follows
whatever theme the watch reports.

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

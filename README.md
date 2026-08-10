# dreamwave

A music player for the **Pebble Time 2**, in two halves:

- **`watchapp/`** — a Pebble watchapp (C) that browses, searches and controls playback on the watch, and can play audio through the Time 2's speaker.
- **`pebble-companion/`** — a standalone Android app (`dev.pebble.musicbridge`) that resolves and streams the audio, and bridges it to the watch over PebbleKit.

Audio can be routed two ways: **Phone**, where the companion plays locally through a Media3 session, or **Watch**, where PCM is downsampled, IMA-ADPCM encoded and streamed to the watch speaker over BLE.

## Layout

| Path | What it is |
|---|---|
| `watchapp/` | Pebble C app, built with the Pebble SDK (targets `emery`) |
| `watchapp/lib/grid_keyboard/` | Self-contained 3×3 swipe keyboard for text entry |
| `pebble-companion/` | Android companion app — playback, streaming, PebbleKit transport |
| `innertube/` | YouTube Music API client, from Metrolist (GPL-3.0) |
| `watchimage-bridge/` | Go source for `watchimagebridge.aar` (cover-art encoding) |
| `licenses/` | Full license texts for all upstreams |

## Building

### Android companion

```bash
./gradlew :pebble-companion:assembleDebug
```

Output lands in `pebble-companion/build/outputs/apk/debug/`.

`pebble-companion/libs/watchimagebridge.aar` is checked in as a build input. To rebuild it from source you need Go and `gomobile`:

```bash
cd watchimage-bridge
gomobile bind -target=android/arm64 -androidapi 26 \
  -javapkg dev.pebble.watchimagebridge \
  -o ../pebble-companion/libs/watchimagebridge.aar watchimagebridge
```

### Watchapp

Requires the [Pebble SDK](https://github.com/google/pebble) (tested with Pebble Tool 5.0.38 / SDK 4.17):

```bash
cd watchapp
npm install
pebble build
pebble install --phone <ip>
```

## Licensing

This project as a whole is **AGPL-3.0-or-later** — see [`LICENSE`](LICENSE).

It is derived from [Metrolist](https://github.com/MetrolistGroup/Metrolist) (GPL-3.0-or-later) and links [mirrormsg/watchimage](https://github.com/killdano/mirrormsg) (AGPL-3.0-or-later). GPL-3.0 and AGPL-3.0 are compatible, but the combined work is governed by the stricter AGPL terms, so that is what applies here. Files originating from GPL-3.0 upstreams keep their original terms; only the combined work is distributed under AGPL-3.0.

Full attribution for every upstream is in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

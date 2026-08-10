# Watchapp

The dreamwave watchapp (`watchapp/`) is a native Pebble C app targeting **emery** (Pebble Time 2). It is the remote control and, optionally, the speaker for the whole system: it browses, searches and controls playback on the phone, or plays audio itself through the Time 2's speaker.

UUID: `38d45a52-e0f8-4db0-92ad-1fc852703e69` — the companion app uses it to address this watchapp on the PebbleKit channel.

## Screens

| Screen | What it does |
|---|---|
| **Home** | Now-playing hero over a four-icon dock (bespoke) or a classic menu. The hero shows, in priority order: bridge disconnected → now playing → idle. |
| **Search** | Entry point for the three search modes (below). |
| **Keyboard** | On-watch text entry for search queries. |
| **Results** | Search results page (up to the configured results limit). |
| **Buffering / Playing / Paused / Error** | The transient playback states; Playing/Paused form the Now Playing screen. |
| **Queue** | The upcoming-track queue. |
| **Library** | Library sections (below). |
| **Library Items** | Item list inside a library section (holds up to 60 entries). |
| **Menu** | Search / Library / Settings / About. |
| **Settings** | Day-to-day options (audio route, volumes, loop, shuffle, input mode, …). |
| **Advanced** | Deeper interface/library/audio tuning — locked until unlocked from About. |
| **About** | Version, credits, and the Advanced-unlock gesture. |

## Search

Three modes, chosen per query (**Search type**):

- **Song Search** — plain track search.
- **Artist Radio** — a radio mix seeded from the matching artist.
- **Song Radio** — a radio mix seeded from the matching song.

Two input methods (**Input mode**):

- **Voice search** (default) — Pebble dictation.
- **Keyboard** — on-watch text entry. **Grid** (a 3×3 swipe keyboard, `lib/grid_keyboard/`, self-contained) on the Time 2, **Classic** otherwise. The keyboard option is hidden until Advanced is unlocked.

Recent searches are remembered (configurable limit) and shown under Library → Recent Searches.

## Library

| Section | Contents |
|---|---|
| **Recently Played** | Playback history (size set by **History**, in songs). |
| **Cached Music** | Tracks cached on the phone, playable without re-resolving. |
| **Favorites** | Tracks starred from Now Playing. |
| **Playlists** | Playlists created/managed in the companion app. |
| **Continue** | Resume points — tracks with a saved playback position. |
| **Recent Searches** | Past queries; selecting one re-runs it. |

**Library extras** (Advanced) toggles additional sections.

## Now Playing

- Cover art, pushed from the phone (encoded on-device by the companion's Go bridge) with an optional **blurred cover-art background**.
- Optional **progress bar** (**Show progress**).
- Transport: play/pause, next/previous, **shuffle**, and **loop** (off / one / all).
- **Independent volume** for the watch speaker and the phone.
- **Audio route** switching (watch speaker ↔ phone) — see below.

## Audio routes

| Route | Behavior |
|---|---|
| **Phone** | The companion plays through a local Media3 session; the watch is purely a remote. |
| **Watch** | The phone downsamples PCM, IMA-ADPCM encodes it and streams it over BLE to the Time 2's speaker. |

Route changes carry a monotonic **route epoch**, so a stale route message from either side never wins over a newer choice. Per-route quality: **Watch quality** = Efficient / Balanced; **Phone quality** = Data Saver / High. **Cache radio** (Advanced → Audio) controls whether radio sessions contribute to the phone-side stream cache.

## Appearance

- **Bespoke UI** toggle — the custom dreamwave layout vs. the stock system-menu look. App-wide.
- **Theme** — e.g. "Dreamwave Teal".
- **Home style** — Kiwi or Unicorn.
- **Home quotes** — rotating quotes on the home screen.

## Settings reference

**Settings** (always visible): audio route, watch/phone volume, loop, shuffle, input mode, progress bar.

**Advanced** — unlock via **About → press SELECT 7× on the version row**:

| Group | Item | Values |
|---|---|---|
| INTERFACE | Keyboard | Grid / Classic |
| | Bespoke UI | On / Off |
| | Theme | theme name |
| | Home style | Kiwi / Unicorn |
| | Home quotes | Show / Hide |
| | Cover art bg | On / Off |
| LIBRARY | History | N songs |
| | Results | N |
| | Library extras | On / Off |
| AUDIO | Watch quality | Efficient / Balanced |
| | Phone quality | Data Saver / High |
| | Cache radio | On / Off |

All settings persist on the watch and are pushed to the companion as config messages, so both sides stay in sync.

## Build

Requires the [Pebble SDK](https://github.com/google/pebble) (tested with Pebble Tool 5.0.39 / SDK 4.17):

```bash
cd watchapp
npm install
pebble build
pebble install --phone <ip>
```

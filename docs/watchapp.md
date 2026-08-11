# Watchapp

The dreamwave watchapp (`watchapp/`) is a native Pebble C app targeting **emery** (Pebble Time 2). It is the remote control and, optionally, the speaker for the whole system: it browses, searches and controls playback on the phone, or plays audio itself through the Time 2's speaker.

UUID: `38d45a52-e0f8-4db0-92ad-1fc852703e69` — the companion app uses it to address this watchapp on the PebbleKit channel.

## Screens

| Screen | What it does |
|---|---|
| **Home** | Now-playing hero over a four-icon dock (bespoke) or a classic menu. The hero shows, in priority order: bridge disconnected → now playing → idle. |
| | Playing, the bespoke hero is **art bleed**: the cover art is the card, running from the top margin down to the dock, with a band across the bottom carrying title, artist and play state, and the progress strip above it. The band takes the accent when the hero is the selected ring entry, the same as any list row. With no art (or **Cover art bg** off) the card falls back to a note glyph on the ground. The dock grows a fifth **Now Playing** entry while something is playing. |
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

## Library

| Section | Contents |
|---|---|
| **Recently Played** | Playback history (size set by **History**, in songs). |
| **Cached Music** | Tracks cached on the phone, playable without re-resolving. |
| **Favorites** | Tracks starred from Now Playing. |
| **Playlists** | Playlists created/managed in the companion app. |

**Library extras** (Advanced) toggles additional sections.

## Now Playing

- Cover art, pushed from the phone (encoded on-device by the companion's Go bridge).
- Optional **progress bar** (**Show progress**).
- Volume opens the same square card the transport confirmations use — same box, same
  place, same border, with the percentage and a level bar where the glyph would be.
  Under the stock UI volume still borrows the progress bar instead.
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
- **Theme** — Default, Dreamwave Teal, Electric Purple, Sunset, Mono, and **Arcade**.

  Under the bespoke UI every theme paints a colored ground — a deep tone of its own hue
  — with bright ink on top, and keeps its familiar accent for fills. Contrast picked the
  values: a mid-tone accent used as *text* on white was under 3:1 (Tiffany Blue 2.9,
  Sunset Orange 3.2), which is why the old secondary text was so hard to read; every
  pairing in the bespoke table is 4.6:1 or better.

  | Theme | ground | accent |
  |---|---|---|
  | Default | `#AA0055` | `#FF55FF` |
  | Dreamwave Teal | `#005555` | `#00AAAA` |
  | Electric Purple | `#550055` | `#AA55AA` |
  | Sunset | `#AA0000` | `#FF5555` |
  | Mono | black | white |
  | Arcade | `#550055` | `#00FFFF` |

  **Arcade** is cyan on deep magenta, and was called Vapor until 0.5.0. Both hues are
  exact; only the ground's lightness has moved, and it has moved twice — `#FF00AA` sat
  at 2.9:1, `#AA0055` brought that to 5.9:1, and `#550055` brings it to 11.5:1, where
  the rest of the themes already were. Contrast is symmetric, so no rearrangement of
  two colors 2.9:1 apart makes small text readable; lightness is the only lever that
  leaves the hue alone. The magenta returns as the ink inside anything the cyan fills.

  Going darker also bought the theme a third color. It had none — dim text and unlit
  icons fell back to the foreground, so "quiet" and "loud" looked identical — and
  `#00AAAA` now clears 5:1 on this ground while reading as clearly dimmer than full cyan.

- **Sophie mode** — swaps every piece of type in the app for LynoJean. Appears under
  **Theme** only while the theme is Mono: the face is a display cut and holds together
  on black and white in a way it does not against the colored grounds. The four sizes
  the app draws at are loaded on demand and freed when it is switched off, so nobody who
  leaves it alone pays the heap for it. The preference survives a theme change but is
  ignored outside Mono, so returning to Mono restores it.

  The stock UI keeps the white ground and colored accent it always had: it hands its
  lists to a system MenuLayer that paints its own white background, so that side cannot
  move off white without stranding text on it. Arcade has no stock counterpart at all and
  is offered under the bespoke UI only — switching back to stock returns you to Default.
- **Home style** — Kiwi or Unicorn. Stock UI only; the bespoke home draws no artwork.
- **Home quotes** — rotating quotes on the home screen. Stock UI only.

## Settings reference

**Settings** (always visible): input mode, audio route, watch/phone volume, progress bar, **Back stops**, Advanced.

**Back stops** decides what BACK does on Now Playing. **On** (the original behavior) tears
the stream down on the way out. **Off** makes BACK pure navigation — the track keeps
playing, and Home keeps its hero. Every other way out of playback is unaffected either
way.

**Advanced** — unlock via **About → press SELECT 7× on the version row**:

| Group | Item | Values |
|---|---|---|
| INTERFACE | Keyboard | Grid / Classic |
| | Bespoke UI | On / Off |
| | Theme | theme name |
| | Home style | Kiwi / Unicorn — stock UI only |
| | Home quotes | Show / Hide — stock UI only |
| LIBRARY | History | N songs |
| | Results | N |
| | Library extras | On / Off |
| AUDIO | Watch quality | Efficient / Balanced |
| | Phone quality | Data Saver / High |
| | Cache radio | On / Off |

Rows that only apply to one of the two UIs are hidden under the other, so Advanced never
offers a toggle that changes nothing you can see.

All settings persist on the watch and are pushed to the companion as config messages, so both sides stay in sync.

## Build

Requires the [Pebble SDK](https://github.com/google/pebble) (tested with Pebble Tool 5.0.39 / SDK 4.17):

```bash
cd watchapp
npm install
pebble build
pebble install --phone <ip>
```

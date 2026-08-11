# Changelog

## 0.5.0

Now Playing has been rebuilt, and the artwork is the reason. It used to be a full-bleed
background with the entire interface laid on top of it, defended only by a global
black/white flip driven by the cover's average brightness — which meant a mid-tone cover
put white text over light patches and dark patches alike. The screen is now two cards,
nothing but badges is ever drawn on a cover, and that brightness scan is gone.

### Now Playing

- **Two cards.** The artwork takes Home's entire card footprint — 190×150 at (5,6), art
  *and* band — so opening Now Playing from Home turns the card you were looking at into
  pure cover without moving its edges. Below it, detached, an accent card carries the
  track.
- **Bigger type.** Title at 24 bold over artist at 18, bottom-aligned against the card's
  lower edge rather than floating in the middle of it.
- **Corner badges.** Output top-left and favourite top-right, remaining and total time as
  chips on the bottom two. Each sits on a solid disc or pill of the ground colour, so
  none of them depends on what the cover looks like underneath.
- **Progress moved into the gap** between the two cards, at 6px on a black track. At 4px
  on light gray it was getting lost — unlike Home's rail, which is pinned between the art
  and the band and reads off both, this one floats with nothing to give it an edge.
- **Shuffle and loop** are vertically centred in the accent card, drawn as ink-filled
  discs when enabled rather than tinted glyphs. The accent fill leaves no third colour to
  dim an idle icon with, so state is carried by shape.
- **No transport button.** Play state is shown by veiling the artwork with a checkerboard
  scrim and putting a resume glyph in the middle of it. A playing screen is just the cover
  and the card.
- **Battery-saver reclaims the gap** — with the progress rail hidden the accent card grows
  upward instead of leaving a band of bare ground.

### One place for confirmations

Volume, next, previous, favourite, shuffle and output all answer on the artwork now, using
the same veil the paused state uses. The 72px card that used to flash over the middle of
the screen, and the separate square volume popup, are both gone — they sat on top of the
cover, each had to pick an ink that would survive whatever was behind it, and once pausing
started veiling the artwork there were two competing answers to the same press.

Play/pause has no confirmation at all: the artwork already answers that one across the
whole screen, and the card was landing on top of the answer.

### Artwork placeholder

A missing cover used to be a bare note glyph on the ground, which read as "nothing is
playing" rather than "the artwork hasn't arrived". Both Now Playing and Home's card now
draw a gray placeholder card, and while a transfer is actually running it shows how far in
it is — information neither screen was carrying anywhere.

- **Fixed:** the canvas was only marked dirty when a cover-art transfer *completed*, never
  while it ran. Now Playing masked this with its once-a-second redraw; Home has no such
  timer, so its placeholder never updated at all.

### Touch

Every tap target on Now Playing is gone. The only gesture left is the long-press that
swaps to full-screen artwork — the one thing with no button equivalent. Play/pause,
shuffle, loop and output all had hit-tests that had to be kept in step with wherever the
layout put their glyphs, and every one of them is reachable from a button or the More
popup. Note that a tap no longer dismisses the full-screen view; long-press again, or
press any button.

### Themes

- **Vapor is now Arcade**, and its ground moved from `#AA0055` to `#550055` — 5.9:1 to
  11.5:1, where every other theme already was. It also gains a third colour for the first
  time (`#00AAAA` for dim text and unlit icons), where before it had to fall back to the
  foreground and nothing could look quieter than anything else.
- **Sophie mode** — a new row under **Theme**, shown only while the theme is Mono, that
  swaps every piece of type in the app for LynoJean. Loaded on demand and freed when
  switched off.

### Home

- The now-playing card no longer draws a progress rail. That belongs to Now Playing, which
  is one press away and has room to show it properly; drawing it in both places also meant
  Home repainting once a second to advance four pixels.

### Defaults

New installs start from a different place. Everything remains changeable in **Settings** or
**Settings → Advanced**.

| Setting | Was | Now |
|---|---|---|
| Bespoke UI | Off | **On** |
| Keyboard | Classic | **Grid** |
| Progress bar | On | **On** (unchanged) |
| Back stops | On | **Off** |
| History | 5 songs | **20 songs** |
| Results | 5 | **10** |
| Cache radio | On | **Off** |

**Cover art bg has been removed** as a setting and is always on. It was a toggle until
now; with the artwork occupying Now Playing's whole upper half and Home's card face,
switching it off left both screens built around a hole. The persisted key is deliberately
not read back, so anyone who had it off does not boot into an empty upper half with no row
left to turn it back on.

### Companion

- **Appearance screen** — the phone picks its theme and light/dark preference
  independently of the watch. The choices are the *watch's* themes, mirrored from
  `bespoke_colors()`, so the app wears the colours that theme actually paints on the
  watch. **Match the watch** remains the default.

---

## 0.4.0

Album art and system media controls; watchapp and companion versions aligned.

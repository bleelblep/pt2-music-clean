# Changelog

## 0.7.0

Now Playing stops moving around, and the two toggles nobody could tell apart finally
look like different things.

### Now Playing

- **The artwork expands instead of re-framing.** The Home card, the Now Playing card and
  the full-bleed view each cropped the cover to their own aspect ratio, so every one
  showed a differently-framed window and the subject slid as you moved between them — and
  the full-bleed view did not crop at all, it *stretched*, so a square sleeve gained 14%
  of height the moment you opened it. There is one composition now, scaled to the display
  and centre-cropped once, and each surface paints the part of it that falls inside its
  own rect. Opening one from another grows the frame without moving a pixel of art.
- **One button, everywhere.** Play, pause, next and previous were three hand-tuned copies
  of the same triangle — 14x18 on the play button, 11x14 in the action bar, and 32x52 for
  the skips, half again as tall for its width as the other two with its tip stopping short
  of its own bar. Put beside a play button the skip glyph read as a different icon set.
  All four now derive from one geometry. Every answer the artwork gives — resume, skip,
  favorite, shuffle, output — is the same accent disc in the same place, dead centre.
- **Shuffle and loop are distinguishable.** Both were two horizontal strokes with an
  arrowhead at opposite ends, same box, same weight, sitting 24px apart. The only
  difference was which end the arrowheads were on, which is not a difference anyone reads
  at 22px. Loop is a closed ring now and shuffle is a crossing X — they separate as
  silhouettes, before any detail resolves.
- **An off toggle is simply absent**, rather than drawn at full strength on a card where
  nothing is tappable. With neither lit the title and artist take the whole card, 170px
  instead of the 120px they were capped at to clear the toggle column.
- **Now Playing touch is target-driven, not long-press driven.** The artwork-only toggle
  moves off the stationary hold and onto a dedicated bottom swipe target, so touching and
  waiting no longer flips the whole screen by accident. Touch now runs through a center
  deadzone and directional targets around it: upper-left previous, upper-right next,
  lower-left shuffle, lower-right loop, top output (or play/pause on Symfonium), bottom
  artwork-only.

### Scrolling

- **Hold to scroll goes the distance.** A long-press handler was subscribed on Up and Down
  for every screen, and a long-click subscription takes the button over once its threshold
  passes — so holding scrolled for 600ms and then stuck, and long lists could only be
  walked a press at a time. It is subscribed now only on the keyboard and Now Playing,
  where it actually means something.
- **Lists wrap at both ends**, including About, which was hard-clamped at its first and
  last rows.

### Keyboard

- **T9 replaces the Classic keyboard.** Classic was a three-level drill-down — 27
  characters narrowed to 9, then to 3, then typed — on a sidebar that shared nothing with
  the Grid keyboard beside it in the same setting. T9 is the same 3x3 keypad, typed the
  way a phone did it: tap a key for its first character, tap the same key again within
  800ms to cycle to the next, stop for 800ms (or tap elsewhere) to keep it.
- **The letters are a phone's**: `abc` on 2 through `wxyz` on 9, four of them on 7 and 9,
  which is where every keypad ever made put them. Spreading 26 letters over eight keys is
  what leaves the 1 key free, and that is where the space lives — tap it once for a space,
  twice for a zero, the two characters a keypad has never had a letter key for. The Grid
  keeps its own even three-per-key layout, because there a key's three characters are
  three swipe directions and a fourth would have nowhere to point.
- **The keys are printed like a phone's.** One character large, the key's three small
  underneath, in the same nine positions the Grid puts them. On the letter screens the
  large one is the key's number, set in the quiet ink — it is a landmark, and what a hold
  types. While a key is cycling it lights up and the character you are on inverts against
  it.
- **Hold a key for its number.** 450ms on any key on the letter screens types the digit
  printed on it, as a phone did, so 1–9 need no screen of their own. A key's number is
  simply its position, so nothing has to be kept in step to say which is which.
- **The third screen is punctuation, not a number pad.** With 1–9 a hold away and 0 on the
  1 key, it had no reason to spend nine keys on digits. It carries 27 marks — `.,?` `!'"`
  `@#$` `%&*` `-+=` `()/` `:;_` `~[]` `<>|` — which is every symbol the Classic keyboard
  offered plus ten it did not (`%` `=` `;` `_` `~` `[` `]` `<` `>` `|`). These keys hero
  the character a single tap gives, rather than a number they no longer type.
- **Typing is by touch; the buttons are the Grid's.** UP cycles abc/ABC/!?# and DOWN
  deletes on either keyboard, and BACK leaves. On T9 there is nothing left for SELECT to
  type — every character is reachable by tapping or holding the key it is printed on,
  which is the whole difference from the Grid's swipe-off-the-key — so it runs the
  search, as the long press already did.

### Launching

- **Quick launch opens Now Playing.** Put the watchapp on a quick-launch button (a long
  press from the watchface) and it lands on the current track instead of Home. It waits
  for the companion's first state snapshot rather than guessing — that is what knows
  whether anything is playing, and Now Playing draws nothing without a track — so the app
  still paints Home first, and stays there if the answer is that nothing is playing. BACK
  returns to Home. Every other way of opening the app is unchanged.

### Music sources

- **Auto shuffle playlists** (Settings → Auto shuffle, shown only under Symfonium). It
  starts the playlist through Symfonium's own shuffle action rather than starting it and
  toggling shuffle afterwards — that toggle is a blind flip with no "set on", so it would
  have turned shuffle *off* whenever it was already on, and raced the play it followed.
- **Shuffle and repeat stick.** Symfonium's session publishes no shuffle or repeat state —
  it declares neither `SET_SHUFFLE_MODE` nor `SET_REPEAT_MODE`, so the fields the watch
  was reading were always empty and the indicators sat off however you had it set. The
  only signal is the custom action's icon, which changes with the state but never says
  which id means what. That mapping is learned as it goes — when the icon moves, the id
  you were on meant the state you believed and the one you moved to means the other — and
  remembered, so a reconnect adopts the real state instead of assuming "off" and
  clobbering the watch's own value every time Symfonium's session drops.
- **No controls that cannot do anything**: the output badge and the favorite heart are
  hidden under Symfonium, which already hid the Output row in Settings and in More.
- **Symfonium touch drops output entirely.** On the new touch target layout, the top
  target maps to play/pause under Symfonium instead of route switching, because Symfonium
  has no watch-speaker route to switch to.
- **Recently Played lists the songs you actually played.** Symfonium's recent-played
  node only serves albums, and its rows asked the watch to play a container — not
  playable. If you keep a *recently played* smart playlist in Symfonium, the watch now
  reads it: real songs in real play order with the real artist in the second line, and
  the playlist is hidden from Playlists so nothing is listed twice. Without one, the
  old album walk fills in (songs from recently played albums, album name in the second
  line), so the screen is never empty.
- **Most Played joins the Library** (Symfonium). Backed the same way: keep a *most
  played* smart playlist in Symfonium and it appears as a watch library row.
- **History reaches 100 under Symfonium.** The History setting (Advanced) topped out
  at 20 songs. With Recently Played smart-playlist backed there is real depth to show,
  so the Symfonium grid runs 20/40/60/80/100; YouTube keeps 5–20.

### Under the hood

- **1.2KB of static budget freed.** The app is bounded by a hard 65535-byte ceiling on
  `.text + .data + .bss` — `PebbleProcessInfo.virtual_size` is a uint16_t — which no SDK
  or firmware update can raise. The 1683 bytes of prose explaining the Watch and Bridge
  rows now live in a resource, loaded to the heap on demand and freed on the way out.
  Heap and the resource pack sit outside the ceiling, so that text costs nothing against
  it. Code cannot move the same way; there is no overlay mechanism.
- **Where that leaves the budget.** Built as of this release the app measures 61011 bytes
  of virtual size against the 65535 ceiling — 4524 free — with a load size of 59724.
  Classic's three 27-character tables, its tap-string table and its two levels of
  drill-down state went with it; T9 spends nothing on a digit table, because a key's
  number is its own position.
- Built against SDK 4.33 (from 4.9.169).

## 0.6.0

Symfonium is now a music source you can switch to, instead of a second watch app nobody
could reach.

### Music sources

- **Two backends, one watch app.** `SymfoniumPlaybackService` — which mirrors Symfonium's
  own player to the watch, artwork and all — was previously only reachable from a separate
  `music-src` watchapp forked off the v0.2.0 UI. Routing was by watchapp UUID, so on the
  app you actually run it was dead code. The choice now lives on the wire
  (`keyConfigMusicSource`), the fork is gone, and `PebbleMusicService` dispatches on the
  selected source rather than on who sent the message.
- **Changeable from either end**, guarded by a **source epoch** that works like the route
  epoch: whoever changes it bumps the epoch, and an older epoch is a stale echo. Without
  it the watch's settings sync would overwrite a phone-side change — the bug that got the
  phone's settings screen deleted in the first place.
- **Phone UI**: a source pill on Home showing and switching the active source, and a
  read-only mirror of Symfonium's now-playing (it owns its own transport). Search and
  Library follow the source too: under Symfonium, Search offers Songs / Albums / Artists
  from its library and Library lists Symfonium's recently played, favorites and
  playlists. The YouTube-only pieces — radio modes, the on-disk cache shelf, playlist
  editing — hide while that source is active; only the Cache screen keeps its notice.
- **Watch UI**: Settings → Music source. While Symfonium is active the watch hides Output
  (Settings *and* the now-playing More popup), watch volume, the audio-quality and
  cache-radio rows, Cached Music, and the Song Radio search mode — and search offers
  **Song / Album / Artist** modes instead of the YouTube radios.

### Fixed

- **The route could wedge on Symfonium.** Its snapshots hardcoded `routeEpoch = 0` while
  the watch persists its own epoch across reboots and applies a route only when the epoch
  is newer. Once you had ever toggled the route under YouTube, every "audio is on the
  phone" snapshot was silently dropped and the watch sat on the Watch route waiting for a
  stream that never comes. The backend now adopts and echoes the watch's epoch, and
  switching to Symfonium asserts the phone route with a fresh one.
- **Long Symfonium media ids broke playback.** The watch stores ids in 81-byte buffers;
  anything longer arrived truncated, came back truncated, missed the item cache and read
  as "song no longer available". Over-long ids now travel as short stable tokens.
- **Playlists row served the wrong library type** — `sendLibrary` repurposed
  `libraryContinue` to mean Playlists, a leftover from the fork's different menu.
- **Symfonium favorites were capped** on the watch. First at 40, where the backend's hard
  cap sat below the watch's request; raising it to 60 only moved the ceiling to the size
  of the watch's array. Favorites are now read through the same sliding window Deep search
  uses (below), so the list has no ceiling at all: the watch holds a window and pages it,
  and the companion walks the browse id to its end once, caches it, and reports the real
  total so the scrollbar has something honest to measure. Symfonium and Bespoke UI only —
  the stock MenuLayer has no notion of a window, and the YouTube companion answers a
  library request with one whole list and no total, so both keep the unpaged request they
  always sent.
- **Cover art re-streamed on every pause/resume under Symfonium.** Three compounding
  causes: art identity was keyed on the payload hash, but Symfonium re-encodes the same
  cover on every metadata republish; a mid-transfer republish started a second parallel
  transfer because in-flight art wasn't marked; and above all the settings-sync handler
  reset the "art sent" marker whenever the watch's settings blob merely *contained* the
  cover key — which is every sync, and the watch syncs on every snapshot. Art identity is
  now the track id, in-flight transfers are tracked, settings effects apply only on real
  changes, and Symfonium's tiny placeholder stand-in is skipped until the real cover
  arrives.
- **Symfonium album art often stayed a placeholder until you skipped a song.** Four
  causes, all of them the same shape — the phone believing the watch had art it did not.
  Opening the watchapp mid-track sent no cover at all: the watchapp frees its art buffer
  on exit while the *track* is unchanged, so the "already sent this one" guard concluded
  the job was done and only a track change ever dislodged it (the YouTube backend clears
  `deliveredCover` on Hello for exactly this reason; Symfonium now does the same). A
  metadata republish during a transfer — Symfonium emits them freely — cancelled the job
  partway, and because the in-flight marker was only cleared on the normal path, that
  track stayed marked in-flight forever and every later attempt returned early; the
  marker is now released in a `finally`, and a republish for a track already being worked
  on leaves the running transfer alone instead of restarting it from chunk zero. A
  transfer whose header the watch never took still streamed all 41 chunks into nothing
  and was recorded as delivered; the header's result is now honoured and the whole
  payload retried. And Symfonium's stand-in cover was skipped in the hope of a republish
  carrying the real one, which does not reliably arrive — the art is now chased with a
  few spaced attempts (under an overall deadline) rather than waited for.
- **Picking a song sometimes never opened Now Playing (search and playlists alike).**
  Symfonium's legacy bridge reports a transient `IDLE` between `setMediaItem` and
  `prepare`, which the companion relayed as "playback idle" — and the watch treated idle
  as "playback died" and left the Buffering screen, racing the real start. The companion
  now reports an item-loading idle as buffering, and the watch no longer abandons
  Buffering on an idle snapshot (genuine failures still arrive as errors).

### Added (Symfonium)

- **Deeper search, and a sliding window to hold it.** Symfonium's search answers with a
  fixed 15 songs however broad the query — established from its own debug log, which
  echoes the pagination extras we send, shows the matching query running with no SQL
  `LIMIT` (4,631 rows for `"a"`), and then truncates to 15 while building the response.
  Neither `EXTRA_PAGE`/`EXTRA_PAGE_SIZE` nor a media-focus hint moves it, so the cap is
  Symfonium's alone.

  What *is* uncapped is browse. The albums a search matches are browsable, and walking
  them roughly triples the songs a query reaches — measured, `"da"` 15 → 40 and
  `"summer"` 13 → 36, for about 10 ms per album, since these are local database reads.
  The companion now does that walk whenever the caller asked for more rows than the
  search returned, which improves the phone's Search screen and the watch's alike; with
  Results at 5 or 10 the direct hits already fill the request and nothing is walked.

  The **Results** setting gains a third value beyond 5 and 10: `Deep`, offered on
  Symfonium with the Bespoke UI. Rather than holding the whole result list, the watch
  keeps a **sliding window** of it — pages of 12 arrive as the selection approaches an
  edge, the window slides once full, and scrolling back up fetches the earlier page
  again. The selection became an index into the whole list rather than into the rows the
  watch happens to hold; every other list leaves the window at base 0, where the new
  accessors reduce to the plain indexing they replaced, so Library, Queue and a capped
  search are untouched.

  The backend holds the ranked list against the watch's search request id — which the
  watch keeps constant across the pages of one search and bumps for a new one — so every
  page is a slice of a single ranking. Re-running the search per page would let the
  library shifting underneath duplicate or skip rows across a page boundary.

  One page is in flight at a time (holding Down would otherwise queue a request per row
  and interleave the replies), and a page that goes unanswered for six seconds is
  released so moving the selection asks again rather than leaving the list loading
  forever.

### Changed

- **Two neutral default themes, and the old Default is now Jazzberry.** Every theme so far
  painted a ground in a hue of its own, which meant there was no quiet option: the app
  opened on a berry ground with a shocking-pink accent whether you wanted an opinion or
  not. **Default Dark** (black) and **Default Light** (white) are the neutral pair, they
  are siblings apart from the inverted ground, and the app now starts on Default Dark.

  Their single accent is `GColorLiberty`, a muted indigo no other theme uses, chosen to
  not attract attention: its fill sits at 3.3:1 on the dark ground — the same band every
  colour theme here already uses (Jazzberry 2.8:1, Teal 3.0:1, Sunset 2.5:1) — with white
  text on it at 6.4:1 on both grounds.

  Emery renders 64 colours, two bits a channel, so the only true neutrals available are
  black, `#555555`, `#AAAAAA` and white. The mid greys are what the pair deliberately
  avoids: on `#555555` the dim text has nowhere to sit, because `#AAAAAA` lands at 3.2:1
  against it and the next step up is the bright text itself. Black and white grounds leave
  room for both.

  Under the stock UI the two look identical — the system MenuLayer paints its own white
  background, so Default Dark cannot bring a black ground across without stranding white
  text on white, and it degrades to its light sibling exactly as Mono already does.

  No existing theme changed colour. The old **Default** keeps its palette and is renamed
  **Jazzberry**, after the ground it always used. Theme values are appended rather than
  inserted, so a persisted theme from an older build still names the same theme it did.

- **Headers are set in LECO 1976.** The eyebrow on every Bespoke list, the Advanced group
  labels and the document mastheads (What's New, About, Credits, Watch, Bridge) leave
  Gothic for the face Pebble's own firmware carries. The app ships its own full-ASCII cut
  of it: every LECO the platform exposes is a numeral subset, which is why the masthead
  could only ever match on Gothic before. Mastheads set at 26 rather than 28 — LECO is the
  wider face and at 28 "What's New" and "dreamwave" both measured within a couple of
  pixels of their column. Sophie mode still overrides it, being a deliberate whole-app
  face choice.

- **The scrollbar stopped crowding the rows.** Its 10px capsule was drawn at x188–198 and
  the rows ran to x195, so the lozenge sat *inside* the selection pill — and an accent
  thumb on an accent pill is not a thin indicator, it is no indicator at all for exactly
  the row being looked at. The capsule is 8px and Bespoke rows now stop at x183, leaving
  7px of ground between row edge and lozenge: the same inset the row keeps on its other
  three sides. The document screens' text columns came in to match.

- **Watch and Bridge explain themselves.** Pressing SELECT on either turns the readout
  into a glossary — every row gains a plain line saying what its number actually is — and
  pressing again puts it back. These are documents with no selection to land on, so there
  was nothing for a per-row press to be about; the mode applies to the whole screen. A
  line under the masthead offers it, since neither screen has a footer band to hint in.

- **The mascot is gone from the Queue screen.** Its loading and empty states were the last
  place in the Bespoke UI still drawing the 48px mascot above the message; they now use
  the same flat `bespoke_empty()` treatment as Library items, so every empty state in the
  app reads alike. Empty Queue also gains the "BACK close" hint the other empty states
  carry. (The stock-UI mascot sites are untouched.)

### Added

- **Search works on the Symfonium source — properly.** It previously answered "Search
  isn't available yet", then briefly tried play-the-query (`playFromSearch`), which
  hijacked playback and could only ever return the one best match. The real answer:
  Symfonium searches its offline copy of the library through `onSearch`, and the way to
  reach it is androidx.media's `MediaBrowserCompat.search()` — Media3's bridge parcels
  the callback as a support-library `ResultReceiver` Symfonium cannot unmarshal (a fatal
  crash on its main thread; Media3's `getItem()` is equally poisonous and likewise
  avoided), while the androidx.media client parcels classes Symfonium does ship. Ranked
  results arrive without touching playback, and a picked row plays via
  `playFromMediaId`, the same call an Android Auto tap makes.
- **Album and artist search on the Symfonium source** (watch Search-type picker and the
  phone's mode chips). Symfonium's search answers songs, albums and artists in one ranked
  list; the modes just pick their slice. Tapping an album plays it in order from track 1
  (its Shuffle pseudo-row is skipped); tapping an artist plays their "All tracks" row.

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

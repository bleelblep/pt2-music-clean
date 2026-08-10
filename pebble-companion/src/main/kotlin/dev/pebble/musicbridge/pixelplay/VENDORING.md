# Vendored PixelPlayer UI

This package is a **fork of PixelPlayer's visual layer**, not a reimplementation
of it. Source: [PixelPlayerHQ/PixelPlayer](https://github.com/PixelPlayerHQ/PixelPlayer)
(`theovilardo/PixelPlayer` redirects here), vendored at commit **`39030156`** — the
last commit released under the MIT license.

> **License pin matters.** PixelPlayer changed its license on 2026-05-12
> (`ceec1bb`, "Changed license from MIT to Proprietary License"): anything from that
> commit onward is proprietary and prohibits redistribution in source or binary form.
> Commits *before* it remain MIT, and MIT is irrevocable for already-released code —
> which is why this package vendors `39030156` and must never "upgrade" past
> `ceec1bb`. The MIT license text is at `licenses/MIT-pixelplayer.txt`.

Every `.kt` file here except the four listed under *Adapters* is upstream's file
with exactly one mechanical change applied:

```
package com.theveloper.pixelplay…  ->  package dev.pebble.musicbridge.pixelplay…
import  com.theveloper.pixelplay.…  ->  import  dev.pebble.musicbridge.pixelplay.…
import  com.theveloper.pixelplay.R  ->  import  dev.pebble.musicbridge.R
```

No layout, colour, spacing or animation values were touched. That is deliberate:
it is what makes the player render identically to upstream, and it is what makes
re-syncing possible — re-copy the file and re-run the rewrite.

## Why not vendor the whole thing

`FullPlayerContent` imports `PlayerViewModel`, and the transitive closure from
there is **369 files / ~133k lines**: Hilt, Room, MediaStore, Navidrome,
Telegram, Google Drive, cast, lyrics, sync workers, Glance widgets. That is the
entire application.

Cutting the graph at the ViewModel and at the feature sheets this app has no
data for leaves the actual visual layer, which is ~6k lines. That is what is
here.

## Added after the initial import

| File | Why |
|---|---|
| `presentation/navigation/Transitions.kt` | Screen push/pop transitions; the app shell's `AnimatedContent` uses them verbatim. |
| `presentation/components/scoped/SheetMotionController.kt` | Sheet translationY/expansionFraction motion, vendored so the companion's player sheet moves like `UnifiedPlayerSheetV2`. |
| `presentation/components/scoped/SheetVerticalDragMath.kt` | Drag-frame math + fling thresholds + collapse spring helpers used by the gesture handler. |
| `presentation/components/scoped/SheetVerticalDragGestureHandler.kt` | The sheet's vertical-drag behavior (velocity carry-over, collapse squash) verbatim. |

## Adapters — the deliberately non-1:1 files

| File | Why |
|---|---|
| `presentation/viewmodel/PlayerViewModel.kt` | Stands in for upstream's ViewModel, exposing only the members the MIT-era `FullPlayerContent` touches (a superset of them, so both call shapes compile). Keeping the *shape* identical is what lets `FullPlayerContent.kt` stay verbatim. |
| `presentation/components/LyricsSurfaceStubs.kt` | `LyricsSheet` renders nothing — there is no lyrics provider here. Signature matches upstream's call site; `onTranslateViaAi` defaults to a no-op because the MIT-era call does not pass it. |
| `presentation/components/subcomps/FetchLyricsDialog.kt` | Same, for the fetch dialog. |
| `data/repository/LyricsSearchResult.kt` | Upstream declares it inside its LrcLib client; `record` is dropped to avoid pulling in networking. |
| `utils/Formats.kt` | Trimmed to `formatDuration` (verbatim) — the rest resolves strings through upstream's Application singleton, which this module does not have. |
| `utils/AudioMetaUtils.kt` | Trimmed to `mimeTypeToFormat` (verbatim) — the rest reads local files via MediaMetadataRetriever and Room, neither of which exists here. |
| `data/preferences/AlbumArtQuality.kt` | Lifted verbatim from the MIT-era `UserPreferencesRepository.kt` (no standalone file upstream). |
| `presentation/components/NavigationBarInsets.kt` | Lifted verbatim from the MIT-era `PlayerInternalNavigationBar.kt` (no standalone file upstream). |

## Local patches inside otherwise-verbatim files

Each is marked `LOCAL PATCH` in the source. Grep for that string before re-syncing.

| File | Patch |
|---|---|
| `player/FullPlayerContent.kt` | `showCollapseButton: Boolean = true` parameter, guarding the down-chevron. There is no sheet to collapse into here. |
| `player/FullPlayerContent.kt` | Both lyrics buttons guarded on `LocalShowLyricsButton` (see `player/PlayerChromeLocals.kt`, a local addition). They sit two composables below `FullPlayerContent`, so a CompositionLocal is a much smaller diff than threading a flag through four signatures. |
| `utils/LyricsImportSecurity.kt` | `validateImportedLrcContent` returns `Invalid` where upstream called `LyricsUtils.parseLyrics`. `LyricsUtils` pulls in kuromoji and pinyin4j to romanize Japanese and Chinese lyrics — a lot of weight for a path that is unreachable here. |
| `ui/theme/Type.kt` | `GoogleSansRounded` reads `R.font.robotoflex_variable` instead of `R.font.gflex_variable`. Upstream bundles Google Sans Flex, which is **not openly licensed** and cannot be redistributed; this app ships **Roboto Flex** (SIL OFL-1.1, `licenses/OFL-1.1-robotoflex.txt`). The `ROND` variation setting is a Google Sans Flex axis and is harmlessly ignored by Roboto Flex. |

All patches preserve upstream behaviour by default, so they are inert unless a
caller opts out.

## Notes on the MIT-era content

- String resources referenced by the vendored player use upstream's MIT-era names
  (`presentation_batch_g_player_cd_*`, `setcat_now_playing`); the values live in
  this module's `res/values/strings.xml`. `setcat_now_playing` is referenced by the
  MIT-era code but not defined in upstream's app resources, so we define it
  ourselves ("Now Playing").
- `data/preferences/AlbumArtQuality.kt` and
  `presentation/components/NavigationBarInsets.kt` are lifted verbatim from the
  MIT-era `UserPreferencesRepository.kt` and `PlayerInternalNavigationBar.kt`
  respectively — the classes exist there, just not as standalone files.
- Upstream's proprietary-era-only `AdvancedPerformanceDiagnostics.kt` is **not**
  vendored (nothing at the MIT commit references it).
- `AlbumCarouselSelection.kt` here is the MIT version: it lacks upstream's later
  rapid-skip carousel fixes. Known, accepted — do not "fix" it by copying from a
  post-`ceec1bb` commit (see the license pin above).

## Re-syncing

Only ever from a **pre-`ceec1bb`** commit:

```bash
git clone https://github.com/PixelPlayerHQ/PixelPlayer.git
git checkout 39030156   # last MIT commit — do not go past ceec1bb
# re-copy the file, then:
sed -e 's/^package com\.theveloper\.pixelplay/package dev.pebble.musicbridge.pixelplay/' \
    -e 's/^import com\.theveloper\.pixelplay\./import dev.pebble.musicbridge.pixelplay./' \
    -e 's/^import dev\.pebble\.musicbridge\.pixelplay\.R$/import dev.pebble.musicbridge.R/' \
    -e 's/com\.theveloper\.pixelplay\./dev.pebble.musicbridge.pixelplay./g' \
    upstream/File.kt > File.kt
```

The last rule matters: `FullPlayerContent.kt` has fully-qualified
`com.theveloper.pixelplay.utils.LyricsImportSecurity` references in method
bodies, not just imports.

## Licence

PixelPlayer at `39030156` is **MIT** (`licenses/MIT-pixelplayer.txt`; upstream's
LICENSE file has a placeholder copyright line — the author is Theo Vilardo).
Roboto Flex (`res/font/robotoflex_variable.ttf`) is SIL OFL-1.1
(`licenses/OFL-1.1-robotoflex.txt`).

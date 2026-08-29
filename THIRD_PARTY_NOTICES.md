# Third-Party Notices

This project (the **music-pbl** Pebble watchapp and its **music-src**
Android bridge) builds on, links against, or is inspired by the open-source
projects below. Each is credited on the watchapp's **About** screen. Full
license texts are in the [`licenses/`](licenses/) directory.

| Project | Role in this project | License | Upstream |
|---|---|---|---|
| **Metrolist** | Music-client foundation the companion app is derived from | GPL-3.0-or-later | https://github.com/MetrolistGroup/Metrolist |
| **PipePipe Extractor** | YouTube search & audio-stream extraction (companion) | GPL-3.0-or-later | https://github.com/maxrave-dev/PipePipeExtractor |
| **PebbleKit / Rebble (PebbleKit Android 2)** | Watch+" Android communication | Apache-2.0 | https://github.com/pebble-dev/PebbleKitAndroid2 |
| **mirrormsg / watchimage** | Cover-art encoding pipeline (companion; linked as `watchimagebridge`) | AGPL-3.0-or-later | https://github.com/killdano/mirrormsg |
| **PixelPlayer** | Player UI layer (vendored under `pixelplay/` in the companion). Vendored at the last MIT-licensed commit (`39030156`); later commits are proprietary and are not used. | MIT | https://github.com/PixelPlayerHQ/PixelPlayer |
| **Roboto Flex** | Typeface bundled with the companion app (`robotoflex_variable.ttf`) | SIL OFL-1.1 | https://github.com/TypeNetwork/Roboto-Flex |
| **LECO 1976** | Typeface bundled with the watchapp (`LECO1976Regular.otf`), used for screen headers. Taken from the PebbleOS source release (`resources/normal/base/ttf/`), where it is the face behind the firmware's `FONT_KEY_LECO_*` numeral fonts. **See the note below.** | See note | https://github.com/google/pebble |
| **Bobby Assistant** | Home-screen interaction inspiration (watchapp) | Apache-2.0 | https://github.com/pebble-dev/bobby-assistant |
| **Tertiary Text** | Three-button keyboard concept (watchapp) | MIT | https://github.com/vgmoose/tertiary_text |

The Pebble SDK itself (used to build the watchapp) is likewise Apache-2.0:
https://github.com/google/pebble

### Note on LECO 1976

The file's own metadata reads `Copyright (c) 2009 by Samuel Carnoky. All rights
reserved.` with `LECO 1976-Regular is a trademark of Samuel Carnoky.`
(carnoky.com), and carries no embedded license or license URL. It ships inside the
Apache-2.0 PebbleOS repository at `resources/normal/base/ttf/`, outside that
repository's `third_party/` tree and with no license file of its own — so whether
Pebble's blanket Apache-2.0 grant extends to a typeface it originally licensed
commercially is not settled by anything in that repository.

Redistributing the `.otf` may therefore need a license from Carnoky Type. Note
that a built `.pbw` does **not** contain the `.otf`: the SDK rasterizes it to a
bitmap subset of the ASCII range at the two sizes the app draws at. If that
distinction matters for your distribution, it is worth confirming before release.

## License texts

- [`licenses/GPL-3.0.txt`](licenses/GPL-3.0.txt) — Metrolist, PipePipe Extractor
- [`licenses/AGPL-3.0.txt`](licenses/AGPL-3.0.txt) — mirrormsg / watchimage
- [`licenses/Apache-2.0.txt`](licenses/Apache-2.0.txt) — PebbleKit Android 2, Bobby Assistant, Pebble SDK
- [`licenses/MIT-tertiary-text.txt`](licenses/MIT-tertiary-text.txt) — Tertiary Text (© 2013-2015 VGMoose)
- [`licenses/MIT-pixelplayer.txt`](licenses/MIT-pixelplayer.txt) — PixelPlayer (vendored at `39030156`, the last MIT commit; author: Theo Vilardo)
- [`licenses/OFL-1.1-robotoflex.txt`](licenses/OFL-1.1-robotoflex.txt) — Roboto Flex (© 2017 The Roboto Flex Project Authors)

## Project license

This project as a whole is released under **AGPL-3.0-or-later** (see the root
[`LICENSE`](LICENSE)).

Why AGPL rather than GPL: the companion app is derived from Metrolist (GPL-3.0)
**and** links the `watchimage` library (AGPL-3.0). AGPL-3.0 is compatible with
GPL-3.0, but when the two are combined the stricter AGPL terms govern the
combined work — so the project as a whole is licensed AGPL-3.0.

Individual source files that originate from Metrolist or other GPL-3.0 upstreams
keep their original GPL-3.0 headers; that is expected and compatible. Their terms
are unchanged — it is only the *combined* work that is distributed under AGPL-3.0.

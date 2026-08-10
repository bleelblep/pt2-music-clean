# Third-Party Notices

This project (the **dreamwave** Pebble watchapp and its **pebble-companion**
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
| **Bobby Assistant** | Home-screen interaction inspiration (watchapp) | Apache-2.0 | https://github.com/pebble-dev/bobby-assistant |
| **Tertiary Text** | Three-button keyboard concept (watchapp) | MIT | https://github.com/vgmoose/tertiary_text |

The Pebble SDK itself (used to build the watchapp) is likewise Apache-2.0:
https://github.com/google/pebble

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

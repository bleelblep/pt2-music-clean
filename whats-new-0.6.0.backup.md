# What's New — 0.6.0 (archived)

The `WHATS_NEW[]` table as it shipped in 0.6.0, kept verbatim before 0.7.0 replaced it.
That table holds the newest release only — it answers "what changed?", not "what is the
project history" — so each release overwrites the last. This file is the archive.

The living copy is `watchapp/src/c/main.c`, and the full history is `CHANGELOG.md`.

```c
{WnSection, "NEW"},
{WnBullet, "🌗 Default Dark & Default Light"},
{WnBullet, "📖 What's New has its own button"},
{WnSection, "IMPROVED"},
{WnBullet, "⏱ Now Playing counts up, not down"},
{WnBullet, "🧹 Cleaner empty screens"},
{WnBullet, "🔤 LECO headers on every screen"},
{WnBullet, "📏 Scrollbar clear of the rows"},
{WnBullet, "💡 Watch & Bridge explain their stats"},
{WnSection, "SYMFONIUM"},
{WnBullet, "🎶 Symfonium as a source"},
{WnBullet, "🔍 Song, album & artist search"},
{WnBullet, "🔎 Deeper search, ~40 results"},
{WnBullet, "📱 Phone app mirrors & searches it"},
{WnBullet, "⭐ Favorites, all of them"},
{WnSection, "FIXED"},
{WnBullet, "🎯 Picks always open Now Playing"},
{WnBullet, "✋ Search never auto-plays"},
{WnBullet, "🎨 Cover art holds on pause/play"},
{WnBullet, "🖼 Art no longer needs a skip"},
{WnBullet, "💥 Search crashes"},
{WnBullet, "🔗 Long song ids broke playback"},
{WnBullet, "📚 Playlists opened the wrong list"},
{WnBullet, "🔀 Route wedged on Watch"},
```

## Why it was ordered that way

General first, Symfonium last. Nearly everything 0.6.0 touched was the Symfonium source,
and interleaved by NEW/IMPROVED/FIXED it read as though the whole app had been rebuilt
around a backend half the users do not run. Grouped that way, whoever is on YouTube reads
two short sections and stops.

Fixes got their own section rather than trailing the Symfonium features. They used to run
on under that heading with no header of their own, which left "Search crashes" and "Route
wedged on Watch" reading as things the release *added*.

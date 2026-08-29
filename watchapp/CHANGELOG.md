# Watchapp Changelog

## Unreleased

- Now Playing touch moved from long-press to directional targets with a center deadzone.
- Added bottom swipe target to toggle artwork-only mode.
- Added top swipe target behavior split by source:
  - YouTube: toggle output route (phone/watch)
  - Symfonium: play/pause (no output switching under this source)
- Kept directional controls for transport/toggles:
  - Upper-left: previous
  - Upper-right: next
  - Lower-left: shuffle
  - Lower-right: loop
- Touch target geometry now follows the active artwork surface, including full-screen
  artwork-only mode, so overlays and hit areas stay aligned there.

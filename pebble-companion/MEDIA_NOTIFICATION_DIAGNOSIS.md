# Media notification not appearing in system media controls (Android 16)

Status: FIXED (see "Fix implemented" below). Also confirmed by logcat that the
service was being killed and recreated mid-song (PID change ~40s in) - the same
root cause (not a real foreground service).

## Fix implemented
1. Foreground the service inside the watch-message window: `onWatchMessage` calls
   `ensureStartedForPlayback()` for `commandPlay`/`commandResume` BEFORE the slow
   stream resolution in `play()`. Handling a watch message is a valid window to
   start an FGS, so Android 16 permits it; the later background start from inside
   `play()` was being denied.
2. Own the foreground handshake: `onStartCommand` immediately calls
   `promoteToForeground()` -> `startForeground()` with
   `FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK` and a placeholder notification, so the
   5-second startForegroundService contract is always met deterministically
   (instead of relying on Media3's timing, which missed the window on Android 16).
   Media3 then swaps in its session-linked media notification.
3. Notification IDs aligned to Media3's default (1001) for the placeholder, the
   watch-route status, and Media3's own notification, so they replace each other
   (no duplicates) and the system media controls key off the session notification.
4. `leaveForeground()` (stopForeground) is called when playback goes idle so the
   notification does not linger while nothing is playing.
5. MainActivity pre-starts the service, prompts for POST_NOTIFICATIONS, and requests
   the battery-optimization exemption as a fallback. Manifest already declares
   `foregroundServiceType="mediaPlayback"` + FGS/battery permissions.

This fixes BOTH the missing system media notification AND songs stopping mid-play
(the service is now a protected foreground service and is not reclaimed by the OS).

---

## Original investigation (kept for reference)

## Symptom
- The phone recognizes the app is playing (audio focus + media stream volume work).
- The app does NOT appear in the stock system media controls (Quick Settings /
  lock screen media player) that real music apps use.
- Audio itself plays fine on the phone route.

## Root cause
The system media controls on Android 13+ (strictly enforced on 14/15/16) are built
from the platform `MediaSession` PlaybackState/MediaMetadata, surfaced via a
session-linked `MediaStyle` notification. That notification is only posted when the
`MediaSessionService` is a properly *started + foregrounded* service.

In this app:
- `PebblePlaybackService` is created only via `bindService(BIND_AUTO_CREATE)` from
  `PebbleMusicService` (it is a bound service, not a user-launched one).
- Playback is triggered by a watch command (`commandPlay`) that usually arrives
  while the companion UI is closed / app is backgrounded.
- `ensureStartedForPlayback()` calls `startForegroundService()` from that
  background/bound IPC context.
- On Android 12+/16, a background `startForegroundService()` that must then call
  `startForeground()` is restricted for FGS type `mediaPlayback`. A start triggered
  from a bound IPC callback does NOT get a background-FGS exemption on Android 16.
- Result: the OS denies foreground promotion, Media3's internal `startForeground`
  (from `onUpdateNotification`) is effectively blocked / throws
  `ForegroundServiceStartNotAllowedException` (caught/logged), and the
  session-linked media notification is never posted -> nothing shows in the stock
  media UI.

Audio focus and volume keep working because they do not require a foreground
service; only the media-session notification / system media UI does.

## Contributing / secondary issues
- `RouteAwareNotificationProvider` posts the watch-route notification on a different
  channel (`WATCH_STATUS_CHANNEL_ID`) and ID (`1001`) than the media channel/ID
  returned by `getNotificationChannelInfo()` (the DefaultMediaNotificationProvider
  media channel). This channel/ID mismatch mainly affects the watch route but
  muddies which notification is the session-linked "media" one.
- If runtime `POST_NOTIFICATIONS` is revoked, no media notification appears at all
  on Android 13+.

## How to confirm (no code change)
1. `adb logcat | grep -iE "ForegroundServiceStartNotAllowed|startForeground|PT2Music|MediaSession"`
   right when pressing play. Expect a ForegroundServiceStartNotAllowedException or a
   "did not request foreground" warning.
2. Play a track while the companion MainActivity is OPEN in the foreground. If the
   media UI appears then but not when backgrounded, that confirms the Android 16
   background-FGS-start restriction.
3. Verify `POST_NOTIFICATIONS` is granted (Settings > Apps > dreamwave >
   Notifications).

## Fix direction (for later)
- Let Media3's own foreground handling drive the notification via playback state,
  rather than a manual background `startForegroundService()`.
- Ensure the service becomes a foreground `MediaSessionService` in a way Android 16
  permits (e.g. a valid FGS start reason for media playback, or keeping the service
  started before playback in an allowed context).
- Ensure `POST_NOTIFICATIONS` is granted at runtime.
- Align the notification channel/ID so the session-linked media notification is the
  one posted on the phone route.

## Follow-up implemented (long-press seek)
Finalized behavior (mirrors typical media players):
- Long-press PREVIOUS: if more than 5s into the song, restart it (seek to 0);
  if within the first 5s, skip to the previous track.
- Long-press NEXT: skip straight to the next track.
Note: restart-to-0 only works on the phone route (watch route streams sequential
PCM and cannot reposition), so on the watch route PREVIOUS always skips tracks.

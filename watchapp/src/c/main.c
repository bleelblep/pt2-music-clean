#include <pebble.h>

#define MAX_RESULTS 10
// Library screens can hold many more entries than a search page. The shared
// s_results array is sized to the larger of the two so both features fit.
#define MAX_LIBRARY 60
#define MAX_ENTRIES (MAX_LIBRARY > MAX_RESULTS ? MAX_LIBRARY : MAX_RESULTS)
#define TEXT_LENGTH 81
#define THEME_KEY 1
#define WATCH_VOLUME_KEY 2
#define PHONE_VOLUME_KEY 3
#define AUDIO_ROUTE_KEY 4
#define INPUT_MODE_KEY 6
#define HISTORY_LIMIT_KEY 7
#define ALT_HOME_KEY 8
#define SEARCH_LIMIT_KEY 9
#define RECENT_SEARCH_LIMIT_KEY 10
#define EXTRA_LIBRARY_KEY 11
#define KEYBOARD_STYLE_KEY 12
#define SHOW_PROGRESS_KEY 13
#define CACHE_ENABLED_KEY 14
#define CACHE_SIZE_MB_KEY 15
#define SHOW_HOME_QUOTES_KEY 16
#define SHUFFLE_ENABLED_KEY 17
#define LOOP_MODE_KEY 18
#define COVER_ART_BG_KEY 19
#define WATCH_AUDIO_QUALITY_KEY 20
#define PHONE_AUDIO_QUALITY_KEY 21
#define CACHE_RADIO_KEY 22
// 23 was the per-screen Queue style flag and 26 the per-screen Home layout flag; both
// were folded into the single app-wide BESPOKE_UI_KEY below and are no longer read.
#define ADVANCED_UNLOCKED_KEY 24
#define LEGACY_THEME_KEY 25  // Reserved/unused - see s_legacy_theme's comment.
#define BESPOKE_UI_KEY 27
#define ROUTE_EPOCH_KEY 28
#define MSG_CONFIG_SHOW_PROGRESS 27
#define MSG_CACHE_ENABLED 28
#define MSG_CACHE_SIZE_MB 29
#define MSG_CONFIG_CACHE_ENABLED 30
#define MSG_CONFIG_CACHE_SIZE_MB 31
#define MSG_SHUFFLE_ENABLED 32
#define MSG_LOOP_MODE 33
#define MSG_CONFIG_COVER_ART_BG 34
#define MSG_IMAGE_DATA 35
#define MSG_IMAGE_WIDTH 36
#define MSG_IMAGE_HEIGHT 37
#define MSG_IMAGE_SEQUENCE 38
#define MSG_IMAGE_TOTAL_BYTES 39
#define MSG_THEME 40
#define MSG_CONFIG_WATCH_AUDIO_QUALITY 41
#define MSG_CONFIG_PHONE_AUDIO_QUALITY 42
#define MSG_SEARCH_MODE 43
#define MSG_CONFIG_CACHE_RADIO 44
// Monotonic audio-route epoch, shared with the companion: whichever side changes the
// route bumps it, and a route arriving with an older epoch is a stale echo from before
// a newer local change and is ignored. Keeps a snapshot that was in flight during a
// route toggle from reverting the toggle.
#define MSG_ROUTE_EPOCH 45
// Recently Played count cycles through these values (5 > 10 > 15 > 20).
#define HISTORY_LIMIT_MIN 5
#define HISTORY_LIMIT_MAX 20
#define HISTORY_LIMIT_STEP 5
// Recent Searches display count (default 10). Increasable like Recently Played;
// the UI control is intentionally not wired up yet.
#define RECENT_SEARCH_LIMIT_MIN 5
#define RECENT_SEARCH_LIMIT_MAX 20
#define RECENT_SEARCH_LIMIT_STEP 5
#define RECENT_SEARCH_LIMIT_DEFAULT 10
#define ADPCM_HEADER_SIZE 4
#define ADPCM_BLOCK_SIZE 512
#define ADPCM_SAMPLES_PER_BLOCK 1017
#define MENU_HEADER_HEIGHT 31
#define MENU_ROW_HEIGHT 44
#define MENU_RESULT_ROW_HEIGHT 40
#define PROTOCOL_VERSION 4
#define CAPABILITY_STATE_SNAPSHOT 1
#define CAPABILITY_SEARCH_REQUEST_ID 2

// The companion picks the cover-art size at runtime by audio route: a small 64x64 while
// audio streams to the watch over BLE (so the transfer does not contend with the audio
// stream and fail), and a sharp 144x144 when audio is on the phone and the link is free.
// The watch accepts either square size, records the received dimensions in
// s_cover_art_w/h, and sizes the (heap) buffer to exactly what the transfer declared.
#define COVER_ART_LOW_DIM 64
#define COVER_ART_HIGH_DIM 144
// Silence tolerated between cover-art chunks before the transfer is written off. The
// companion retries a failed chunk 5x at 100 ms and then restarts the whole payload
// after 400 ms, so anything past ~2 s means it has stopped sending for good.
#define COVER_ART_STALL_MS 2500

typedef enum {
  ScreenHome,
  ScreenLibrary,
  ScreenLibraryItems,
  ScreenMenu,
  ScreenSettings,
  ScreenAdvanced,
  ScreenAbout,
  ScreenInputChoice,
  ScreenSearchType,
  ScreenKeyboard,
  ScreenPlaceholder,
  ScreenSearching,
  ScreenResults,
  ScreenBuffering,
  ScreenPlaying,
  ScreenPaused,
  ScreenError,
  ScreenQueue,
} AppScreen;

typedef enum {
  ThemeTeal = 0,
  ThemePurple = 1,
  ThemeSunset = 2,
  ThemeDefault = 3,
  ThemeMono = 4,   // Black & white theme.
} AppTheme;

typedef enum {
  InputVoice,
  InputKeyboard,
  InputAsk,
} InputMode;

typedef enum {
  SearchModeSong,
  SearchModeArtist,
  SearchModeSongRadio,
} SearchMode;

enum {
  CommandHello = 1,
  CommandSearch = 2,
  CommandPlay = 3,
  CommandStop = 4,
  CommandPause = 5,
  CommandResume = 6,
  CommandToggleLoop = 7,
  CommandSetAudioRoute = 8,
  CommandSetVolume = 9,
  CommandRequestState = 10,
  CommandRequestLibrary = 11,
  CommandToggleFavorite = 12,
  CommandSeek = 13,
  CommandSyncSettings = 14,
  CommandToggleShuffle = 15,
  CommandPrevious = 16,
  CommandNext = 17,
  CommandDeleteCached = 18,
  CommandRequestQueue = 19,
  CommandQueueJump = 20,
  EventReady = 100,
  EventSearchResult = 101,
  EventAudioStart = 102,
  EventAudioChunk = 103,
  EventAudioEnd = 104,
  EventError = 105,
  EventSearchComplete = 106,
  EventPaused = 107,
  EventPlaybackInfo = 108,
  EventPlaybackPosition = 109,
  EventStateSnapshot = 110,
  EventLibraryItem = 111,
  EventLibraryComplete = 112,
  EventCoverArtStart = 113,
  EventCoverArtChunk = 114,
  EventCoverArtClear = 115,
  EventFavoriteState = 116,
  EventQueueItem = 117,
  EventQueueComplete = 118,
};

enum {
  LibraryRecent = 0,
  LibraryCached = 1,
  LibraryFavorites = 2,
  LibraryContinue = 3,
  LibraryRecentSearches = 4,
  // Phone-owned playlists; the watch lists and plays them but never edits them.
  LibraryPlaylists = 5,
};

enum {
  PlaybackIdle = 0,
  PlaybackBuffering = 1,
  PlaybackPlaying = 2,
  PlaybackPaused = 3,
};

enum {
  LoopModeOff = 0,
  LoopModeOne = 1,
  LoopModeAll = 2,
};

typedef struct {
  char video_id[TEXT_LENGTH];
  char title[TEXT_LENGTH];
  char artist[TEXT_LENGTH];
} SearchResult;

typedef struct {
  GColor background;
  GColor foreground;
  GColor accent;
  GColor secondary;
  GColor surface;
  // Resting (unpressed) colors for the action-bar rail (Home / Now Playing /
  // keyboard sidebar). Normally a white strip with black icons regardless of
  // accent; the Mono theme inverts this to black with white icons to save
  // power on OLED-style panels.
  GColor action_bar_bg;
  GColor action_bar_icon;
  // Colors used for the momentarily-pressed action-bar segment. Legacy themes
  // highlight with the theme accent and a white icon; Mono inverts the rail's
  // own resting colors so the press feedback stays visible against a black bar.
  GColor action_bar_press_bg;
  GColor action_bar_press_icon;
} ThemeColors;

static Window *s_window;
static Window *s_overlay_window;
static Layer *s_canvas;
static Layer *s_root_canvas;
static Layer *s_overlay_canvas;
static bool s_overlay_visible;
static MenuLayer *s_native_menu_layer;
static GBitmap *s_home_background;
// Tracks which home background bitmap is currently loaded, as a variant index:
//   0 = Unicorn, 1 = Kiwi, 2 = Mono Unicorn, 3 = Mono Kiwi.
// Only one full-size background is kept in memory at a time because two
// 200x228 8-bit bitmaps do not both fit in the app heap.
static int8_t s_home_background_variant = -1;
// Variant whose background failed to load (-1 none, otherwise a variant index
// as above). Stops a failed swap from thrashing destroy/reload cycles on every
// frame; cleared when the user re-enters Home or toggles the style so the
// swap is retried.
static int8_t s_home_bg_failed_variant = -1;
static GBitmap *s_mascot_modern;
static GBitmap *s_mascot_dreamhouse;
static GBitmap *s_mascot_ticket;
// Corner mascot for the pink "Unicorn" home variant (flat pink fill instead of a
// full-screen background image). Only loaded while that variant is active.
static GBitmap *s_mascot_home;
static DictationSession *s_dictation;
static AppScreen s_screen = ScreenHome;
static AppTheme s_theme = ThemeDefault;
static bool s_alt_home;
// When enabled, the Library menu also lists the extra sections: Favorites,
// Continue, and Recent Searches.
static bool s_extra_library;
// When disabled, the Home banner box (rotating quotes) is hidden entirely,
// leaving just the background art. Useful for a cleaner/quieter Home screen.
static bool s_show_home_quotes = true;
static uint8_t s_history_limit = HISTORY_LIMIT_MIN;
static uint8_t s_search_limit = 5;   // Search result count: toggles between 5 and 10.
static uint8_t s_recent_search_limit = RECENT_SEARCH_LIMIT_DEFAULT;  // Recent Searches display count.
// Heap-allocated in init() (see the call site). ~14.6 KB of rows: as a static array
// this counts against the 64 KB app virtual-size limit (a uint16 field in the app
// metadata), which the app was already within ~60 bytes of; heap does not.
static SearchResult *s_results;
static SearchResult s_now_playing;
static bool s_has_now_playing;
static int s_result_count;
static int s_selected_result;
static bool s_bridge_ready;
// Last applied audio-route epoch; bumped on every locally initiated route change.
// Persisted so a restart cannot roll the route back to a pre-change value.
static int32_t s_route_epoch;
// True once a state snapshot has been applied. Snapshots never choose the screen (see
// EventStateSnapshot) - this only records that the companion's state has landed at
// least once, so nothing reads a still-empty playback state as authoritative.
static bool s_snapshot_applied;
static bool s_stream_open;
static int32_t s_expected_sequence;
static uint8_t s_watch_volume = 50;
static uint8_t s_phone_volume = 50;
static int16_t s_pcm_buffer[ADPCM_SAMPLES_PER_BLOCK];
static uint32_t s_duration_seconds;
static uint32_t s_elapsed_seconds;
static uint32_t s_played_samples;
static AppTimer *s_progress_timer;
static AppTimer *s_volume_timer;
static AppTimer *s_animation_timer;
static AppTimer *s_action_bar_timer;
static AppTimer *s_handshake_timer;
static bool s_show_volume;
static int s_animation_frame;
static ButtonId s_pressed_button;
static bool s_button_pressed;
static bool s_action_bar_visible;
// When true on the now-playing screen, all UI (text/controls/action bar) is hidden
// and only the full-screen cover art is drawn. Toggled by a touchscreen long-press;
// only meaningful when cover art is available. Reset on leaving the now-playing screen.
static bool s_artwork_only;
// Which page of the playing-screen action bar is showing:
//   0 = search / loop        (SELECT advances to page 1)
//   1 = previous / next      (SELECT advances to page 2)
//   2 = volume / play-pause  (SELECT toggles play/pause)
static int s_action_bar_page;
// "More" popup on the now-playing screen. The playback controls were flattened so the
// hardware buttons map directly (SELECT = play/pause, UP/DOWN = volume, long UP/DOWN =
// prev/next). The less-frequent actions (shuffle, loop, favorite, output, new search,
// queue) live in this small button-driven list, opened with a SELECT long-press: UP/DOWN
// move the highlight, SELECT activates, BACK closes.
static bool s_np_more_open;
static int s_np_more_selection;
#define NP_MORE_COUNT 6
static bool s_loop_enabled;
static uint8_t s_loop_mode;
static bool s_shuffle_enabled;
static bool s_current_favorite;   // Whether the currently loaded track is favorited.
static bool s_phone_audio;
static bool s_show_progress = true;
static bool s_cache_enabled = true;
static uint16_t s_cache_size_mb = 250;
static bool s_cache_radio = true;
// Defaults on: album art is the point of the now-playing screen, and the companion
// ships the same default so a watch that has never pushed its settings still gets art.
static bool s_cover_art_background = true;
// Both default to the "highest quality" tier so anyone who never touches this
// setting keeps today's always-highest-bitrate behavior.
static bool s_watch_audio_quality = true;   // false = Efficient, true = Balanced
static bool s_phone_audio_quality = true;   // false = Data Saver, true = High
static bool s_cover_art_ready;
static bool s_cover_art_receiving;
// Dimensions of the cover currently held/being received (square: 64 or 144).
static int s_cover_art_w;
static int s_cover_art_h;
static bool s_cover_art_color;
static bool s_cover_art_dark;
static int s_cover_art_expected_sequence;
static int s_cover_art_expected_bytes;
static int s_cover_art_received_bytes;
// Track the in-progress transfer belongs to, taken from the Start message. Cover art
// is matched on *which track it is for* rather than on the stream generation: the
// generation counter is bumped locally by next/previous/resume/route-toggle/back, and
// a transfer that was in flight when that happened used to be discarded silently (the
// companion would report every chunk delivered while the watch logged no completions).
static char s_cover_art_video_id[TEXT_LENGTH];
// Cleans up a transfer the companion started and then abandoned (a chunk it could not
// deliver after its retries). Without this the watch sits with receiving=true holding a
// part-filled heap buffer until some later Start happens to reclaim it.
static AppTimer *s_cover_art_timeout;
// Heap-allocated (not a static array): at 144x144 the buffer is ~20 KB, and the pbw
// header encodes the app's static RAM footprint in a uint16 (max 65535 bytes), so a
// static buffer this large overflows it. Allocated on demand while a cover is being
// received/shown and freed by clear_cover_art().
static uint8_t *s_cover_art_data;

#define ACTION_BAR_PAGES 3

// Transient on-screen feedback shown after a long-press action (favorite toggle
// or output switch). Rendered as a large icon centered on the playing screen,
// styled like the action-bar glyphs, then fades out on a short timer.
typedef enum {
  FeedbackNone,
  FeedbackFavoriteOn,
  FeedbackFavoriteOff,
  FeedbackOutputPhone,
  FeedbackOutputWatch,
  FeedbackShuffleOn,
  FeedbackShuffleOff,
  FeedbackKeyboardHint,
  // Flashed on the now-playing screen to confirm which control a button/gesture triggered.
  FeedbackPlay,
  FeedbackPause,
  FeedbackNext,
  FeedbackPrev,
} FeedbackIcon;
static FeedbackIcon s_feedback_icon;
static AppTimer *s_feedback_timer;
static int32_t s_stream_generation;
static bool s_search_active;
static int32_t s_search_request_id;
static int32_t s_companion_capabilities;
static int s_menu_selection;
static int s_library_type;
static bool s_library_loading;
static bool s_queue_loading;
// ScreenPlaying/ScreenPaused are transient nav-history screens (see
// screen_is_transient()), so nav_push(ScreenQueue) never records a way back to them.
// Queue's own Back handling restores this directly instead of falling through to
// nav_back(), which would otherwise pop all the way to whatever screen was open
// before playback started (e.g. Library or Results).
static AppScreen s_queue_return_screen = ScreenPlaying;
// Default (false): every list screen is the stock style (native MenuLayer, accent
// header band). When on, the whole app switches to the bespoke chromeless language -
// white ground, dark-gray eyebrow, rounded accent selection, footer hint band - across
// Home, Menu, Library, song lists, Search type, Settings, Advanced, About and Queue.
// See screen_uses_native_menu() and the bespoke_* helpers.
static bool s_bespoke_ui;
// Home's own selection. Bespoke Home is a real menu (UP/DOWN move, SELECT opens)
// rather than three fixed button shortcuts, and it must not disturb s_menu_selection.
static int s_home_selection;
// Whether audio is actually playing, as opposed to a track merely being loaded.
// Playing-vs-paused otherwise lives only in s_screen, which is gone the moment you
// navigate away, so Home could not tell the two apart - see draw_home_card().
static bool s_playback_active;
// Selecting the VERSION row on About 7 times toggles s_advanced_unlocked, which
// reveals the rest of Advanced beyond Keyboard (see advanced_item_count()).
// s_about_select_count is transient (reset on each fresh visit to About, not
// persisted); the unlock state is.
static bool s_advanced_unlocked;
// Reserved for a possible legacy Dreamhouse/Ticket mascot theme - built but disabled
// per product direction (Advanced hides non-essential settings behind the About
// unlock instead); see the commented-out branches in current_status_mascot().
static uint8_t s_legacy_theme;
static int s_about_select_count;
static AppScreen s_placeholder_parent;
static char s_placeholder_title[TEXT_LENGTH];
static char s_placeholder_message[TEXT_LENGTH];
static char s_time_text[6];
static bool s_ignore_menu_repeat;
static bool s_keyboard_pt2;
#ifdef PBL_PLATFORM_EMERY
static bool s_touch_subscribed;
static bool s_touch_active;
static int8_t s_touch_origin_key = -1;
static int8_t s_touch_active_key = -1;
// Raw touch-down coordinates for the current grid gesture, used to measure
// swipe direction rather than requiring the finger to land inside a cell.
static int16_t s_touch_start_x;
static int16_t s_touch_start_y;
// Which on-screen badge is currently pressed, if any.
typedef enum {
  Pt2BadgeNone,
  Pt2BadgeHelp,
} Pt2Badge;
static Pt2Badge s_touch_badge = Pt2BadgeNone;
// Now-playing touchscreen long-press detection (there is no long-press touch event,
// so we time a stationary hold ourselves).
static AppTimer *s_np_hold_timer = NULL;
static bool s_np_touching;
// Set once the stationary-hold timer fires (artwork-only toggled), so the following
// touch liftoff is not also treated as a tap on the playback controls.
static bool s_np_hold_fired;
static int16_t s_np_touch_x;
static int16_t s_np_touch_y;
#endif

static void select_click(ClickRecognizerRef recognizer, void *context);
static void click_config_provider(void *context);
// Now-playing actions - shared by buttons, touch gestures, and the More popup. Defined
// alongside the click handlers but forward-declared here so np_touch_handle can call them.
static void np_toggle_play_pause(void);
static void np_next(void);
static void np_previous(void);
static void np_volume_up(void);
static void np_volume_down(void);
static void np_cycle_loop(void);
static void np_toggle_shuffle(void);
static void np_toggle_output(void);
static int settings_item_count(void);
static int advanced_item_count(void);
static int library_item_count(void);
static const char *library_items_title(void);
static bool screen_uses_native_menu(AppScreen screen);
static bool screen_uses_overlay_window(AppScreen screen);
static void sync_overlay_window(bool animated);
static void sync_native_menu(bool animated);
static void sync_touch_service(void);
static void draw_feedback_overlay(GContext *ctx);
static void draw_note_icon(GContext *ctx, GPoint c, GColor color);
static void draw_person_icon(GContext *ctx, GPoint c, GColor color);
static void draw_broadcast_icon(GContext *ctx, GPoint c, GColor color);
static void draw_search_icon(GContext *ctx, GPoint c, GColor color);
static void draw_vinyl_icon(GContext *ctx, GPoint c, GColor color);
static void draw_sliders_icon(GContext *ctx, GPoint c, GColor color);
static void draw_info_icon(GContext *ctx, GPoint c, GColor color);

static void clear_cover_art(void);

static void cancel_cover_art_timeout(void) {
  if (s_cover_art_timeout) {
    app_timer_cancel(s_cover_art_timeout);
    s_cover_art_timeout = NULL;
  }
}

static void clear_cover_art(void) {
  cancel_cover_art_timeout();
  s_cover_art_ready = false;
  s_cover_art_receiving = false;
  s_cover_art_w = 0;
  s_cover_art_h = 0;
  s_cover_art_color = false;
  s_cover_art_dark = false;
  s_cover_art_expected_sequence = 0;
  s_cover_art_expected_bytes = 0;
  s_cover_art_received_bytes = 0;
  s_cover_art_video_id[0] = '\0';
  if (s_cover_art_data) {
    free(s_cover_art_data);
    s_cover_art_data = NULL;
  }
}

// A transfer that stops mid-stream: the companion gave up on a chunk, or the link
// dropped. Release the buffer rather than holding a useless partial image (and the
// receiving flag, which would make the retry's chunks land on stale state).
static void cover_art_timeout_cb(void *context) {
  (void) context;
  s_cover_art_timeout = NULL;
  if (!s_cover_art_receiving) return;
  APP_LOG(APP_LOG_LEVEL_WARNING,
          "[CoverArt] transfer stalled at %d/%d bytes; discarding",
          s_cover_art_received_bytes, s_cover_art_expected_bytes);
  clear_cover_art();
}

// (Re)arms the stall watchdog. Called on Start and on every accepted chunk, so the
// window is "silence since the last chunk", not "total transfer time" - a slow but
// progressing transfer is never cut off.
static void arm_cover_art_timeout(void) {
  cancel_cover_art_timeout();
  s_cover_art_timeout = app_timer_register(COVER_ART_STALL_MS, cover_art_timeout_cb, NULL);
}

// Averages the just-received cover art's luma (same weighting the phone-side encoder
// uses for its mono threshold) so the Now Playing screen can pick white or black UI
// elements that won't blend into the artwork. Runs once per cover art transfer, not
// per frame.
static void update_cover_art_brightness(void) {
  if (!s_cover_art_data) return;
  const int bytes_per_row = s_cover_art_w / 8;
  uint32_t luma_total = 0;
  uint32_t pixel_count = (uint32_t) s_cover_art_w * s_cover_art_h;
  if (pixel_count == 0) return;
  if (s_cover_art_color) {
    for (uint32_t i = 0; i < pixel_count; i++) {
      uint8_t px = s_cover_art_data[i];
      uint8_t r = (px >> 4) & 0x03;
      uint8_t g = (px >> 2) & 0x03;
      uint8_t b = px & 0x03;
      luma_total += 3 * r + 6 * g + b;
    }
    // Max per-pixel luma is 3*3 + 6*3 + 3 = 30; call it dark below the midpoint.
    s_cover_art_dark = (luma_total / pixel_count) <= 15;
  } else {
    uint32_t black_pixels = 0;
    for (int y = 0; y < s_cover_art_h; y++) {
      for (int bx = 0; bx < bytes_per_row; bx++) {
        uint8_t packed = s_cover_art_data[y * bytes_per_row + bx];
        for (int bit = 0; bit < 8; bit++) {
          if ((packed >> (7 - bit)) & 0x01) black_pixels++;
        }
      }
    }
    s_cover_art_dark = black_pixels * 2 >= pixel_count;
  }
}

// Scales the cover into 'bounds' by writing straight to the 8-bit framebuffer (one byte
// per pixel). This is far cheaper than a graphics_fill_rect per source pixel - at 144x144
// that was ~20k calls per frame, slow enough to visibly repaint on every redraw.
static void draw_cover_art_background(GContext *ctx, GRect bounds) {
  if (!s_cover_art_background || !s_cover_art_ready || !s_cover_art_data) return;
  const int w = s_cover_art_w;
  const int h = s_cover_art_h;
  const int bytes_per_row = w / 8;
  const int bw = bounds.size.w;
  const int bh = bounds.size.h;
  if (w <= 0 || h <= 0 || bw <= 0 || bh <= 0) return;

  // Precompute the source column for each destination column so the inner loop is a
  // plain lookup + byte write.
  int sx_map[200];
  int cols = bw > 200 ? 200 : bw;
  for (int dx = 0; dx < cols; dx++) sx_map[dx] = dx * w / bw;

  const uint8_t black = GColorBlack.argb;
  const uint8_t white = GColorWhite.argb;

  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  for (int dy = 0; dy < bh; dy++) {
    int py = bounds.origin.y + dy;
    if (py < 0 || py >= 228) continue;
    GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, py);
    int sy = dy * h / bh;
    const uint8_t *src_row = s_cover_art_data + (s_cover_art_color ? sy * w : sy * bytes_per_row);
    for (int dx = 0; dx < cols; dx++) {
      int px = bounds.origin.x + dx;
      if (px < row.min_x || px > row.max_x) continue;
      int sx = sx_map[dx];
      uint8_t color;
      if (s_cover_art_color) {
        color = src_row[sx];
      } else {
        color = ((src_row[sx >> 3] >> (7 - (sx & 7))) & 0x01) ? black : white;
      }
      row.data[px] = color;
    }
  }
  graphics_release_frame_buffer(ctx, fb);
}

// Navigation history. Forward navigation pushes the screen being left (with the
// context needed to restore it); the Back button pops and restores. Transient
// screens (Searching/Buffering/Error/Placeholder) are never pushed so Back skips
// straight past them to the last real screen the user was on.
typedef struct {
  AppScreen screen;
  int menu_selection;
  int library_type;
  int selected_result;
} NavEntry;

#define NAV_STACK_MAX 12
static NavEntry s_nav_stack[NAV_STACK_MAX];
static int s_nav_depth;

// Shared smooth-scroll state used by every scrollable screen (About text plus all
// the selection lists). s_scroll is the pixel offset currently rendered and eases
// toward s_scroll_target each animation frame; s_scroll_max is recomputed by the
// active screen's draw routine from its content height.
static int16_t s_scroll;
static int16_t s_scroll_target;
static int16_t s_scroll_max;
static AppTimer *s_scroll_timer;
static InputMode s_input_mode = InputVoice;
static SearchMode s_search_mode = SearchModeSong;
static uint8_t s_keyboard_mode;
static uint8_t s_keyboard_start;
static uint8_t s_keyboard_size = 27;
static uint8_t s_query_length;

static const char KEYBOARD_LOWER[] = "abcdefghijklm nopqrstuvwxyz";
static const char KEYBOARD_UPPER[] = "ABCDEFGHIJKLM NOPQRSTUVWXYZ";
static const char KEYBOARD_SYMBOLS[] = "1234567890!?-'\"$()&*+#:@/,.";

static const char *const PT2_LETTERS_TAP[9] = {
  "a", "b", "c", "d", " ", "e", "f", "g", "h",
};
static const char *const PT2_LETTERS_LABEL[9] = {
  "abc", "def", "ghi", "jkl", "m  n", "opq", "rst", "uvw", "xyz",
};
static const char *const PT2_NUMBERS_TAP[9] = {
  "1", "2", "3", "4", "5", "6", "7", "8", "9",
};
static const char *const PT2_NUMBERS_LABEL[9] = {
  "1", "2", "3", "4", "5", "6", "7", "8", "9",
};
// The bottom-center "8" cell doubles as the 0 key. Like the letter cells, it fans on
// touch: swipe up-left keeps 8 (onto the "1" key), swipe up-right picks 0 (onto the
// "3" key); a plain tap types 8. This is how the grid keyboard reaches 0 without a
// tenth cell (entry is touch-only).
#define PT2_ZERO_CELL 7

#ifdef PBL_PLATFORM_EMERY
typedef struct {
  char character;
  int8_t target;
} Pt2TouchChoice;

static const Pt2TouchChoice PT2_TOUCH_MAP[9][3] = {
  {{'a', 6}, {'b', 8}, {'c', 2}},
  {{'d', 6}, {'e', 7}, {'f', 8}},
  {{'g', 8}, {'h', 6}, {'i', 0}},
  {{'j', 2}, {'k', 5}, {'l', 8}},
  {{'m', 3}, {' ', 4}, {'n', 5}},
  {{'o', 0}, {'p', 3}, {'q', 6}},
  {{'r', 0}, {'s', 2}, {'t', 8}},
  {{'u', 0}, {'v', 1}, {'w', 2}},
  {{'x', 2}, {'y', 0}, {'z', 6}},
};
#endif

static const int8_t ADPCM_INDEX_TABLE[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
static const int16_t ADPCM_STEP_TABLE[89] = {
  7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
  34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
  157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
  598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878,
  2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
  6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
  18500, 20350, 22385, 24623, 27086, 29794, 32767,
};
static char s_query[TEXT_LENGTH];
static char s_status[TEXT_LENGTH] = "Connecting to phone";

static const char *const HOME_QUOTES[] = {
  "Unbridled stoke",
  "Mane event: catch a wave",
  "Hold your horses, let's surf",
  "Life's a beach, it's fantastic",
  "Come on unicorn, let's go surfing",
  "Hoofin' it, it's electric",
  "Brush my mane, comb the tide",
  "Sparkle horn, catch the swell",
  "Neigh worries, just vibes",
  "Pony up for paradise",
  "One horn, endless swell",
  "Trot to the tunes",
  "Magical waves, mythical waves",
  "Surf's up, unicorn's out",
  "Dream in pink, ride in teal",
};
// Alternate quote set shown when the hidden "Alt" home style is enabled.
static const char *const HOME_QUOTES_ALT[] = {
  "How Beak-zarre",
  "Don't Preen It's Over",
  "Don't Forget Your Roost",
  "Why does surf do this to me?",
  "And we'll never be flying",
  "Don't come and go swim my way",
  "How many birds surf like this",
  "Friday surf is the scene tune your frequencies",
  "Kiss me till the tide goes out",
};
static int s_home_quote;

static const char *const *active_home_quotes(void) {
  return s_alt_home ? HOME_QUOTES_ALT : HOME_QUOTES;
}

static int active_home_quote_count(void) {
  return s_alt_home ? (int) ARRAY_LENGTH(HOME_QUOTES_ALT) : (int) ARRAY_LENGTH(HOME_QUOTES);
}

static void shuffle_home_quote(void) {
  int count = active_home_quote_count();
  if (count <= 1) { s_home_quote = 0; return; }
  int next = rand() % (count - 1);
  if (next >= s_home_quote) next++;  // Skip the current index to avoid an immediate repeat.
  s_home_quote = next;
}

static ThemeColors colors(void) {
  if (s_theme == ThemePurple) {
    return (ThemeColors) {
      .background = GColorWhite,
      .foreground = GColorBlack,
      .accent = GColorPurpureus,
      .secondary = GColorDarkGray,
      .surface = GColorLightGray,
      .action_bar_bg = GColorWhite,
      .action_bar_icon = GColorBlack,
      .action_bar_press_bg = GColorPurpureus,
      .action_bar_press_icon = GColorWhite,
    };
  }
  if (s_theme == ThemeSunset) {
    return (ThemeColors) {
      .background = GColorWhite,
      .foreground = GColorBlack,
      .accent = GColorSunsetOrange,
      .secondary = GColorDarkGray,
      .surface = GColorLightGray,
      .action_bar_bg = GColorWhite,
      .action_bar_icon = GColorBlack,
      .action_bar_press_bg = GColorSunsetOrange,
      .action_bar_press_icon = GColorWhite,
    };
  }
  if (s_theme == ThemeDefault) {
    return (ThemeColors) {
      .background = GColorWhite,
      .foreground = GColorBlack,
      .accent = GColorFashionMagenta,
      .secondary = GColorDarkGray,
      .surface = GColorLightGray,
      .action_bar_bg = GColorWhite,
      .action_bar_icon = GColorBlack,
      .action_bar_press_bg = GColorFashionMagenta,
      .action_bar_press_icon = GColorWhite,
    };
  }
  if (s_theme == ThemeMono) {
    // Pure black & white: white canvas/background everywhere (including Home),
    // but the action bar rail is inverted to black with white icons; no color
    // accent anywhere.
    return (ThemeColors) {
      .background = GColorWhite,
      .foreground = GColorBlack,
      .accent = GColorBlack,
      .secondary = GColorDarkGray,
      .surface = GColorLightGray,
      .action_bar_bg = GColorBlack,
      .action_bar_icon = GColorWhite,
      .action_bar_press_bg = GColorWhite,
      .action_bar_press_icon = GColorBlack,
    };
  }
  return (ThemeColors) {
    .background = GColorWhite,
    .foreground = GColorBlack,
    .accent = GColorTiffanyBlue,
    .secondary = GColorDarkGray,
    .surface = GColorLightGray,
    .action_bar_bg = GColorWhite,
    .action_bar_icon = GColorBlack,
    .action_bar_press_bg = GColorTiffanyBlue,
    .action_bar_press_icon = GColorWhite,
  };
}

static GColor accent_color(void) {
  return colors().accent;
}

static const char *theme_name(void) {
  if (s_theme == ThemePurple) return "Electric Purple";
  if (s_theme == ThemeSunset) return "Sunset";
  if (s_theme == ThemeTeal) return "Dreamwave Teal";
  if (s_theme == ThemeMono) return "Mono";
  return "Default";
}

static void animation_callback(void *context) {
  s_animation_timer = NULL;
  if (s_screen != ScreenSearching && s_screen != ScreenBuffering) return;
  s_animation_frame = (s_animation_frame + 1) % 12;
  layer_mark_dirty(s_canvas);
  s_animation_timer = app_timer_register(180, animation_callback, NULL);
}

static void scroll_callback(void *context) {
  s_scroll_timer = NULL;
  int delta = s_scroll_target - s_scroll;
  if (delta == 0) return;
  // Ease toward the target: move a fraction of the remaining distance each frame.
  int step = delta / 3;
  if (step == 0) step = delta > 0 ? 1 : -1;
  s_scroll += step;
  layer_mark_dirty(s_canvas);
  if (s_scroll != s_scroll_target) {
    s_scroll_timer = app_timer_register(16, scroll_callback, NULL);
  }
}

// Requests a smooth scroll to the given pixel offset. The target is clamped to
// [0, s_scroll_max]; callers that adjust s_scroll_max mid-frame should re-clamp.
static void scroll_to(int16_t target) {
  if (target < 0) target = 0;
  s_scroll_target = target;
  if (!s_scroll_timer) {
    s_scroll_timer = app_timer_register(16, scroll_callback, NULL);
  }
}

// Snaps the scroll position instantly (no animation), e.g. when switching screens
// or resetting a list. Cancels any in-flight animation.
static void scroll_reset(void) {
  if (s_scroll_timer) {
    app_timer_cancel(s_scroll_timer);
    s_scroll_timer = NULL;
  }
  s_scroll = 0;
  s_scroll_target = 0;
  s_scroll_max = 0;
}

static void set_screen(AppScreen screen) {
  AppScreen previous_screen = s_screen;
  bool changed = screen != s_screen;
  // Hand the stock Home artwork's heap back the moment we leave Home, and let
  // draw_home()'s ensure_home_background() reload it on the way in.
  //
  // The full-screen 200x228 background is an 8-bit bitmap: 45600 bytes out of a 77704
  // byte heap. That left ~32 KB for everything else, and a 144x144 colour cover needs
  // 20736 of them *contiguous* - on top of the MenuLayer, the overlay window and the
  // mascot bitmaps. So the cover malloc failed and album art silently never appeared,
  // but only on the stock UI: bespoke Home draws no artwork and already frees this
  // bitmap, which is exactly why switching to bespoke made album art start working.
  if (changed && screen != ScreenHome && s_home_background) {
    gbitmap_destroy(s_home_background);
    s_home_background = NULL;
    s_home_background_variant = -1;
  }
  if (s_animation_timer) {
    app_timer_cancel(s_animation_timer);
    s_animation_timer = NULL;
  }
  // Every screen change starts at the top with no in-flight scroll animation.
  if (changed) scroll_reset();
  // Latch playing-vs-paused while we are on a playback screen. Leaving for another
  // screen deliberately does not clear it: the track keeps playing, and Home needs to
  // know that. clear_playing_track() is what ends it.
  if (screen == ScreenPlaying) {
    s_playback_active = true;
  } else if (screen == ScreenPaused) {
    s_playback_active = false;
  }
  if (screen != ScreenPlaying && screen != ScreenPaused) {
    if (s_action_bar_timer) {
      app_timer_cancel(s_action_bar_timer);
      s_action_bar_timer = NULL;
    }
    s_action_bar_visible = false;
    s_action_bar_page = 0;
    s_artwork_only = false;
    s_np_more_open = false;
    s_button_pressed = false;
    if (s_feedback_timer) {
      app_timer_cancel(s_feedback_timer);
      s_feedback_timer = NULL;
    }
    s_feedback_icon = FeedbackNone;
  }
  if (screen == ScreenHome && s_screen != ScreenHome) {
    shuffle_home_quote();
    // Give a previously failed background swap another chance on each Home entry.
    s_home_bg_failed_variant = -1;
  }
  bool playback_state_change =
      (previous_screen == ScreenPlaying || previous_screen == ScreenPaused) &&
      (screen == ScreenPlaying || screen == ScreenPaused);
  if (changed && !playback_state_change && s_overlay_visible &&
      screen_uses_overlay_window(screen) &&
      window_stack_contains_window(s_overlay_window)) {
    window_stack_remove(s_overlay_window, true);
    s_overlay_visible = false;
  }
  s_screen = screen;
  sync_touch_service();
  sync_overlay_window(changed);
  sync_native_menu(changed);
  if (screen == ScreenSearching || screen == ScreenBuffering) {
    s_animation_frame = 0;
    s_animation_timer = app_timer_register(180, animation_callback, NULL);
  }
  if (s_overlay_canvas && screen_uses_overlay_window(screen)) {
    layer_set_hidden(s_overlay_canvas, screen_uses_native_menu(screen));
    layer_mark_dirty(s_overlay_canvas);
  } else {
    layer_mark_dirty(s_root_canvas);
  }
}

// Screens that should never be recorded as a Back destination. They are either
// transient (spinners/errors) or represent live playback rather than a place in
// the menu hierarchy.
static bool screen_is_transient(AppScreen screen) {
  return screen == ScreenSearching || screen == ScreenBuffering ||
         screen == ScreenError || screen == ScreenPlaceholder ||
         screen == ScreenPlaying || screen == ScreenPaused;
}

static void nav_reset(void) {
  s_nav_depth = 0;
}

// Records the current screen on the history stack, then navigates to a new one.
// Use this for every forward navigation so Back can retrace the exact path.
static void nav_push(AppScreen screen) {
  APP_LOG(APP_LOG_LEVEL_INFO, "[MenuSync] nav_push from screen=%d selected_result=%d to screen=%d",
          (int) s_screen, s_selected_result, (int) screen);
  if (!screen_is_transient(s_screen)) {
    // Collapse immediate duplicates so repeated forward taps do not stack.
    bool duplicate = s_nav_depth > 0 && s_nav_stack[s_nav_depth - 1].screen == s_screen;
    if (!duplicate && s_nav_depth < NAV_STACK_MAX) {
      s_nav_stack[s_nav_depth++] = (NavEntry) {
        .screen = s_screen,
        .menu_selection = s_menu_selection,
        .library_type = s_library_type,
        .selected_result = s_selected_result,
      };
    } else if (!duplicate) {
      // Stack full: drop the oldest entry to make room for the newest.
      memmove(&s_nav_stack[0], &s_nav_stack[1], sizeof(NavEntry) * (NAV_STACK_MAX - 1));
      s_nav_stack[NAV_STACK_MAX - 1] = (NavEntry) {
        .screen = s_screen,
        .menu_selection = s_menu_selection,
        .library_type = s_library_type,
        .selected_result = s_selected_result,
      };
    }
  }
  set_screen(screen);
}

// Returns to the previous screen on the history stack (restoring its context).
// Returns false when the stack is empty (caller decides what to do, e.g. exit).
static bool nav_back(void) {
  if (s_nav_depth == 0) return false;
  NavEntry entry = s_nav_stack[--s_nav_depth];
  s_menu_selection = entry.menu_selection;
  s_library_type = entry.library_type;
  s_selected_result = entry.selected_result;
  APP_LOG(APP_LOG_LEVEL_INFO, "[MenuSync] nav_back -> screen=%d selected_result=%d result_count=%d",
          (int) entry.screen, entry.selected_result, s_result_count);
  set_screen(entry.screen);
  return true;
}

static void progress_timer_callback(void *context) {
  s_progress_timer = NULL;
  if (s_screen != ScreenPlaying) return;
  if (s_phone_audio) {
    if (s_duration_seconds == 0 || s_elapsed_seconds < s_duration_seconds) {
      s_elapsed_seconds++;
    }
  } else {
    s_elapsed_seconds = s_played_samples / 16000;
  }
  layer_mark_dirty(s_canvas);
  s_progress_timer = app_timer_register(1000, progress_timer_callback, NULL);
}

static void start_progress_timer(void) {
  if (s_progress_timer) app_timer_cancel(s_progress_timer);
  s_progress_timer = app_timer_register(1000, progress_timer_callback, NULL);
}

static void stop_progress_timer(void) {
  if (s_progress_timer) app_timer_cancel(s_progress_timer);
  s_progress_timer = NULL;
}

static void volume_timer_callback(void *context) {
  s_volume_timer = NULL;
  s_show_volume = false;
  layer_mark_dirty(s_canvas);
}

static void show_volume_temporarily(void) {
  s_show_volume = true;
  if (s_volume_timer) app_timer_cancel(s_volume_timer);
  s_volume_timer = app_timer_register(2000, volume_timer_callback, NULL);
  layer_mark_dirty(s_canvas);
}

static void action_bar_timer_callback(void *context) {
  s_action_bar_timer = NULL;
  s_action_bar_visible = false;
  s_action_bar_page = 0;
  s_button_pressed = false;
  layer_mark_dirty(s_canvas);
}

static void show_action_bar_page(int page) {
  s_action_bar_visible = true;
  s_action_bar_page = page;
  if (s_action_bar_timer) app_timer_cancel(s_action_bar_timer);
  s_action_bar_timer = app_timer_register(2000, action_bar_timer_callback, NULL);
  layer_mark_dirty(s_canvas);
}

static void feedback_timer_callback(void *context) {
  s_feedback_timer = NULL;
  s_feedback_icon = FeedbackNone;
  layer_mark_dirty(s_canvas);
}

// Flashes a confirmation/hint popup over the current screen for the given duration.
static void show_feedback_timed(FeedbackIcon icon, uint32_t duration_ms) {
  s_feedback_icon = icon;
  if (s_feedback_timer) app_timer_cancel(s_feedback_timer);
  s_feedback_timer = app_timer_register(duration_ms, feedback_timer_callback, NULL);
  layer_mark_dirty(s_canvas);
}

// Flashes a large confirmation icon over the playing screen for ~1s.
static void show_feedback(FeedbackIcon icon) {
  show_feedback_timed(icon, 1100);
}

static void format_time(uint32_t seconds, char *buffer, size_t size) {
  snprintf(buffer, size, "%lu:%02lu", (unsigned long) (seconds / 60),
           (unsigned long) (seconds % 60));
}

static uint8_t displayed_volume(void) {
  return s_phone_audio ? s_phone_volume : s_watch_volume;
}

static const SearchResult *current_playing_result(void) {
  if (s_has_now_playing) return &s_now_playing;
  if (s_result_count <= 0) return NULL;
  int index = s_selected_result;
  if (index < 0) index = 0;
  if (index >= s_result_count) index = s_result_count - 1;
  return &s_results[index];
}

static void draw_text(GContext *ctx, const char *text, GFont font, GColor color,
                      GRect rect, GTextAlignment alignment) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, rect, GTextOverflowModeTrailingEllipsis,
                     alignment, NULL);
}

static void fill_round(GContext *ctx, GColor color, GRect rect, uint16_t radius) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, rect, radius, GCornersAll);
}

// Draws a vertical scrollbar (track + thumb) pinned to the right edge. The thumb
// size and position are derived from the scroll progress. 'content_size' is the
// total scrollable extent and 'viewport_size' is how much is visible at once; the
// same routine works for item-based lists (counts) and pixel-based scrolling.
// Nothing is drawn when everything already fits.
static void draw_scrollbar(GContext *ctx, GRect track, int32_t offset,
                           int32_t viewport_size, int32_t content_size,
                           GColor track_color, GColor thumb_color) {
  if (content_size <= viewport_size || viewport_size <= 0) return;
  graphics_context_set_fill_color(ctx, track_color);
  graphics_fill_rect(ctx, track, track.size.w / 2, GCornersAll);

  const int min_thumb = 12;
  int thumb_h = (int) ((int64_t) track.size.h * viewport_size / content_size);
  if (thumb_h < min_thumb) thumb_h = min_thumb;
  if (thumb_h > track.size.h) thumb_h = track.size.h;

  int32_t max_offset = content_size - viewport_size;
  if (offset < 0) offset = 0;
  if (offset > max_offset) offset = max_offset;
  int travel = track.size.h - thumb_h;
  int thumb_y = max_offset > 0 ? (int) ((int64_t) travel * offset / max_offset) : 0;

  graphics_context_set_fill_color(ctx, thumb_color);
  graphics_fill_rect(ctx, GRect(track.origin.x, track.origin.y + thumb_y, track.size.w, thumb_h),
                     track.size.w / 2, GCornersAll);
}

// Shared bookkeeping for a smoothly-scrolling selection list. Given the fixed
// row pitch, item count, currently selected index and the viewport rectangle,
// it updates s_scroll_max and, when 'follow_selection' is set, adjusts the scroll
// target so the selected row is fully visible (smooth-glide, not centered).
// Returns the pixel offset (s_scroll) the caller should subtract from row Y.
static int scroll_list_layout(int row_pitch, int count, int selected,
                              int viewport_top, int viewport_height,
                              bool follow_selection) {
  int content_height = row_pitch * count;
  int max_scroll = content_height - viewport_height;
  if (max_scroll < 0) max_scroll = 0;
  s_scroll_max = max_scroll;

  if (follow_selection && count > 0) {
    int sel_top = selected * row_pitch;
    int sel_bottom = sel_top + row_pitch;
    int target = s_scroll_target;
    if (sel_top < target) {
      target = sel_top;                          // Scroll up to reveal the row.
    } else if (sel_bottom > target + viewport_height) {
      target = sel_bottom - viewport_height;     // Scroll down to reveal the row.
    }
    if (target < 0) target = 0;
    if (target > max_scroll) target = max_scroll;
    if (target != s_scroll_target) scroll_to(target);
  }

  // Keep the rendered offset in range even if content shrank since last frame.
  if (s_scroll_target > max_scroll) s_scroll_target = max_scroll;
  if (s_scroll > max_scroll) s_scroll = max_scroll;
  (void) viewport_top;
  return s_scroll;
}

static void draw_dreamhouse_backdrop(GContext *ctx) {
  (void) ctx;
}

static void update_time_text(void) {
  time_t now = time(NULL);
  struct tm *current = localtime(&now);
  if (clock_is_24h_style()) {
    strftime(s_time_text, sizeof(s_time_text), "%H:%M", current);
  } else {
    strftime(s_time_text, sizeof(s_time_text), "%I:%M", current);
    if (s_time_text[0] == '0') memmove(s_time_text, s_time_text + 1, sizeof(s_time_text) - 1);
  }
}

static void minute_tick(struct tm *tick_time, TimeUnits units_changed) {
  update_time_text();
  if (s_canvas && s_screen == ScreenHome) layer_mark_dirty(s_canvas);
}

static GBitmap *current_status_mascot(void) {
  // Legacy Dreamhouse/Ticket mascot theme - disabled, see s_legacy_theme's comment.
  // if (s_legacy_unlocked && s_legacy_theme == 1 && s_mascot_dreamhouse) return s_mascot_dreamhouse;
  // if (s_legacy_unlocked && s_legacy_theme == 2 && s_mascot_ticket) return s_mascot_ticket;
  return s_mascot_modern;
}

#if 0  // Disabled along with the legacy mascot theme above.
static const char *legacy_theme_name(void) {
  if (s_legacy_theme == 1) return "Dreamhouse";
  if (s_legacy_theme == 2) return "Ticket";
  return "Modern";
}
#endif

static void draw_status_mascot(GContext *ctx, GPoint origin) {
  GBitmap *mascot = current_status_mascot();
  if (mascot) graphics_draw_bitmap_in_rect(ctx, mascot, GRect(origin.x, origin.y, 48, 48));
}

static void draw_home_action_bar(GContext *ctx) {
  ThemeColors c = colors();
  const int x = 170;
  graphics_context_set_fill_color(ctx, c.action_bar_bg);
  graphics_fill_rect(ctx, GRect(x, 0, 30, 228), 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, c.action_bar_icon);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(x, 0), GPoint(x, 228));

  graphics_draw_line(ctx, GPoint(179, 35), GPoint(190, 35));
  graphics_draw_line(ctx, GPoint(179, 35), GPoint(179, 47));
  graphics_draw_line(ctx, GPoint(179, 47), GPoint(190, 47));
  graphics_draw_line(ctx, GPoint(190, 35), GPoint(190, 47));
  graphics_draw_line(ctx, GPoint(182, 31), GPoint(187, 31));

  // Same search icon used by the playback action bar.
  graphics_draw_circle(ctx, GPoint(183, 105), 7);
  graphics_draw_line(ctx, GPoint(188, 110), GPoint(194, 116));

  graphics_context_set_fill_color(ctx, c.action_bar_icon);
  graphics_fill_circle(ctx, GPoint(185, 176), 2);
  graphics_fill_circle(ctx, GPoint(185, 183), 2);
  graphics_fill_circle(ctx, GPoint(185, 190), 2);
}

static void draw_header(GContext *ctx, const char *label) {
  ThemeColors c = colors();
  graphics_context_set_fill_color(ctx, c.accent);
  graphics_fill_rect(ctx, GRect(0, 0, 200, MENU_HEADER_HEIGHT), 0, GCornerNone);
  draw_text(ctx, label, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GColorWhite,
            GRect(8, 4, 184, 24), GTextAlignmentLeft);
}

// Ensures the home background bitmap matches the current Home style, reloading it
// only when the style actually changes. Keeping just one bitmap resident avoids
// exhausting the app heap with two full-screen 8-bit images.
// Home background variant indices: 0 = Unicorn, 1 = Kiwi, 2 = Mono Unicorn,
// 3 = Mono Kiwi. Combines the existing Home style toggle with the Mono theme's
// dedicated black & white art.
static int home_background_variant(void) {
  return (s_theme == ThemeMono ? 2 : 0) + (s_alt_home ? 1 : 0);
}

// The default "Unicorn" home (variant 0) is drawn as a flat pink fill with the mascot
// in the corner rather than a full-screen background bitmap. The Kiwi (alt) and Mono
// variants still use their own full-screen art.
static bool home_is_pink_variant(int variant) {
  return variant == 0;
}
// Pink fill shared by the Unicorn home; matches the mascot image's own background
// (255,0,170 is exactly representable in Pebble's 64-color palette, so no seam).
#define HOME_PINK GColorFromRGB(255, 0, 170)

static uint32_t home_background_resource(int variant) {
  switch (variant) {
    case 1: return RESOURCE_ID_IMAGE_DREAMWAVE_HOME_BACKGROUND_ALT;
    case 2: return RESOURCE_ID_IMAGE_MONO_HOME_BACKGROUND;
    case 3: return RESOURCE_ID_IMAGE_MONO_HOME_BACKGROUND_ALT;
    default: return RESOURCE_ID_IMAGE_DREAMWAVE_HOME_BACKGROUND;
  }
}

static void ensure_home_background(void) {
  int want_variant = home_background_variant();
  bool pink = home_is_pink_variant(want_variant);
  // Already configured for this variant? The pink variant carries no full-screen
  // bitmap, so its "loaded" state is tracked purely by the variant index.
  if (s_home_background_variant == want_variant && (s_home_background || pink)) return;
  // After a failed swap, keep showing the fallback art instead of retrying on every
  // paint; the latch is cleared when Home is re-entered or the style is toggled.
  if (s_home_background && s_home_bg_failed_variant == want_variant) return;
  if (s_home_background) {
    gbitmap_destroy(s_home_background);
    s_home_background = NULL;
  }
  // Temporarily drop the small mascot bitmaps as well: they were allocated right
  // after the old background, so freeing them lets the heap coalesce a hole large
  // enough for the replacement image. Without this the swap can fail forever on a
  // tight heap (the new bitmap's bookkeeping struct lands inside the freed hole,
  // leaving it a few bytes short for the pixel data) and Home stays blank until
  // the app is relaunched.
  if (s_mascot_modern) gbitmap_destroy(s_mascot_modern);
  if (s_mascot_dreamhouse) gbitmap_destroy(s_mascot_dreamhouse);
  if (s_mascot_ticket) gbitmap_destroy(s_mascot_ticket);
  if (s_mascot_home) gbitmap_destroy(s_mascot_home);
  s_mascot_modern = NULL;
  s_mascot_dreamhouse = NULL;
  s_mascot_ticket = NULL;
  s_mascot_home = NULL;
  if (pink) {
    // Flat pink fill + corner mascot: no full-screen background bitmap to load.
    s_home_background = NULL;
    s_home_background_variant = want_variant;
    s_home_bg_failed_variant = -1;
  } else {
    s_home_background = gbitmap_create_with_resource(home_background_resource(want_variant));
    s_home_background_variant = want_variant;
    if (s_home_background) {
      s_home_bg_failed_variant = -1;
    } else {
      // Allocation failed: fall back to the other style's art within the same
      // color family (Unicorn <-> Kiwi) so Home is never left blank, and remember
      // the failure so we do not thrash reload attempts.
      s_home_bg_failed_variant = want_variant;
      int fallback_variant = want_variant ^ 1;
      s_home_background = gbitmap_create_with_resource(home_background_resource(fallback_variant));
      s_home_background_variant = fallback_variant;
    }
  }
  s_mascot_modern = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DREAMWAVE_MASCOT_MODERN);
  s_mascot_dreamhouse = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DREAMWAVE_MASCOT_DREAMHOUSE);
  s_mascot_ticket = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DREAMWAVE_MASCOT_TICKET);
  if (pink) {
    s_mascot_home = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DREAMWAVE_MASCOT_HOME);
  }
}

/* ---------------------------------------------------------------------------
   Bespoke UI (Advanced > Bespoke UI). One chromeless language for every list
   screen, taken from the Queue: white ground, a small dark-gray eyebrow at
   (18,14), a rounded accent selection, the right-edge scrollbar, and a footer
   hint band at y=198. These helpers exist so the screens cannot drift apart.
   --------------------------------------------------------------------------- */
#define BESPOKE_LIST_TOP 40
#define BESPOKE_FOOTER_TOP 198
#define BESPOKE_VIEWPORT_H (BESPOKE_FOOTER_TOP - BESPOKE_LIST_TOP)
// Every bespoke list uses the Queue's row metrics - one rhythm across the whole app.
// Single-line rows keep the same height and pitch as the two-line song rows so the
// lists line up with each other rather than each screen inventing its own spacing.
#define BESPOKE_ROW_PITCH 43
#define BESPOKE_ROW_H 39

static void bespoke_eyebrow(GContext *ctx, const char *label) {
  draw_text(ctx, label, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GColorBlack,
            GRect(18, 14, 164, 18), GTextAlignmentLeft);
}

static void bespoke_ground(GContext *ctx, const char *label) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  bespoke_eyebrow(ctx, label);
}

// Re-stamps the top and bottom bands after the rows are drawn, so a row scrolled to
// the edge of the viewport can never bleed into the eyebrow or the hint.
static void bespoke_frame(GContext *ctx, const char *label, const char *hint) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, 0, 200, BESPOKE_LIST_TOP), 0, GCornerNone);
  bespoke_eyebrow(ctx, label);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, BESPOKE_FOOTER_TOP, 200, 228 - BESPOKE_FOOTER_TOP),
                     0, GCornerNone);
  if (hint) {
    draw_text(ctx, hint, fonts_get_system_font(FONT_KEY_GOTHIC_14), GColorBlack,
              GRect(8, 204, 184, 18), GTextAlignmentCenter);
  }
}

static void bespoke_scrollbar(GContext *ctx, int offset) {
  draw_scrollbar(ctx, GRect(194, BESPOKE_LIST_TOP, 4, BESPOKE_VIEWPORT_H), offset,
                 BESPOKE_VIEWPORT_H, BESPOKE_VIEWPORT_H + s_scroll_max,
                 GColorLightGray, accent_color());
}

// Two-line row: bold title over a gray subtitle. `title_color` lets a caller flag a
// row (song lists tint the track that is playing) without duplicating the row body.
static void bespoke_row2(GContext *ctx, int y, int h, const char *title, const char *sub,
                         bool selected, GColor title_color) {
  if (selected) {
    graphics_context_set_fill_color(ctx, accent_color());
    graphics_fill_rect(ctx, GRect(5, y, 190, h), 4, GCornersAll);
  }
  draw_text(ctx, title, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
            selected ? GColorWhite : title_color,
            GRect(12, y + 1, 176, 22), GTextAlignmentLeft);
  draw_text(ctx, sub, fonts_get_system_font(FONT_KEY_GOTHIC_14),
            selected ? GColorWhite : GColorBlack,
            GRect(12, y + 20, 176, 18), GTextAlignmentLeft);
}

// Single-line row: bold label left, optional value right-aligned in the accent. Same
// height and margins as bespoke_row2(); the label is centred in the row rather than
// sitting where a two-line row's title would.
static void bespoke_row1(GContext *ctx, int y, int h, const char *label, const char *value,
                         bool selected) {
  if (selected) {
    graphics_context_set_fill_color(ctx, accent_color());
    graphics_fill_rect(ctx, GRect(5, y, 190, h), 4, GCornersAll);
  }
  int text_y = y + (h - 22) / 2;
  draw_text(ctx, label, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
            selected ? GColorWhite : GColorBlack,
            GRect(12, text_y, value ? 100 : 176, 22), GTextAlignmentLeft);
  if (value) {
    draw_text(ctx, value, fonts_get_system_font(FONT_KEY_GOTHIC_14),
              selected ? GColorWhite : accent_color(),
              GRect(112, text_y + 3, 76, 18), GTextAlignmentRight);
  }
}

// Variable-height sibling of scroll_list_layout(): the grouped Advanced list mixes
// section headers with rows, so the selection's extent is passed in directly rather
// than derived from a uniform pitch.
static int bespoke_scroll(int content_h, int sel_top, int sel_bottom) {
  int max_scroll = content_h - BESPOKE_VIEWPORT_H;
  if (max_scroll < 0) max_scroll = 0;
  s_scroll_max = max_scroll;
  int target = s_scroll_target;
  if (sel_top < target) {
    target = sel_top;
  } else if (sel_bottom > target + BESPOKE_VIEWPORT_H) {
    target = sel_bottom - BESPOKE_VIEWPORT_H;
  }
  if (target < 0) target = 0;
  if (target > max_scroll) target = max_scroll;
  if (target != s_scroll_target) scroll_to(target);
  if (s_scroll_target > max_scroll) s_scroll_target = max_scroll;
  if (s_scroll > max_scroll) s_scroll = max_scroll;
  return s_scroll;
}

// The empty/loading state shared by every bespoke list: a centred message with an
// optional second line, on the same ground as the populated list so it never reads
// as a crash.
static void bespoke_empty(GContext *ctx, const char *label, const char *message,
                          const char *detail, const char *hint) {
  bespoke_ground(ctx, label);
  draw_text(ctx, message, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GColorBlack,
            GRect(18, 96, 164, 26), GTextAlignmentCenter);
  if (detail) {
    draw_text(ctx, detail, fonts_get_system_font(FONT_KEY_GOTHIC_14), GColorBlack,
              GRect(18, 120, 164, 36), GTextAlignmentCenter);
  }
  if (hint) {
    draw_text(ctx, hint, fonts_get_system_font(FONT_KEY_GOTHIC_14), GColorBlack,
              GRect(8, 204, 184, 18), GTextAlignmentCenter);
  }
}

// Song lists: Results, Library items and the Queue all render through this, so the
// three screens stay pixel-identical. `current_id` tints the row for the track that
// is playing; pass "" to disable.
static void bespoke_song_list(GContext *ctx, const char *label, const char *hint,
                              const char *current_id) {
  const int row_pitch = BESPOKE_ROW_PITCH;
  const int row_h = BESPOKE_ROW_H;
  int offset = scroll_list_layout(row_pitch, s_result_count, s_selected_result,
                                  BESPOKE_LIST_TOP, BESPOKE_VIEWPORT_H, true);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  for (int i = 0; i < s_result_count; i++) {
    int y = BESPOKE_LIST_TOP + i * row_pitch - offset;
    if (y + row_h < BESPOKE_LIST_TOP || y > BESPOKE_FOOTER_TOP) continue;
    bool is_current = current_id[0] && strcmp(s_results[i].video_id, current_id) == 0;
    bespoke_row2(ctx, y, row_h, s_results[i].title, s_results[i].artist,
                 i == s_selected_result, is_current ? accent_color() : GColorBlack);
  }
  bespoke_frame(ctx, label, hint);
  bespoke_scrollbar(ctx, offset);
}

// The id every song list highlights as "this one is playing", or "" when nothing is.
static const char *bespoke_now_playing_id(void) {
  return s_has_now_playing ? s_now_playing.video_id : "";
}

static void draw_home_list(GContext *ctx);
static void draw_advanced_bespoke(GContext *ctx);

static void draw_home(GContext *ctx) {
  if (s_bespoke_ui) {
    draw_home_list(ctx);
    return;
  }
  ensure_home_background();
  if (home_is_pink_variant(home_background_variant())) {
    graphics_context_set_fill_color(ctx, HOME_PINK);
    graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
    if (s_mascot_home) {
      GSize sz = gbitmap_get_bounds(s_mascot_home).size;
      graphics_draw_bitmap_in_rect(ctx, s_mascot_home, GRect(0, 228 - sz.h, sz.w, sz.h));
    }
  } else if (s_home_background) {
    graphics_draw_bitmap_in_rect(ctx, s_home_background, GRect(0, 0, 200, 228));
  }
  if (!s_show_home_quotes) {
    draw_home_action_bar(ctx);
    return;
  }
  // Banner box pinned to the top (no speech-bubble tail). The box height adapts to
  // however many rows the quote wraps to, with equal spacing above and below the text.
  int quote_index = s_home_quote % active_home_quote_count();
  const char *quote = s_bridge_ready ? active_home_quotes()[quote_index] : "Finding phone...";
  // Longer quotes need a smaller font to avoid clipping inside the banner.
  const char *quote_font = strlen(quote) > 18 ? FONT_KEY_GOTHIC_14_BOLD : FONT_KEY_GOTHIC_18_BOLD;
  GFont font = fonts_get_system_font(quote_font);

  const int box_x = 10;
  const int box_y = 14;   // Fixed top position.
  const int box_w = 152;
  const int pad_x = 8;
  const int pad_y = 8;    // Equal spacing above and below the text.
  const int text_w = box_w - 2 * pad_x;

  // Measure the wrapped text so the box hugs its content with symmetric padding.
  GSize text_size = graphics_text_layout_get_content_size(
      quote, font, GRect(0, 0, text_w, 200), GTextOverflowModeWordWrap, GTextAlignmentCenter);
  int box_h = text_size.h + 2 * pad_y;

  const GRect banner = GRect(box_x, box_y, box_w, box_h);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, banner, 6, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_round_rect(ctx, banner, 6);
  // Nudge up slightly to offset the font's internal top leading so the visible gap
  // above and below the glyphs looks equal.
  draw_text(ctx, quote, font, GColorBlack,
            GRect(box_x + pad_x, box_y + pad_y - 2, text_w, text_size.h),
            GTextAlignmentCenter);
  draw_home_action_bar(ctx);
}

static const char *const LIBRARY_ITEMS[] = {
  "Recently Played", "Cached Music", "Favorites", "Playlists", "Continue", "Recent Searches",
};
// Maps a Library menu row to its library type (parallel to LIBRARY_ITEMS).
static const int LIBRARY_TYPES[] = {
  LibraryRecent, LibraryCached, LibraryFavorites, LibraryPlaylists, LibraryContinue, LibraryRecentSearches,
};
static const char *const MENU_ITEMS[] = {"Search", "Library", "Settings", "About"};

// Bespoke Home: a now-playing hero over a four-icon dock. The hero carries the three
// things worth saying the instant Home appears, in priority order: the bridge is down,
// something is playing, or nothing is. The dock is the More popup's icon row promoted
// to the top level - UP/DOWN walk it, SELECT opens, and the destinations reuse
// MENU_ITEMS so SELECT runs the same actions ScreenMenu does.
//
// The hero joins the selection ring (at index 0) only when there is a track behind it
// to open; not-connected and idle variants are display surfaces with nowhere to go,
// so the dock keeps indices 0-3 then. Selecting the hero materializes it as an accent
// card - the same "selection fills in the accent" idiom as every other screen.
static bool home_hero_selectable(void) {
  return s_has_now_playing;
}

static void draw_home_hero(GContext *ctx, bool selected) {
  const GColor text = selected ? GColorWhite : GColorBlack;
  const GColor tag = selected ? GColorWhite : accent_color();
  if (selected) {
    graphics_context_set_fill_color(ctx, accent_color());
    graphics_fill_rect(ctx, GRect(5, 32, 190, 122), 6, GCornersAll);
  }

  if (!s_bridge_ready) {
    draw_text(ctx, "NOT CONNECTED", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
              text, GRect(18, 40, 176, 16), GTextAlignmentLeft);
    graphics_context_set_text_color(ctx, selected ? GColorWhite : GColorDarkGray);
    graphics_draw_text(ctx, "Open dreamwave on phone",
                       fonts_get_system_font(FONT_KEY_GOTHIC_18),
                       GRect(18, 60, 164, 48), GTextOverflowModeWordWrap,
                       GTextAlignmentLeft, NULL);
    return;
  }

  draw_text(ctx, s_playback_active ? "NOW PLAYING" : "PAUSED",
            fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), text,
            GRect(18, 40, 116, 16), GTextAlignmentLeft);
  draw_text(ctx, s_phone_audio ? "PHONE" : "WATCH",
            fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), tag,
            GRect(126, 40, 56, 16), GTextAlignmentRight);
  draw_text(ctx, s_now_playing.title, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
            text, GRect(18, 60, 164, 32), GTextAlignmentLeft);
  draw_text(ctx, s_now_playing.artist, fonts_get_system_font(FONT_KEY_GOTHIC_18),
            text, GRect(18, 92, 164, 22), GTextAlignmentLeft);
  // The companion's position events keep this bar fresh while Home is showing.
  // Respects the battery-saver "Progress bar" toggle exactly as Now Playing does.
  if (s_show_progress) {
    int filled = 0;
    if (s_duration_seconds > 0) {
      uint32_t elapsed = s_elapsed_seconds > s_duration_seconds ? s_duration_seconds
                                                                : s_elapsed_seconds;
      filled = 164 * elapsed / s_duration_seconds;
    }
    graphics_context_set_fill_color(ctx, GColorLightGray);
    graphics_fill_rect(ctx, GRect(18, 122, 164, 4), 2, GCornersAll);
    graphics_context_set_fill_color(ctx, selected ? GColorWhite : accent_color());
    graphics_fill_rect(ctx, GRect(18, 122, filled, 4), 2, GCornersAll);
    char left[16];
    char right[16];
    format_time(s_elapsed_seconds, left, sizeof(left));
    format_time(s_duration_seconds, right, sizeof(right));
    draw_text(ctx, left, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), text,
              GRect(18, 130, 82, 16), GTextAlignmentLeft);
    draw_text(ctx, right, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), text,
              GRect(100, 130, 82, 16), GTextAlignmentRight);
  }
}

// Parallel to MENU_ITEMS: what each destination holds. Shown under the destination's
// name when the dock highlight is on it - the More popup's name-over-description idiom.
static const char *const MENU_HINTS[] = {
  "Songs, artists, radio", "Recent, cached, saved", "Output, volume, more", "Version and credits",
};

static void draw_home_list(GContext *ctx) {
  const int hero_rows = home_hero_selectable() ? 1 : 0;
  const bool hero_selected = hero_rows && s_home_selection == 0;
  int row = s_home_selection - hero_rows;
  if (row < 0 || row >= (int) ARRAY_LENGTH(MENU_ITEMS)) row = 0;
  bespoke_ground(ctx, "DREAMWAVE");

  // The upper surface always describes the highlighted ring entry, More-popup style:
  // the now-playing card when the hero is selected, otherwise the destination's name
  // over what it holds. A down bridge overrides both - that alert matters more than
  // menu context.
  if (!s_bridge_ready) {
    draw_home_hero(ctx, false);
  } else if (hero_selected) {
    draw_home_hero(ctx, true);
  } else {
    draw_text(ctx, MENU_ITEMS[row], fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
              GColorBlack, GRect(18, 54, 164, 40), GTextAlignmentLeft);
    draw_text(ctx, MENU_HINTS[row], fonts_get_system_font(FONT_KEY_GOTHIC_18),
              GColorDarkGray, GRect(18, 96, 164, 24), GTextAlignmentLeft);
  }

  // Nothing here scrolls, so no bespoke_frame stamping - just the dock and the hint.
  const int dock_y = 178;
  const int dock_pitch = 40;
  const int count = (int) ARRAY_LENGTH(MENU_ITEMS);
  for (int i = 0; i < count; i++) {
    const int cx = 100 + (2 * i - (count - 1)) * dock_pitch / 2;
    GColor glyph = GColorBlack;
    if (s_home_selection == i + hero_rows) {
      graphics_context_set_fill_color(ctx, accent_color());
      graphics_fill_circle(ctx, GPoint(cx, dock_y), 17);
      glyph = GColorWhite;
    }
    const GPoint gc = GPoint(cx, dock_y);
    switch (i) {
      case 0: draw_search_icon(ctx, gc, glyph); break;
      case 1: draw_vinyl_icon(ctx, gc, glyph); break;
      case 2: draw_sliders_icon(ctx, gc, glyph); break;
      default: draw_info_icon(ctx, gc, glyph); break;
    }
  }
  draw_text(ctx, "UP/DOWN choose    SELECT open",
            fonts_get_system_font(FONT_KEY_GOTHIC_14), GColorBlack,
            GRect(8, 204, 184, 18), GTextAlignmentCenter);
}

static const char *const INPUT_CHOICE_ITEMS[] = {"Voice search", "Keyboard"};

static const char *const SEARCH_TYPE_ITEMS[] = {"Song Search", "Artist Radio", "Song Radio"};

// Keyboard is the only row shown until unlocked from About (7x SELECT on VERSION) -
// see advanced_item_count()/s_advanced_unlocked - so it must stay index 0.
static const char *const ADVANCED_ITEMS[] = {
  "Keyboard",        // 0  \_ Interface
  "Bespoke UI",      // 1  |
  "Theme",           // 2  |
  "Home style",      // 3  |
  "Home quotes",     // 4  |
  "Cover art bg",    // 5  /
  "History",         // 6  \_ Library
  "Results",         // 7  |
  "Library extras",  // 8  /
  "Watch quality",   // 9  \_ Audio
  "Phone quality",   // 10 |
  "Cache radio",     // 11 /
};

// Bespoke Advanced draws label and value in separate columns, so it needs them apart;
// the stock list joins them as "Label: Value". Order matches ADVANCED_ITEMS.
static const char *advanced_value(int index) {
  static char buf[16];
  switch (index) {
    case 0: return s_keyboard_pt2 ? "Grid" : "Classic";
    case 1: return s_bespoke_ui ? "On" : "Off";
    case 2: return theme_name();
    case 3: return s_alt_home ? "Kiwi" : "Unicorn";
    case 4: return s_show_home_quotes ? "Show" : "Hide";
    case 5: return s_cover_art_background ? "On" : "Off";
    case 6: snprintf(buf, sizeof(buf), "%d songs", s_history_limit); return buf;
    case 7: snprintf(buf, sizeof(buf), "%d", s_search_limit); return buf;
    case 8: return s_extra_library ? "On" : "Off";
    case 9: return s_watch_audio_quality ? "Balanced" : "Efficient";
    case 10: return s_phone_audio_quality ? "High" : "Data Saver";
    case 11: return s_cache_radio ? "On" : "Off";
    default: return "";
  }
}

// Contiguous spans of ADVANCED_ITEMS, used only by the bespoke grouped layout.
typedef struct {
  const char *label;
  int first;
  int count;
} AdvancedGroup;

static const AdvancedGroup ADVANCED_GROUPS[] = {
  {"INTERFACE", 0, 6},
  {"LIBRARY",   6, 3},
  {"AUDIO",     9, 3},
};

static int native_menu_item_count(void) {
  if (s_screen == ScreenLibrary) return library_item_count();
  if (s_screen == ScreenMenu) return (int) ARRAY_LENGTH(MENU_ITEMS);
  if (s_screen == ScreenInputChoice) return (int) ARRAY_LENGTH(INPUT_CHOICE_ITEMS);
  if (s_screen == ScreenSearchType) return (int) ARRAY_LENGTH(SEARCH_TYPE_ITEMS);
  if (s_screen == ScreenSettings) return settings_item_count();
  if (s_screen == ScreenAdvanced) return advanced_item_count();
  if (s_screen == ScreenResults || s_screen == ScreenLibraryItems || s_screen == ScreenQueue) {
    return s_result_count;
  }
  return 0;
}

static const char *native_menu_title(void) {
  if (s_screen == ScreenLibrary) return "LIBRARY";
  if (s_screen == ScreenMenu) return "DREAMWAVE";
  if (s_screen == ScreenInputChoice) return "SEARCH WITH";
  if (s_screen == ScreenSearchType) return "SEARCH";
  if (s_screen == ScreenSettings) return "SETTINGS";
  if (s_screen == ScreenAdvanced) return "ADVANCED";
  if (s_screen == ScreenResults) return "SEARCH RESULTS";
  if (s_screen == ScreenLibraryItems) return library_items_title();
  if (s_screen == ScreenQueue) return "QUEUE";
  return "";
}

static const char *native_menu_item_title(int index) {
  if (s_screen == ScreenLibrary) {
    if (index < 0 || index >= library_item_count()) return "";
    return LIBRARY_ITEMS[index];
  }
  if (s_screen == ScreenMenu) {
    if (index < 0 || index >= (int) ARRAY_LENGTH(MENU_ITEMS)) return "";
    return MENU_ITEMS[index];
  }
  if (s_screen == ScreenInputChoice) {
    if (index < 0 || index >= (int) ARRAY_LENGTH(INPUT_CHOICE_ITEMS)) return "";
    return INPUT_CHOICE_ITEMS[index];
  }
  if (s_screen == ScreenSearchType) {
    if (index < 0 || index >= (int) ARRAY_LENGTH(SEARCH_TYPE_ITEMS)) return "";
    return SEARCH_TYPE_ITEMS[index];
  }
  if (s_screen == ScreenResults || s_screen == ScreenLibraryItems || s_screen == ScreenQueue) {
    if (index < 0 || index >= s_result_count) return "";
    return s_results[index].title;
  }
  if (s_screen == ScreenSettings) {
    static char route[32];
    static char watch_volume[32];
    static char phone_volume[32];
    static char input_mode[32];
    static char progress_bar[32];
    static char advanced[24];
    snprintf(route, sizeof(route), "Output: %s", s_phone_audio ? "Phone" : "Watch");
    snprintf(watch_volume, sizeof(watch_volume), "Watch volume: %d%%", s_watch_volume);
    snprintf(phone_volume, sizeof(phone_volume), "Phone volume: %d%%", s_phone_volume);
    snprintf(input_mode, sizeof(input_mode), "Input: %s",
             s_input_mode == InputVoice ? "Voice" :
             s_input_mode == InputKeyboard ? "Keyboard" : "Ask");
    snprintf(progress_bar, sizeof(progress_bar), "Progress bar: %s",
             s_show_progress ? "Show" : "Hide");
    snprintf(advanced, sizeof(advanced), "Advanced");
    const char *items[] = {input_mode, route, watch_volume, phone_volume, progress_bar, advanced};
    int count = settings_item_count();
    if (index < 0 || index >= count) return "";
    return items[index];
  }
  if (s_screen == ScreenAdvanced) {
    static char row[40];
    if (index < 0 || index >= advanced_item_count()) return "";
    snprintf(row, sizeof(row), "%s: %s", ADVANCED_ITEMS[index], advanced_value(index));
    return row;
  }
  return "";
}

static const char *native_menu_item_subtitle(int index) {
  if (s_screen == ScreenResults || s_screen == ScreenLibraryItems || s_screen == ScreenQueue) {
    if (index < 0 || index >= s_result_count) return "";
    return s_results[index].artist;
  }
  return NULL;
}

static bool native_menu_uses_result_selection(void) {
  return s_screen == ScreenResults || s_screen == ScreenLibraryItems || s_screen == ScreenQueue;
}

static int *native_menu_selection_ptr(void) {
  return native_menu_uses_result_selection() ? &s_selected_result : &s_menu_selection;
}

static uint16_t native_menu_get_num_rows_callback(struct MenuLayer *menu_layer,
                                                  uint16_t section_index,
                                                  void *context) {
  (void) menu_layer;
  (void) section_index;
  (void) context;
  return native_menu_item_count();
}

static int16_t native_menu_get_cell_height_callback(struct MenuLayer *menu_layer,
                                                    MenuIndex *cell_index,
                                                    void *context) {
  (void) menu_layer;
  (void) cell_index;
  (void) context;
  if (native_menu_uses_result_selection()) return 50;
  return 44;
}

static void native_menu_draw_header_callback(GContext *ctx, const Layer *cell_layer,
                                             uint16_t section_index,
                                             void *context) {
  (void) section_index;
  (void) context;
  graphics_context_set_fill_color(ctx, accent_color());
  graphics_fill_rect(ctx, layer_get_bounds(cell_layer), 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, native_menu_title(),
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(8, 4, layer_get_bounds(cell_layer).size.w - 16, 24),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static int16_t native_menu_get_header_height_callback(struct MenuLayer *menu_layer,
                                                      uint16_t section_index,
                                                      void *context) {
  (void) menu_layer;
  (void) section_index;
  (void) context;
  return 31;
}

static void native_menu_draw_row_callback(GContext *ctx, const Layer *cell_layer,
                                          MenuIndex *cell_index,
                                          void *context) {
  (void) context;
  const bool results_style = native_menu_uses_result_selection();
  const char *title = native_menu_item_title(cell_index->row);
  const char *subtitle = native_menu_item_subtitle(cell_index->row);
  if (results_style && subtitle) {
    menu_cell_basic_draw(ctx, cell_layer, title, subtitle, NULL);
    return;
  }
  menu_cell_basic_draw(ctx, cell_layer, title, NULL, NULL);

  if (s_screen == ScreenSettings && cell_index->row == settings_item_count() - 1) {
    GRect bounds = layer_get_bounds(cell_layer);
    graphics_context_set_text_color(ctx, menu_cell_layer_is_highlighted(cell_layer)
                                             ? GColorWhite
                                             : GColorDarkGray);
    graphics_draw_text(ctx, ">", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(bounds.size.w - 18, 11, 12, 24),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  }
}

static void native_menu_select_callback(struct MenuLayer *menu_layer,
                                        MenuIndex *cell_index,
                                        void *context) {
  (void) menu_layer;
  (void) context;
  *native_menu_selection_ptr() = cell_index->row;
  select_click(NULL, NULL);
}

// Keeps the app's selection variable in sync while the MenuLayer handles
// Up/Down scrolling directly. Without this, s_menu_selection / s_selected_result
// go stale during scrolling and a later nav_push() saves the wrong row.
static void native_menu_selection_changed_callback(struct MenuLayer *menu_layer,
                                                   MenuIndex new_index,
                                                   MenuIndex old_index,
                                                   void *context) {
  (void) menu_layer;
  (void) old_index;
  (void) context;
  *native_menu_selection_ptr() = new_index.row;
}

static bool screen_uses_native_menu(AppScreen screen) {
  // Bespoke UI draws every list itself on the canvas, so the MenuLayer stays hidden.
  if (s_bespoke_ui) return false;
  if (screen == ScreenMenu || screen == ScreenLibrary ||
      screen == ScreenSettings || screen == ScreenAdvanced ||
      screen == ScreenInputChoice || screen == ScreenSearchType) {
    return true;
  }
  if (screen == ScreenResults) return s_result_count > 0;
  if (screen == ScreenLibraryItems) return !s_library_loading && s_result_count > 0;
  if (screen == ScreenQueue) {
    return !s_queue_loading && s_result_count > 0;
  }
  return false;
}

static bool screen_uses_overlay_window(AppScreen screen) {
  return screen != ScreenHome;
}

static void sync_overlay_window(bool animated) {
  if (!s_overlay_window || !s_root_canvas) return;
  bool show_overlay = screen_uses_overlay_window(s_screen);
  s_canvas = show_overlay ? s_overlay_canvas : s_root_canvas;
  layer_set_hidden(menu_layer_get_layer(s_native_menu_layer),
                   !(show_overlay && screen_uses_native_menu(s_screen)));
  if (show_overlay && !s_overlay_visible) {
    window_stack_push(s_overlay_window, animated);
    s_overlay_visible = true;
  } else if (!show_overlay && s_overlay_visible) {
    if (window_stack_contains_window(s_overlay_window)) {
      window_stack_remove(s_overlay_window, animated);
    }
    s_overlay_visible = false;
  }
  if (s_overlay_canvas) layer_set_hidden(s_overlay_canvas, !show_overlay);
  layer_set_hidden(s_root_canvas, show_overlay);
  if (show_overlay) {
    layer_mark_dirty(s_overlay_canvas);
  } else {
    layer_mark_dirty(s_root_canvas);
  }
}

static void sync_native_menu(bool animated) {
  if (!s_native_menu_layer) return;
  bool visible = screen_uses_native_menu(s_screen);
  Layer *menu_layer = menu_layer_get_layer(s_native_menu_layer);
  if (visible) {
    layer_set_hidden(menu_layer, false);
    if (s_overlay_canvas) layer_set_hidden(s_overlay_canvas, true);
    int *selection = native_menu_selection_ptr();
    int count = native_menu_item_count();
    APP_LOG(APP_LOG_LEVEL_INFO, "[MenuSync] screen=%d animated=%d raw_sel=%d count=%d",
            (int) s_screen, (int) animated, *selection, count);
    if (count <= 0) {
      *selection = 0;
    } else {
      if (*selection < 0) *selection = 0;
      if (*selection >= count) *selection = count - 1;
    }
    menu_layer_reload_data(s_native_menu_layer);
    MenuIndex selected = (MenuIndex) {.section = 0, .row = *selection};
    APP_LOG(APP_LOG_LEVEL_INFO, "[MenuSync] target_row=%d widget_row_before=%d",
            selected.row, (int) menu_layer_get_selected_index(s_native_menu_layer).row);
    // Jumping straight to the restored row with menu_layer_set_selected_index() can
    // silently fail to move the viewport when returning to a list at the row it was
    // left on (observed on-device: the row highlight ends up off-screen and the list
    // renders scrolled to some stale position, only fixed by a Down/Up press) --
    // apparently because that call is a same-row no-op when the widget's own
    // leftover selection already matches the row we're restoring. A single-row step
    // via menu_layer_set_selected_next() is never a same-row no-op, so it always
    // makes the widget recompute real geometry/scroll from its own get_cell_height()
    // callback; walk to the target row that way, forcing at least one real step even
    // when we're already "at" it, and land the final step with the real
    // alignment/animation.
    if (count > 0) {
      MenuIndex current = menu_layer_get_selected_index(s_native_menu_layer);
      if (count > 1 && current.row == selected.row) {
        bool bounce_up = selected.row > 0;
        menu_layer_set_selected_next(s_native_menu_layer, bounce_up, MenuRowAlignNone, false);
        current = menu_layer_get_selected_index(s_native_menu_layer);
      }
      int guard = count + 1;
      while (current.row != selected.row && guard-- > 0) {
        bool up = selected.row < current.row;
        bool last_step = (current.row + (up ? -1 : 1)) == selected.row;
        // MenuRowAlignCenter + animated on the final step is exactly what the
        // stock firmware's own menu Up/Down handlers use; intermediate steps are
        // instant teleports that only the final state of is ever drawn.
        menu_layer_set_selected_next(s_native_menu_layer, up,
                                     last_step ? MenuRowAlignCenter : MenuRowAlignNone,
                                     last_step ? animated : false);
        current = menu_layer_get_selected_index(s_native_menu_layer);
      }
    }
    // Only set the index directly when the walk could not have landed on the target
    // (count <= 1, or the guard gave out). Doing it unconditionally re-ran the
    // selection highlight/scroll animation a second time per sync — the selection
    // path animates even when the row is unchanged — which made repeated presses
    // look stuttery next to stock menus.
    if (count <= 1 ||
        menu_layer_get_selected_index(s_native_menu_layer).row != selected.row) {
      menu_layer_set_selected_index(s_native_menu_layer,
                                    selected,
                                    MenuRowAlignCenter,
                                    animated);
    }
    APP_LOG(APP_LOG_LEVEL_INFO, "[MenuSync] widget_row_after=%d",
            (int) menu_layer_get_selected_index(s_native_menu_layer).row);
  } else {
    layer_set_hidden(menu_layer, true);
    if (s_overlay_canvas) {
      layer_set_hidden(s_overlay_canvas, false);
      // Bespoke lists own their selection highlight, so the canvas has to repaint
      // whenever the selection moves - the MenuLayer is no longer doing it for us.
      layer_mark_dirty(s_overlay_canvas);
    }
    if (s_overlay_visible) {
      window_set_click_config_provider(s_overlay_window, click_config_provider);
    }
  }
}

// Moves the native MenuLayer's selection one row in `dir` (-1 = up, +1 = down),
// wrapping at the ends. Per-step movement uses the exact call the stock firmware's
// own menu Up/Down handlers use — menu_layer_set_selected_next(MenuRowAlignCenter,
// animated) — so the highlight and scroll animations are indistinguishable from
// built-in menus. The wrap jump is instant (no stock menu wraps, so there is no
// stock animation to match; a slow glide across the whole list reads as a bug).
// sync_native_menu() is deliberately NOT called here: it reloads menu data (which
// cancels any in-flight selection animation) and is meant for screen restores,
// not per-press scrolling.
static void native_menu_scroll_step(int dir) {
  if (!s_native_menu_layer) return;
  int *selection = native_menu_selection_ptr();
  int count = native_menu_item_count();
  if (count <= 0) return;
  MenuIndex current = menu_layer_get_selected_index(s_native_menu_layer);
  if (dir < 0 && current.row <= 0) {
    *selection = count - 1;
    menu_layer_set_selected_index(s_native_menu_layer, MenuIndex(0, count - 1),
                                  MenuRowAlignBottom, false);
    return;
  }
  if (dir > 0 && current.row >= count - 1) {
    *selection = 0;
    menu_layer_set_selected_index(s_native_menu_layer, MenuIndex(0, 0),
                                  MenuRowAlignTop, false);
    return;
  }
  menu_layer_set_selected_next(s_native_menu_layer, dir < 0, MenuRowAlignCenter, true);
  *selection = menu_layer_get_selected_index(s_native_menu_layer).row;
}

static void draw_native_menu(GContext *ctx, const char *title, const char *const *items,
                             int item_count) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);

  // Pixel-based smooth scrolling: rows are laid out at a fixed pitch and shifted
  // by the shared scroll offset, which glides to keep the selection on screen.
  const int row_pitch = 44;
  const int list_top = 35;
  const int viewport_h = 228 - list_top;
  int offset = scroll_list_layout(row_pitch, item_count, s_menu_selection,
                                  list_top, viewport_h, true);
  for (int i = 0; i < item_count; i++) {
    int y = list_top + i * row_pitch - offset;
    if (y + 40 < list_top || y > 228) continue;  // Skip rows outside the viewport.
    bool selected = i == s_menu_selection;
    if (selected) {
      graphics_context_set_fill_color(ctx, accent_color());
      graphics_fill_rect(ctx, GRect(5, y, 190, 40), 3, GCornersAll);
    }
    draw_text(ctx, items[i], fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
              selected ? GColorWhite : GColorBlack, GRect(14, y + 7, 168, 24),
              GTextAlignmentLeft);
    draw_text(ctx, ">", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
              selected ? GColorWhite : GColorDarkGray, GRect(176, y + 7, 14, 24),
              GTextAlignmentRight);
  }
  // Header drawn last so rows scrolled above it are covered.
  graphics_context_set_fill_color(ctx, accent_color());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 31), 0, GCornerNone);
  draw_text(ctx, title, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GColorWhite,
            GRect(9, 4, 182, 24), GTextAlignmentLeft);
  draw_scrollbar(ctx, GRect(195, 35, 3, 189), offset, viewport_h,
                 viewport_h + s_scroll_max, GColorLightGray, accent_color());
}

static const char *library_items_title(void) {
  switch (s_library_type) {
    case LibraryRecent: return "RECENTLY PLAYED";
    case LibraryCached: return "CACHED MUSIC";
    case LibraryFavorites: return "FAVORITES";
    case LibraryPlaylists: return "PLAYLISTS";
    case LibraryContinue: return "CONTINUE";
    case LibraryRecentSearches: return "RECENT SEARCHES";
    default: return "LIBRARY";
  }
}

static const char *library_items_empty(void) {
  switch (s_library_type) {
    case LibraryRecent: return "No listening history";
    case LibraryCached: return "No cached songs";
    case LibraryFavorites: return "No favorites yet";
    case LibraryPlaylists: return "No playlists yet";
    case LibraryContinue: return "Nothing to continue";
    case LibraryRecentSearches: return "No recent searches";
    default: return "Nothing here yet";
  }
}

// What each Library section holds. These are descriptions, not counts: the watch only
// learns a section's size once it has asked the phone for that section, so a live count
// on this screen would be a number we do not have yet.
static const char *const LIBRARY_HINTS[] = {
  "What you played last",
  "Saved to phone",
  "Songs you hearted",
  "Made on the phone",
  "Pick up where you left off",
  "Run a search again",
};

static void draw_library_bespoke(GContext *ctx) {
  const int row_pitch = BESPOKE_ROW_PITCH;
  const int row_h = BESPOKE_ROW_H;
  int count = library_item_count();
  int offset = scroll_list_layout(row_pitch, count, s_menu_selection,
                                  BESPOKE_LIST_TOP, BESPOKE_VIEWPORT_H, true);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  for (int i = 0; i < count; i++) {
    int y = BESPOKE_LIST_TOP + i * row_pitch - offset;
    if (y + row_h < BESPOKE_LIST_TOP || y > BESPOKE_FOOTER_TOP) continue;
    bespoke_row2(ctx, y, row_h, LIBRARY_ITEMS[i], LIBRARY_HINTS[i],
                 i == s_menu_selection, GColorBlack);
  }
  bespoke_frame(ctx, "LIBRARY", "SELECT open");
  bespoke_scrollbar(ctx, offset);
}

static void draw_library_items_bespoke(GContext *ctx) {
  if (s_library_loading) {
    bespoke_empty(ctx, library_items_title(), "Loading...", NULL, NULL);
    return;
  }
  if (s_result_count == 0) {
    bespoke_empty(ctx, library_items_title(), library_items_empty(), NULL, "BACK close");
    return;
  }
  bespoke_song_list(ctx, library_items_title(),
                    s_library_type == LibraryRecentSearches ? "SELECT search    BACK close"
                                                            : "SELECT play    BACK close",
                    bespoke_now_playing_id());
}

static void draw_library_items(GContext *ctx) {
  if (s_bespoke_ui) {
    draw_library_items_bespoke(ctx);
    return;
  }
  if (screen_uses_native_menu(ScreenLibraryItems)) return;
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, accent_color());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 31), 0, GCornerNone);
  draw_text(ctx, library_items_title(), fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
            GColorWhite, GRect(8, 4, 184, 24), GTextAlignmentLeft);
  if (s_library_loading) {
    draw_status_mascot(ctx, GPoint(76, 66));
    draw_text(ctx, "Loading...", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
              GColorBlack, GRect(10, 125, 180, 26), GTextAlignmentCenter);
    return;
  }
  // Only loading/empty reach the canvas in stock style: screen_uses_native_menu()
  // hands every populated library list to the MenuLayer, so there is no row-drawing
  // path here to keep.
  draw_status_mascot(ctx, GPoint(76, 58));
  draw_text(ctx, library_items_empty(), fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
            GColorBlack, GRect(10, 116, 180, 28), GTextAlignmentCenter);
}

// Queue screen: reachable from the Now Playing "More" popup. Styled after the
// redesigned Now Playing screens (chromeless white canvas, small dark-gray eyebrow
// label instead of a colored header bar) rather than the older accent-header list
// style draw_library_items/draw_native_menu use, per the visual direction those were
// redesigned toward. The currently-playing row is picked out in accent_color() (even
// when not selected) so "what's now" stays visible while scrolling "what's next".
// Queue is the screen the rest of the bespoke UI was derived from, so its design is
// deliberately unchanged: the populated list goes through the shared row renderer
// (byte-identical output), and the loading/empty states keep their mascot rather than
// adopting the flatter bespoke_empty() used elsewhere.
static void draw_queue(GContext *ctx) {
  // With Bespoke UI off, a populated queue is rendered by the native MenuLayer instead,
  // same as Cached Music; this only handles the loading/empty states then.
  if (screen_uses_native_menu(ScreenQueue)) return;
  if (s_queue_loading || s_result_count == 0) {
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
    bespoke_eyebrow(ctx, "QUEUE");
    draw_status_mascot(ctx, GPoint(76, s_queue_loading ? 76 : 68));
    draw_text(ctx, s_queue_loading ? "Loading..." : "Nothing queued",
              fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GColorBlack,
              GRect(10, s_queue_loading ? 135 : 126, 180, 28), GTextAlignmentCenter);
    return;
  }
  bespoke_song_list(ctx, "QUEUE", "SELECT jump    BACK close", bespoke_now_playing_id());
}

static int settings_item_count(void) {
  return 6;
}

static int advanced_item_count(void) {
  // Only Keyboard (index 0) is strictly needed day-to-day; everything else stays
  // hidden until unlocked from About (7x SELECT on VERSION) - see
  // s_advanced_unlocked/s_about_select_count.
  return s_advanced_unlocked ? (int) ARRAY_LENGTH(ADVANCED_ITEMS) : 1;
}

// Library shows Recently Played + Cached Music + Favorites + Playlists by default;
// the remaining sections (Continue / Recent Searches) appear when Library extras is
// enabled. Rows map through LIBRARY_TYPES, so the default count must keep to
// LIBRARY_ITEMS order.
static int library_item_count(void) {
  return s_extra_library ? (int) ARRAY_LENGTH(LIBRARY_ITEMS) : 4;
}

static int current_menu_item_count(void) {
  if (s_screen == ScreenLibrary) return library_item_count();
  if (s_screen == ScreenMenu) return (int) ARRAY_LENGTH(MENU_ITEMS);
  if (s_screen == ScreenInputChoice) return 2;
  if (s_screen == ScreenSearchType) return (int) ARRAY_LENGTH(SEARCH_TYPE_ITEMS);
  if (s_screen == ScreenAdvanced) return advanced_item_count();
  return settings_item_count();
}

// Parallel to the Settings row order used by native_menu_item_title() and the SELECT
// handler: Input, Output, Watch volume, Phone volume, Progress bar, Advanced.
static const char *const SETTINGS_ITEMS[] = {
  "Input", "Output", "Watch volume", "Phone volume", "Progress bar", "Advanced",
};

static const char *settings_value(int index) {
  static char buf[8];
  switch (index) {
    case 0: return s_input_mode == InputVoice ? "Voice"
                 : s_input_mode == InputKeyboard ? "Keyboard" : "Ask";
    case 1: return s_phone_audio ? "Phone" : "Watch";
    case 2: snprintf(buf, sizeof(buf), "%d%%", s_watch_volume); return buf;
    case 3: snprintf(buf, sizeof(buf), "%d%%", s_phone_volume); return buf;
    case 4: return s_show_progress ? "Show" : "Hide";
    default: return NULL;   // Advanced is a link onward, not a value.
  }
}

static void draw_settings_bespoke(GContext *ctx) {
  const int row_pitch = BESPOKE_ROW_PITCH;
  const int row_h = BESPOKE_ROW_H;
  int count = settings_item_count();
  int offset = scroll_list_layout(row_pitch, count, s_menu_selection,
                                  BESPOKE_LIST_TOP, BESPOKE_VIEWPORT_H, true);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  for (int i = 0; i < count; i++) {
    int y = BESPOKE_LIST_TOP + i * row_pitch - offset;
    if (y + row_h < BESPOKE_LIST_TOP || y > BESPOKE_FOOTER_TOP) continue;
    bespoke_row1(ctx, y, row_h, SETTINGS_ITEMS[i], settings_value(i), i == s_menu_selection);
  }
  bespoke_frame(ctx, "SETTINGS", "SELECT change");
  bespoke_scrollbar(ctx, offset);
}

// Grouped Advanced list: section headers break twelve settings into Interface /
// Library / Audio. Headers are not selectable, so the layout is measured first to find
// where the selected row actually sits before scrolling to it.
static void draw_advanced_bespoke(GContext *ctx) {
  const int header_h = 24;
  const int row_pitch = BESPOKE_ROW_PITCH;
  const int row_h = BESPOKE_ROW_H;
  int count = advanced_item_count();
  int content_h = 0, sel_top = 0, sel_bottom = row_pitch;
  for (unsigned g = 0; g < ARRAY_LENGTH(ADVANCED_GROUPS); g++) {
    const AdvancedGroup *grp = &ADVANCED_GROUPS[g];
    if (grp->first >= count) break;
    content_h += header_h;
    for (int i = grp->first; i < grp->first + grp->count && i < count; i++) {
      if (i == s_menu_selection) {
        sel_top = content_h;
        sel_bottom = content_h + row_pitch;
      }
      content_h += row_pitch;
    }
  }
  int offset = bespoke_scroll(content_h, sel_top, sel_bottom);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  int y = BESPOKE_LIST_TOP - offset;
  for (unsigned g = 0; g < ARRAY_LENGTH(ADVANCED_GROUPS); g++) {
    const AdvancedGroup *grp = &ADVANCED_GROUPS[g];
    if (grp->first >= count) break;
    if (y + header_h > BESPOKE_LIST_TOP && y < BESPOKE_FOOTER_TOP) {
      draw_text(ctx, grp->label, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                GColorDarkGray, GRect(18, y + 2, 164, 16), GTextAlignmentLeft);
    }
    y += header_h;
    for (int i = grp->first; i < grp->first + grp->count && i < count; i++) {
      if (y + row_h > BESPOKE_LIST_TOP && y < BESPOKE_FOOTER_TOP) {
        bespoke_row1(ctx, y, row_h, ADVANCED_ITEMS[i], advanced_value(i),
                     i == s_menu_selection);
      }
      y += row_pitch;
    }
  }
  bespoke_frame(ctx, "ADVANCED", "SELECT change");
  bespoke_scrollbar(ctx, offset);
}

static void draw_settings(GContext *ctx) {
  if (s_bespoke_ui) {
    draw_settings_bespoke(ctx);
    return;
  }
  char route[32];
  char watch_volume[32];
  char phone_volume[32];
  char input_mode[32];
  char progress_bar[32];
  char advanced[24];
  snprintf(route, sizeof(route), "Output: %s", s_phone_audio ? "Phone" : "Watch");
  snprintf(watch_volume, sizeof(watch_volume), "Watch volume: %d%%", s_watch_volume);
  snprintf(phone_volume, sizeof(phone_volume), "Phone volume: %d%%", s_phone_volume);
  snprintf(input_mode, sizeof(input_mode), "Input: %s",
           s_input_mode == InputVoice ? "Voice" :
           s_input_mode == InputKeyboard ? "Keyboard" : "Ask");
  snprintf(progress_bar, sizeof(progress_bar), "Progress bar: %s",
           s_show_progress ? "Show" : "Hide");
  snprintf(advanced, sizeof(advanced), "Advanced");
  const char *items[] = {input_mode, route, watch_volume, phone_volume, progress_bar, advanced};
  draw_native_menu(ctx, "SETTINGS", items, settings_item_count());
}

static void draw_advanced(GContext *ctx) {
  if (s_bespoke_ui) {
    draw_advanced_bespoke(ctx);
    return;
  }
  char rows[ARRAY_LENGTH(ADVANCED_ITEMS)][40];
  const char *items[ARRAY_LENGTH(ADVANCED_ITEMS)];
  int count = advanced_item_count();
  for (int i = 0; i < count; i++) {
    snprintf(rows[i], sizeof(rows[i]), "%s: %s", ADVANCED_ITEMS[i], advanced_value(i));
    items[i] = rows[i];
  }
  draw_native_menu(ctx, "ADVANCED", items, count);
}

static void draw_input_choice(GContext *ctx) {
  static const char *const items[] = {"Voice search", "Keyboard"};
  if (s_bespoke_ui) {
    // No variant was picked for this two-row screen, so it takes the plain Menu shape
    // rather than being left as the one stock list in a bespoke app.
    bespoke_ground(ctx, "SEARCH WITH");
    for (int i = 0; i < (int) ARRAY_LENGTH(items); i++) {
      bespoke_row1(ctx, BESPOKE_LIST_TOP + i * BESPOKE_ROW_PITCH, BESPOKE_ROW_H,
                   items[i], NULL, i == s_menu_selection);
    }
    bespoke_frame(ctx, "SEARCH WITH", "SELECT choose");
    return;
  }
  draw_native_menu(ctx, "SEARCH WITH", items, ARRAY_LENGTH(items));
}

// Artist Radio and Song Radio are not self-explanatory; the second line says what each
// will actually queue up.
static const char *const SEARCH_TYPE_HINTS[] = {
  "Find one track",
  "An artist's catalogue",
  "Similar songs, in order",
};

static void draw_search_type(GContext *ctx) {
  if (s_bespoke_ui) {
    // A page of the Now Playing "More" popup, pixel-for-pixel: eyebrow at (18,18),
    // 28px name at y=54, gray 18px description at y=96, and the circular icon row at
    // cy=158 / 30px pitch with the selection in an accent disc. UP/DOWN page between
    // modes (s_menu_selection already cycles via the shared menu handlers); SELECT
    // chooses. Nothing here scrolls, so no bespoke_ground/frame stamping.
    const int count = (int) ARRAY_LENGTH(SEARCH_TYPE_ITEMS);
    int sel = s_menu_selection;
    if (sel < 0) sel = 0;
    if (sel >= count) sel = count - 1;

    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
    draw_text(ctx, "SEARCH", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GColorBlack,
              GRect(18, 18, 164, 18), GTextAlignmentLeft);
    draw_text(ctx, SEARCH_TYPE_ITEMS[sel], fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
              GColorBlack, GRect(18, 54, 164, 40), GTextAlignmentLeft);
    // 18px descriptive text keeps the gray (like the More popup's state value) so the
    // big name stays on top of the hierarchy; only 14px text goes full black.
    graphics_context_set_text_color(ctx, GColorDarkGray);
    graphics_draw_text(ctx, SEARCH_TYPE_HINTS[sel],
                       fonts_get_system_font(FONT_KEY_GOTHIC_18),
                       GRect(18, 96, 164, 48), GTextOverflowModeWordWrap,
                       GTextAlignmentLeft, NULL);
    const int cy = 158;
    const int icon_pitch = 30;
    for (int i = 0; i < count; i++) {
      const int cx = 100 + (2 * i - (count - 1)) * icon_pitch / 2;
      GColor glyph = GColorBlack;
      if (i == sel) {
        graphics_context_set_fill_color(ctx, accent_color());
        graphics_fill_circle(ctx, GPoint(cx, cy), 17);
        glyph = GColorWhite;
      }
      const GPoint gc = GPoint(cx, cy);
      switch (i) {
        case 0: draw_note_icon(ctx, gc, glyph); break;
        case 1: draw_person_icon(ctx, gc, glyph); break;
        default: draw_broadcast_icon(ctx, gc, glyph); break;
      }
    }
    draw_text(ctx, "SELECT choose    BACK close", fonts_get_system_font(FONT_KEY_GOTHIC_14),
              GColorBlack, GRect(8, 202, 184, 18), GTextAlignmentCenter);
    return;
  }
  draw_native_menu(ctx, "SEARCH", SEARCH_TYPE_ITEMS, ARRAY_LENGTH(SEARCH_TYPE_ITEMS));
}

static const char *const ACK_NAMES[] = {
  "Metrolist", "PipePipe Extractor", "PebbleKit / Rebble", "mirrormsg/watchimage",
  "PixelPlayer", "Bobby Assistant", "Tertiary Text",
};
static const char *const ACK_DESC[] = {
  "Music client foundation - GPLv3",
  "Search and stream extraction - GPLv3",
  "Watch and Android communication",
  "Official cover-art pipeline - AGPLv3",
  "Companion player UI - MIT",
  "Home interaction inspiration - Apache-2.0",
  "Three-button keyboard - MIT",
};

// Spec rows over the acknowledgements. The credits are licence attribution, so they
// stay in full and this screen keeps scrolling even though the rows above it do not.
static void draw_about_bespoke(GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  int y = BESPOKE_LIST_TOP - s_scroll;
  bespoke_row1(ctx, y, BESPOKE_ROW_H, "Version", "0.2.0", false);
  y += BESPOKE_ROW_PITCH;
  bespoke_row1(ctx, y, BESPOKE_ROW_H, "Watch", "Emery", false);
  y += BESPOKE_ROW_PITCH;
  bespoke_row1(ctx, y, BESPOKE_ROW_H, "Bridge", s_bridge_ready ? "Ready" : "Offline", false);
  y += BESPOKE_ROW_PITCH + 8;
  draw_text(ctx, "ACKNOWLEDGEMENTS", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
            GColorDarkGray, GRect(18, y, 164, 16), GTextAlignmentLeft);
  y += 22;
  for (unsigned i = 0; i < ARRAY_LENGTH(ACK_NAMES); i++) {
    draw_text(ctx, ACK_NAMES[i], fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GColorBlack,
              GRect(18, y, 164, 18), GTextAlignmentLeft);
    draw_text(ctx, ACK_DESC[i], fonts_get_system_font(FONT_KEY_GOTHIC_14), GColorBlack,
              GRect(18, y + 15, 164, 18), GTextAlignmentLeft);
    y += 37;
  }
  // 'y' is the content bottom in screen space (already offset by -s_scroll), so the
  // maximum scroll is however far past the bottom edge the content currently extends.
  int overflow = (y + 8) - 228 + s_scroll;
  s_scroll_max = overflow > 0 ? overflow : 0;
  bespoke_frame(ctx, "ABOUT", NULL);
  bespoke_scrollbar(ctx, s_scroll);
}

static void draw_about(GContext *ctx) {
  if (s_bespoke_ui) {
    draw_about_bespoke(ctx);
    return;
  }
  ThemeColors c = colors();
  graphics_context_set_fill_color(ctx, c.background);
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  int y = 38 - s_scroll;

  if (s_mascot_modern) {
    graphics_draw_bitmap_in_rect(ctx, current_status_mascot(), GRect(76, y, 48, 48));
  }
  y += 53;
  draw_text(ctx, "dreamwave", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), c.foreground,
            GRect(10, y, 180, 30), GTextAlignmentCenter);
  y += 30;
  draw_text(ctx, "Music made for Pebble", fonts_get_system_font(FONT_KEY_GOTHIC_18), c.secondary,
            GRect(10, y, 180, 24), GTextAlignmentCenter);
  y += 26;
  draw_text(ctx, s_bridge_ready ? "Phone bridge connected" : "Phone bridge unavailable",
            fonts_get_system_font(FONT_KEY_GOTHIC_14), c.secondary,
            GRect(10, y, 180, 22), GTextAlignmentCenter);
  y += 30;

  graphics_context_set_fill_color(ctx, c.surface);
  graphics_fill_rect(ctx, GRect(20, y, 160, 46), 4, GCornersAll);
  draw_text(ctx, "VERSION", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), c.secondary,
             GRect(31, y + 5, 138, 16), GTextAlignmentCenter);
  draw_text(ctx, "0.1.0", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
             GColorBlack,
             GRect(31, y + 20, 138, 22), GTextAlignmentCenter);
  y += 57;

  graphics_context_set_stroke_color(ctx, c.surface);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(16, y), GPoint(184, y));
  y += 8;

  draw_text(ctx, "ACKNOWLEDGEMENTS", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
            c.accent, GRect(10, y, 180, 25), GTextAlignmentCenter);
  y += 24;
  for (unsigned i = 0; i < ARRAY_LENGTH(ACK_NAMES); i++) {
    draw_text(ctx, ACK_NAMES[i], fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), c.foreground,
              GRect(12, y, 176, 18), GTextAlignmentLeft);
    draw_text(ctx, ACK_DESC[i], fonts_get_system_font(FONT_KEY_GOTHIC_14), c.secondary,
              GRect(12, y + 16, 176, 18), GTextAlignmentLeft);
    y += 39;
  }

  // 'y' is the bottom of the content in screen space (already offset by -s_scroll).
  // The maximum scroll is however far past the bottom edge the content currently extends.
  int overflow = (y + 8) - 228 + s_scroll;  // 8px bottom padding.
  s_scroll_max = overflow > 0 ? overflow : 0;

  graphics_context_set_fill_color(ctx, c.accent);
  graphics_fill_rect(ctx, GRect(0, 0, 200, 31), 0, GCornerNone);
  draw_text(ctx, "ABOUT", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GColorWhite,
             GRect(8, 4, 184, 24), GTextAlignmentLeft);
  // Pixel-based scrollbar: the viewport is the visible area below the header and
  // the content extent is that viewport plus however far the content overflows.
  const int32_t viewport = 228 - 31;
  draw_scrollbar(ctx, GRect(194, 35, 4, 189), s_scroll, viewport,
                 viewport + s_scroll_max, c.surface, c.accent);
}

static const char *keyboard_characters(void) {
  if (s_keyboard_mode == 1) return KEYBOARD_UPPER;
  if (s_keyboard_mode == 2) return KEYBOARD_SYMBOLS;
  return KEYBOARD_LOWER;
}

static const char *keyboard_mode_label(void) {
  if (s_keyboard_mode == 1) return "UPPER";
  if (s_keyboard_mode == 2) return "SYMBOLS";
  return "LOWER";
}

static void keyboard_option(char *buffer, size_t buffer_size, int choice) {
  const char *characters = keyboard_characters();
  int option_size = s_keyboard_size / 3;
  int option_start = s_keyboard_start + choice * option_size;
  int copy_size = option_size < (int) buffer_size - 1 ? option_size : (int) buffer_size - 1;
  memcpy(buffer, characters + option_start, copy_size);
  buffer[copy_size] = '\0';
  if (copy_size == 1 && buffer[0] == ' ') snprintf(buffer, buffer_size, "SPACE");
}

static void draw_keyboard(GContext *ctx) {
  if (s_keyboard_pt2) {
    const int grid_left = 2;
    const int grid_top = 58;
    const int gap = 2;
    const int cell_w = 64;
    const int cell_h = 54;

    graphics_context_set_fill_color(ctx, GColorLightGray);
    graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);

    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, GRect(2, 2, 196, 53), 4, GCornersAll);
    draw_text(ctx, s_query_length > 0 ? s_query : "Start typing...",
              fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
              s_query_length > 0 ? GColorBlack : GColorDarkGray,
              GRect(8, 8, 184, 39), GTextAlignmentLeft);

    const bool numbers = s_keyboard_mode == 2;
    const bool uppercase = s_keyboard_mode == 1;
    draw_text(ctx, "HELP", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GColorDarkGray,
              GRect(153, 4, 38, 16), GTextAlignmentRight);
    draw_text(ctx, numbers ? "123" : uppercase ? "ABC" : "abc",
              fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), accent_color(),
              GRect(153, 37, 38, 16), GTextAlignmentRight);

    for (int row = 0; row < 3; row++) {
      for (int col = 0; col < 3; col++) {
        int i = row * 3 + col;
        int x = grid_left + col * (cell_w + gap);
        int y = grid_top + row * (cell_h + gap);
        bool dimmed = false;
        bool active = false;
        char display[6];

        bool zero_fan = false;
#ifdef PBL_PLATFORM_EMERY
        // While the "8" cell is held in numbers mode, 8 and 0 fan out like the letter
        // options: 8 onto the "1" key (cell 0), 0 onto the "3" key (cell 2).
        zero_fan = s_touch_active && numbers && s_touch_origin_key == PT2_ZERO_CELL;
        dimmed = s_touch_active;
        active = s_touch_active && i == s_touch_active_key;
        if (s_touch_active && !numbers) {
          display[0] = '\0';
          for (int choice = 0; choice < 3; choice++) {
            if (PT2_TOUCH_MAP[s_touch_origin_key][choice].target == i) {
              char character = PT2_TOUCH_MAP[s_touch_origin_key][choice].character;
              if (uppercase && character >= 'a' && character <= 'z') character -= 'a' - 'A';
              display[0] = character == ' ' ? '_' : character;
              display[1] = '\0';
              dimmed = false;
              break;
            }
          }
        } else if (zero_fan) {
          if (i == 0) { display[0] = '8'; display[1] = '\0'; dimmed = false; }
          else if (i == 2) { display[0] = '0'; display[1] = '\0'; dimmed = false; }
          else { display[0] = '\0'; }
        } else
#endif
        {
          snprintf(display, sizeof(display), "%s",
                   numbers ? PT2_NUMBERS_LABEL[i] : PT2_LETTERS_LABEL[i]);
          if (uppercase && !numbers) {
            for (int j = 0; display[j]; j++) {
              if (display[j] >= 'a' && display[j] <= 'z') display[j] -= 'a' - 'A';
            }
          }
        }

        graphics_context_set_fill_color(ctx, active ? accent_color() : GColorWhite);
        graphics_fill_rect(ctx, GRect(x, y, cell_w, cell_h), 3, GCornersAll);
        if (numbers && i == PT2_ZERO_CELL && !zero_fan) {
          // Idle "8" cell: a large 8 with a small 0 hint (swipe up-right for 0).
          GColor glyph = active ? GColorWhite : dimmed ? GColorLightGray : GColorBlack;
          draw_text(ctx, "8", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), glyph,
                    GRect(x + 2, y + 16, cell_w - 4, 23), GTextAlignmentCenter);
          draw_text(ctx, "0", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                    active ? GColorWhite : GColorLightGray,
                    GRect(x + cell_w - 20, y + 3, 16, 14), GTextAlignmentRight);
        } else {
          draw_text(ctx, display, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                    active ? GColorWhite : dimmed ? GColorLightGray : GColorBlack,
                    GRect(x + 2, y + 16, cell_w - 4, 23), GTextAlignmentCenter);
        }
      }
    }
    draw_feedback_overlay(ctx);
    return;
  }

  // The key column on the right mirrors the home/playing action-bar sidebar:
  // resting/pressed colors follow the current theme (white-on-black rail for
  // the Mono theme, black-on-white for the others).
  ThemeColors c = colors();
  const int bar_x = 144;
  const int segment_height = 76;

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, accent_color());
  graphics_fill_rect(ctx, GRect(0, 0, bar_x, 31), 0, GCornerNone);
  draw_text(ctx, "TYPE SEARCH", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GColorWhite,
            GRect(8, 4, bar_x - 14, 24), GTextAlignmentLeft);

  draw_text(ctx, s_query_length > 0 ? s_query : "Start typing...",
            fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
            s_query_length > 0 ? GColorBlack : GColorDarkGray,
            GRect(8, 39, bar_x - 16, 126), GTextAlignmentLeft);

  // Sidebar: mirrors the action bar's resting background.
  graphics_context_set_fill_color(ctx, c.action_bar_bg);
  graphics_fill_rect(ctx, GRect(bar_x, 0, 200 - bar_x, 228), 0, GCornerNone);

  // Highlight the pressed segment, mirroring the action bar's press colors.
  const ButtonId seg_buttons[] = {BUTTON_ID_UP, BUTTON_ID_SELECT, BUTTON_ID_DOWN};
  int pressed_row = -1;
  for (int i = 0; i < 3; i++) {
    if (s_button_pressed && s_pressed_button == seg_buttons[i]) pressed_row = i;
  }
  if (pressed_row >= 0) {
    graphics_context_set_fill_color(ctx, c.action_bar_press_bg);
    graphics_fill_rect(ctx, GRect(bar_x + 1, pressed_row * segment_height, 200 - bar_x, segment_height),
                       0, GCornerNone);
  }

  graphics_context_set_stroke_color(ctx, c.action_bar_icon);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(bar_x, 0), GPoint(bar_x, 228));
  graphics_draw_line(ctx, GPoint(bar_x, segment_height), GPoint(199, segment_height));
  graphics_draw_line(ctx, GPoint(bar_x, segment_height * 2), GPoint(199, segment_height * 2));

  const char *characters = keyboard_characters();
  if (s_keyboard_size == 27) {
    for (int row = 0; row < 3; row++) {
      GColor key_color = row == pressed_row ? c.action_bar_press_icon : c.action_bar_icon;
      int group_start = row * 9;
      for (int column = 0; column < 3; column++) {
        char group[4];
        memcpy(group, characters + group_start + column * 3, 3);
        group[3] = '\0';
        draw_text(ctx, group, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), key_color,
                  GRect(bar_x + 4, row * segment_height + 8 + column * 18, 48, 18),
                  GTextAlignmentCenter);
      }
    }
  } else {
    char option[3][12];
    for (int row = 0; row < 3; row++) {
      GColor key_color = row == pressed_row ? c.action_bar_press_icon : c.action_bar_icon;
      keyboard_option(option[row], sizeof(option[row]), row);
      draw_text(ctx, option[row], fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), key_color,
                GRect(bar_x + 3, row * segment_height + 23, 50, 32), GTextAlignmentCenter);
    }
  }

  char footer[40];
  snprintf(footer, sizeof(footer), "%s  %d/80", keyboard_mode_label(), s_query_length);
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, GRect(5, 177, bar_x - 10, 23), 4, GCornersAll);
  draw_text(ctx, footer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GColorBlack,
            GRect(8, 180, bar_x - 16, 18), GTextAlignmentCenter);
  draw_text(ctx, s_keyboard_size == 27 ? "BACK: DELETE / EXIT" : "BACK: PREVIOUS",
            fonts_get_system_font(FONT_KEY_GOTHIC_14), GColorDarkGray,
            GRect(5, 204, bar_x - 10, 18), GTextAlignmentCenter);
}

static void draw_placeholder_screen(GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  draw_header(ctx, s_placeholder_title);
  draw_status_mascot(ctx, GPoint(76, 62));
  draw_text(ctx, s_placeholder_message, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
            GColorBlack, GRect(18, 157, 164, 48), GTextAlignmentCenter);
}

static void draw_searching(GContext *ctx) {
  ThemeColors c = colors();
  draw_header(ctx, "PHONE LINK");
  draw_text(ctx, "SEARCHING FOR", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
            c.secondary, GRect(10, 31, 180, 19), GTextAlignmentLeft);
  draw_text(ctx, s_query, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), c.foreground,
            GRect(10, 52, 180, 70), GTextAlignmentLeft);
  for (int i = 0; i < 7; i++) {
    int distance = (i - (s_animation_frame % 7) + 7) % 7;
    int radius = distance == 0 ? 6 : distance == 6 ? 4 : 3;
    graphics_context_set_fill_color(ctx, distance <= 1 || distance == 6 ? c.accent : c.surface);
    graphics_fill_circle(ctx, GPoint(49 + i * 17, 180), radius);
  }
  draw_text(ctx, "Android is finding audio", fonts_get_system_font(FONT_KEY_GOTHIC_14),
            c.foreground, GRect(10, 199, 180, 22), GTextAlignmentCenter);
}

static void draw_results_bespoke(GContext *ctx) {
  if (s_result_count == 0) {
    bespoke_empty(ctx, "SEARCH RESULTS", "No songs found",
                  "Select to search again", "SELECT search");
    return;
  }
  bespoke_song_list(ctx, "SEARCH RESULTS", "SELECT play    BACK close",
                    bespoke_now_playing_id());
}

static void draw_modern_results(GContext *ctx) {
  if (s_bespoke_ui) {
    draw_results_bespoke(ctx);
    return;
  }
  // Only the empty state reaches the canvas in stock style: screen_uses_native_menu()
  // hands every populated result list to the MenuLayer, so there is no row-drawing
  // path here to keep.
  if (screen_uses_native_menu(ScreenResults)) return;
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, accent_color());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 31), 0, GCornerNone);
  draw_text(ctx, "SEARCH RESULTS", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
            GColorWhite, GRect(8, 4, 184, 24), GTextAlignmentLeft);
  draw_text(ctx, s_query, fonts_get_system_font(FONT_KEY_GOTHIC_14), GColorDarkGray,
            GRect(9, 34, 182, 20), GTextAlignmentLeft);
  draw_status_mascot(ctx, GPoint(76, 55));
  draw_text(ctx, "No songs found", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
            GColorBlack, GRect(16, 109, 168, 34), GTextAlignmentCenter);
  draw_text(ctx, "Select to search again", fonts_get_system_font(FONT_KEY_GOTHIC_14),
            GColorDarkGray, GRect(10, 148, 180, 22), GTextAlignmentCenter);
}

static void draw_modern_buffering(GContext *ctx) {
  const SearchResult *result = current_playing_result();
  if (!result) return;
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  // Chromeless, on the pager grid: eyebrow, track title in the 28px slot, artist in
  // the 18px description slot, and the equalizer bars centered on the icon line
  // (cy=158) - so the handoff to Now Playing reads as one screen fading into the next.
  draw_text(ctx, "FINDING AUDIO", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
            GColorBlack, GRect(18, 18, 164, 18), GTextAlignmentLeft);
  draw_text(ctx, result->title, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
            GColorBlack, GRect(18, 54, 164, 40), GTextAlignmentLeft);
  draw_text(ctx, result->artist, fonts_get_system_font(FONT_KEY_GOTHIC_18),
            GColorDarkGray, GRect(18, 96, 164, 24), GTextAlignmentLeft);
  for (int i = 0; i < 5; i++) {
    int phase = (s_animation_frame + i) % 5;
    int height = 12 + (phase <= 2 ? phase : 4 - phase) * 12;
    graphics_context_set_fill_color(ctx, accent_color());
    graphics_fill_rect(ctx, GRect(55 + i * 20, 158 - height / 2, 10, height), 3, GCornersAll);
  }
  draw_text(ctx, "BACK cancel", fonts_get_system_font(FONT_KEY_GOTHIC_14),
            GColorBlack, GRect(8, 202, 184, 18), GTextAlignmentCenter);
}

static void draw_results(GContext *ctx) {
  if (screen_uses_native_menu(ScreenResults)) return;
  draw_modern_results(ctx);
}

static void draw_buffering(GContext *ctx) {
  draw_modern_buffering(ctx);
}

static void draw_repeat_icon(GContext *ctx, GPoint center, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(center.x - 8, center.y - 5),
                     GPoint(center.x + 7, center.y - 5));
  graphics_draw_line(ctx, GPoint(center.x + 7, center.y - 5),
                     GPoint(center.x + 3, center.y - 9));
  graphics_draw_line(ctx, GPoint(center.x + 7, center.y - 5),
                     GPoint(center.x + 3, center.y - 1));
  graphics_draw_line(ctx, GPoint(center.x + 8, center.y + 5),
                     GPoint(center.x - 7, center.y + 5));
  graphics_draw_line(ctx, GPoint(center.x - 7, center.y + 5),
                     GPoint(center.x - 3, center.y + 1));
  graphics_draw_line(ctx, GPoint(center.x - 7, center.y + 5),
                     GPoint(center.x - 3, center.y + 9));
}

static void draw_shuffle_icon(GContext *ctx, GPoint center, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(center.x - 8, center.y - 6),
                     GPoint(center.x + 5, center.y + 6));
  graphics_draw_line(ctx, GPoint(center.x + 5, center.y + 6),
                     GPoint(center.x + 2, center.y + 2));
  graphics_draw_line(ctx, GPoint(center.x + 5, center.y + 6),
                     GPoint(center.x + 1, center.y + 8));

  graphics_draw_line(ctx, GPoint(center.x - 8, center.y + 6),
                     GPoint(center.x - 1, center.y + 6));
  graphics_draw_line(ctx, GPoint(center.x + 2, center.y - 6),
                     GPoint(center.x + 8, center.y - 6));
  graphics_draw_line(ctx, GPoint(center.x + 8, center.y - 6),
                     GPoint(center.x + 5, center.y - 10));
  graphics_draw_line(ctx, GPoint(center.x + 8, center.y - 6),
                     GPoint(center.x + 4, center.y - 4));
}

static void draw_skip_icon(GContext *ctx, GPoint center, GColor color, bool next) {
  graphics_context_set_fill_color(ctx, color);
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  if (next) {
    GPoint tri[] = {
      GPoint(center.x - 6, center.y - 8),
      GPoint(center.x + 4, center.y),
      GPoint(center.x - 6, center.y + 8),
    };
    GPathInfo info = { .num_points = ARRAY_LENGTH(tri), .points = tri };
    GPath *path = gpath_create(&info);
    gpath_draw_filled(ctx, path);
    gpath_destroy(path);
    graphics_fill_rect(ctx, GRect(center.x + 6, center.y - 8, 2, 16), 0, GCornerNone);
  } else {
    GPoint tri[] = {
      GPoint(center.x + 6, center.y - 8),
      GPoint(center.x - 4, center.y),
      GPoint(center.x + 6, center.y + 8),
    };
    GPathInfo info = { .num_points = ARRAY_LENGTH(tri), .points = tri };
    GPath *path = gpath_create(&info);
    gpath_draw_filled(ctx, path);
    gpath_destroy(path);
    graphics_fill_rect(ctx, GRect(center.x - 8, center.y - 8, 2, 16), 0, GCornerNone);
  }
}

// Draws a heart centered at 'center'. 'scale' is roughly half the heart width in
// pixels. When 'filled' the heart is solid; otherwise only its outline is drawn.
static void draw_heart_icon(GContext *ctx, GPoint center, int scale, GColor color, bool filled) {
  // Two lobes (circles) plus a triangular point below, approximated with a filled
  // path so it scales cleanly for both the small sidebar and large feedback sizes.
  int r = scale / 2;
  int lobe_y = center.y - r / 2;
  if (filled) {
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_circle(ctx, GPoint(center.x - r, lobe_y), r);
    graphics_fill_circle(ctx, GPoint(center.x + r, lobe_y), r);
    GPoint tip[] = {
      GPoint(center.x - scale, lobe_y),
      GPoint(center.x + scale, lobe_y),
      GPoint(center.x, center.y + scale),
    };
    GPathInfo info = {.num_points = 3, .points = tip};
    GPath *path = gpath_create(&info);
    gpath_draw_filled(ctx, path);
    gpath_destroy(path);
  } else {
    graphics_context_set_stroke_color(ctx, color);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_circle(ctx, GPoint(center.x - r, lobe_y), r);
    graphics_draw_circle(ctx, GPoint(center.x + r, lobe_y), r);
    graphics_draw_line(ctx, GPoint(center.x - scale, lobe_y), GPoint(center.x, center.y + scale));
    graphics_draw_line(ctx, GPoint(center.x + scale, lobe_y), GPoint(center.x, center.y + scale));
  }
}

// Simple phone outline (rounded rectangle with a speaker slit).
static void draw_phone_icon(GContext *ctx, GPoint center, int scale, GColor color) {
  int w = scale;
  int h = scale * 3 / 2;
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_round_rect(ctx, GRect(center.x - w / 2, center.y - h / 2, w, h), 3);
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, GRect(center.x - w / 6, center.y - h / 2 + 3, w / 3, 2), 1, GCornersAll);
  graphics_fill_circle(ctx, GPoint(center.x, center.y + h / 2 - 4), 2);
}

// Simple watch outline (square face with lugs top and bottom).
static void draw_watch_icon(GContext *ctx, GPoint center, int scale, GColor color) {
  int s = scale;
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_fill_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_round_rect(ctx, GRect(center.x - s / 2, center.y - s / 2, s, s), 3);
  graphics_fill_rect(ctx, GRect(center.x - s / 4, center.y - s / 2 - 4, s / 2, 4), 1, GCornersAll);
  graphics_fill_rect(ctx, GRect(center.x - s / 4, center.y + s / 2, s / 2, 4), 1, GCornersAll);
}

// Draws the transient long-press confirmation overlay (heart / phone / watch)
// centered on the playing screen with a rounded backing card.
static void draw_feedback_overlay(GContext *ctx) {
  if (s_feedback_icon == FeedbackNone) return;
  if (s_feedback_icon == FeedbackKeyboardHint) {
    const int box_w = 180;
    const int box_h = 70;
    GRect card = GRect(100 - box_w / 2, 114 - box_h / 2, box_w, box_h);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, card, 10, GCornersAll);
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_round_rect(ctx, card, 10);
    draw_text(ctx, "Tap for key", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
              GColorWhite, GRect(card.origin.x + 6, card.origin.y + 4, box_w - 12, 16),
              GTextAlignmentCenter);
    draw_text(ctx, "Swipe for letters", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
              GColorWhite, GRect(card.origin.x + 6, card.origin.y + 19, box_w - 12, 16),
              GTextAlignmentCenter);
    draw_text(ctx, "UP mode   SELECT enter",
              fonts_get_system_font(FONT_KEY_GOTHIC_14), GColorWhite,
              GRect(card.origin.x + 6, card.origin.y + 35, box_w - 12, 16), GTextAlignmentCenter);
    draw_text(ctx, "DOWN delete   HELP info",
              fonts_get_system_font(FONT_KEY_GOTHIC_14), GColorWhite,
              GRect(card.origin.x + 6, card.origin.y + 50, box_w - 12, 16), GTextAlignmentCenter);
    return;
  }
  const int box = 72;
  GRect card = GRect(100 - box / 2, 114 - box / 2, box, box);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, card, 10, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_round_rect(ctx, card, 10);
  GPoint c = GPoint(100, 112);
  switch (s_feedback_icon) {
    case FeedbackFavoriteOn:  draw_heart_icon(ctx, c, 16, accent_color(), true); break;
    case FeedbackFavoriteOff: draw_heart_icon(ctx, c, 16, GColorWhite, false); break;
    case FeedbackOutputPhone: draw_phone_icon(ctx, c, 26, GColorWhite); break;
    case FeedbackOutputWatch: draw_watch_icon(ctx, c, 28, GColorWhite); break;
    case FeedbackShuffleOn:   draw_shuffle_icon(ctx, c, accent_color()); break;
    case FeedbackShuffleOff:  draw_shuffle_icon(ctx, c, GColorWhite); break;
    case FeedbackNext:        draw_skip_icon(ctx, c, GColorWhite, true); break;
    case FeedbackPrev:        draw_skip_icon(ctx, c, GColorWhite, false); break;
    case FeedbackPlay: {
      graphics_context_set_fill_color(ctx, GColorWhite);
      GPoint tri[] = { GPoint(c.x - 8, c.y - 12), GPoint(c.x + 12, c.y), GPoint(c.x - 8, c.y + 12) };
      GPathInfo pi = { .num_points = 3, .points = tri };
      GPath *p = gpath_create(&pi);
      gpath_draw_filled(ctx, p);
      gpath_destroy(p);
      break;
    }
    case FeedbackPause:
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_rect(ctx, GRect(c.x - 9, c.y - 12, 6, 24), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(c.x + 3, c.y - 12, 6, 24), 0, GCornerNone);
      break;
    case FeedbackNone:
    case FeedbackKeyboardHint: break;
  }
  const char *label =
      s_feedback_icon == FeedbackFavoriteOn ? "Favorited" :
      s_feedback_icon == FeedbackFavoriteOff ? "Unfavorited" :
      s_feedback_icon == FeedbackShuffleOn ? "Shuffle on" :
      s_feedback_icon == FeedbackShuffleOff ? "Shuffle off" :
      s_feedback_icon == FeedbackOutputPhone ? "Phone" :
      s_feedback_icon == FeedbackOutputWatch ? "Watch" :
      s_feedback_icon == FeedbackPlay ? "Play" :
      s_feedback_icon == FeedbackPause ? "Pause" :
      s_feedback_icon == FeedbackNext ? "Next" :
      s_feedback_icon == FeedbackPrev ? "Previous" : "";
  draw_text(ctx, label, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GColorWhite,
            GRect(card.origin.x - 20, card.origin.y + box + 2, box + 40, 18), GTextAlignmentCenter);
}

static void draw_action_bar_icon(GContext *ctx, ButtonId button, GPoint center, GColor color,
                                 bool paused, int page) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_fill_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);

  if (page == 0) {
    // Page 0: search (UP) / advance (SELECT dots) / loop (DOWN).
    if (button == BUTTON_ID_SELECT) {
      graphics_fill_circle(ctx, GPoint(center.x, center.y - 6), 2);
      graphics_fill_circle(ctx, center, 2);
      graphics_fill_circle(ctx, GPoint(center.x, center.y + 6), 2);
    } else if (button == BUTTON_ID_UP) {
      graphics_draw_circle(ctx, GPoint(center.x - 2, center.y - 2), 7);
      graphics_draw_line(ctx, GPoint(center.x + 3, center.y + 3),
                         GPoint(center.x + 9, center.y + 9));
    } else {
      draw_repeat_icon(ctx, center, color);
    }
    return;
  }

  if (page == 1) {
    // Page 1: previous (UP) / advance (SELECT dots) / next (DOWN).
    if (button == BUTTON_ID_SELECT) {
      graphics_fill_circle(ctx, GPoint(center.x, center.y - 6), 2);
      graphics_fill_circle(ctx, center, 2);
      graphics_fill_circle(ctx, GPoint(center.x, center.y + 6), 2);
    } else if (button == BUTTON_ID_UP) {
      draw_skip_icon(ctx, center, color, false);
    } else {
      draw_skip_icon(ctx, center, color, true);
    }
    return;
  }

  if (page == 2) {
    // Page 2: volume up (UP) / play-pause (SELECT) / volume down (DOWN).
    if (button == BUTTON_ID_SELECT) {
    if (paused) {
      GPoint play[] = {
        GPoint(center.x - 4, center.y - 7),
        GPoint(center.x + 7, center.y),
        GPoint(center.x - 4, center.y + 7),
      };
      GPathInfo path_info = {
        .num_points = ARRAY_LENGTH(play),
        .points = play,
      };
      GPath *path = gpath_create(&path_info);
      gpath_draw_filled(ctx, path);
      gpath_destroy(path);
    } else {
      graphics_fill_rect(ctx, GRect(center.x - 6, center.y - 7, 4, 14), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(center.x + 2, center.y - 7, 4, 14), 0, GCornerNone);
    }
      return;
    }

    graphics_fill_rect(ctx, GRect(center.x - 9, center.y - 3, 4, 6), 0, GCornerNone);
    graphics_draw_line(ctx, GPoint(center.x - 5, center.y - 3),
                       GPoint(center.x, center.y - 7));
    graphics_draw_line(ctx, GPoint(center.x, center.y - 7),
                       GPoint(center.x, center.y + 7));
    graphics_draw_line(ctx, GPoint(center.x, center.y + 7),
                       GPoint(center.x - 5, center.y + 3));
    graphics_draw_line(ctx, GPoint(center.x + 3, center.y),
                       GPoint(center.x + 11, center.y));
    if (button == BUTTON_ID_UP) {
      graphics_draw_line(ctx, GPoint(center.x + 7, center.y - 4),
                         GPoint(center.x + 7, center.y + 4));
    }
  }
}

static void draw_action_bar(GContext *ctx, bool paused) {
  ThemeColors c = colors();
  const int bar_x = 170;
  const int segment_height = 76;
  graphics_context_set_fill_color(ctx, c.action_bar_bg);
  graphics_fill_rect(ctx, GRect(bar_x, 0, 30, 228), 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, c.action_bar_icon);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(bar_x, 0), GPoint(bar_x, 228));

  const ButtonId buttons[] = {BUTTON_ID_UP, BUTTON_ID_SELECT, BUTTON_ID_DOWN};
  for (int i = 0; i < 3; i++) {
    bool pressed = s_button_pressed && s_pressed_button == buttons[i];
    if (pressed) {
      graphics_context_set_fill_color(ctx, c.action_bar_press_bg);
      graphics_fill_rect(ctx, GRect(bar_x, i * segment_height, 30, segment_height),
                         0, GCornerNone);
    }
    draw_action_bar_icon(ctx, buttons[i],
                         GPoint(185 - (pressed ? 3 : 0), i * segment_height + 38),
                         pressed ? c.action_bar_press_icon : c.action_bar_icon, paused,
                         s_action_bar_page);
  }
}

// Draws the filled accent-colored play/pause button used by the "beak fm"
// style Now Playing screen: a triangle when paused (tap/press would resume),
// two bars when playing. Mirrors the SELECT icon on action-bar page 2.
static void draw_play_button(GContext *ctx, GPoint center, int radius, bool paused) {
  graphics_context_set_fill_color(ctx, accent_color());
  graphics_fill_circle(ctx, center, radius);
  graphics_context_set_fill_color(ctx, GColorWhite);
  if (paused) {
    GPoint play[] = {
      GPoint(center.x - 5, center.y - 9),
      GPoint(center.x + 9, center.y),
      GPoint(center.x - 5, center.y + 9),
    };
    GPathInfo path_info = { .num_points = ARRAY_LENGTH(play), .points = play };
    GPath *path = gpath_create(&path_info);
    gpath_draw_filled(ctx, path);
    gpath_destroy(path);
  } else {
    graphics_fill_rect(ctx, GRect(center.x - 8, center.y - 8, 5, 16), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(center.x + 3, center.y - 8, 5, 16), 0, GCornerNone);
  }
}

// Small magnifying-glass icon, matching the search glyph used elsewhere.
static void draw_search_icon(GContext *ctx, GPoint c, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_circle(ctx, GPoint(c.x - 2, c.y - 2), 6);
  graphics_draw_line(ctx, GPoint(c.x + 3, c.y + 3), GPoint(c.x + 8, c.y + 8));
}

// Vinyl record, used for Library on the bespoke Home dock: your crate of saved music.
// (An earlier books-on-a-shelf glyph read as three bars at this size and collided
// with the Queue icon.)
static void draw_vinyl_icon(GContext *ctx, GPoint c, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_fill_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_circle(ctx, c, 8);
  graphics_fill_circle(ctx, c, 2);
}

// Three faders, used for Settings on the bespoke Home dock. Reads at 16px where the
// gear's spokes turned to mush.
static void draw_sliders_icon(GContext *ctx, GPoint c, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_fill_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  for (int i = -1; i <= 1; i++) {
    graphics_draw_line(ctx, GPoint(c.x + i * 6, c.y - 7), GPoint(c.x + i * 6, c.y + 7));
  }
  graphics_fill_circle(ctx, GPoint(c.x - 6, c.y - 2), 3);
  graphics_fill_circle(ctx, GPoint(c.x, c.y + 3), 3);
  graphics_fill_circle(ctx, GPoint(c.x + 6, c.y - 4), 3);
}

// Circled "i", used for About on the bespoke Home dock.
static void draw_info_icon(GContext *ctx, GPoint c, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_fill_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_circle(ctx, c, 7);
  graphics_fill_circle(ctx, GPoint(c.x, c.y - 3), 1);
  graphics_fill_rect(ctx, GRect(c.x - 1, c.y, 2, 5), 1, GCornersAll);
}

// Three stacked bars (a track list), used for the "Queue" action.
static void draw_queue_icon(GContext *ctx, GPoint c, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(c.x - 8, c.y - 7), GPoint(c.x + 8, c.y - 7));
  graphics_draw_line(ctx, GPoint(c.x - 8, c.y), GPoint(c.x + 8, c.y));
  graphics_draw_line(ctx, GPoint(c.x - 8, c.y + 7), GPoint(c.x + 3, c.y + 7));
}

// Eighth note, used for "Song Search" on the search-type pager: one track, found
// directly. Filled head with a stroked stem and flag.
static void draw_note_icon(GContext *ctx, GPoint c, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_fill_circle(ctx, GPoint(c.x - 3, c.y + 5), 4);
  graphics_draw_line(ctx, GPoint(c.x + 1, c.y + 5), GPoint(c.x + 1, c.y - 7));
  graphics_draw_line(ctx, GPoint(c.x + 1, c.y - 7), GPoint(c.x + 6, c.y - 4));
}

// Head-and-shoulders bust, used for "Artist Radio". Filled like the More popup's
// heart so the three search-type glyphs share one weight.
static void draw_person_icon(GContext *ctx, GPoint c, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_circle(ctx, GPoint(c.x, c.y - 4), 4);
  graphics_fill_rect(ctx, GRect(c.x - 6, c.y + 2, 12, 7), 3, GCornersAll);
}

// Broadcast waves, used for "Song Radio": a seed track radiating into a station.
// Arc angles use the SDK's 0-degrees-at-top, clockwise convention, so the left wave
// sweeps 225->315 through the left side and the right wave 45->135 through the right;
// neither arc crosses the 360/0 wrap.
static void draw_broadcast_icon(GContext *ctx, GPoint c, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_fill_circle(ctx, c, 2);
  graphics_draw_arc(ctx, GRect(c.x - 5, c.y - 5, 10, 10), GOvalScaleModeFitCircle,
                    DEG_TO_TRIGANGLE(225), DEG_TO_TRIGANGLE(315));
  graphics_draw_arc(ctx, GRect(c.x - 5, c.y - 5, 10, 10), GOvalScaleModeFitCircle,
                    DEG_TO_TRIGANGLE(45), DEG_TO_TRIGANGLE(135));
  graphics_draw_arc(ctx, GRect(c.x - 9, c.y - 9, 18, 18), GOvalScaleModeFitCircle,
                    DEG_TO_TRIGANGLE(225), DEG_TO_TRIGANGLE(315));
  graphics_draw_arc(ctx, GRect(c.x - 9, c.y - 9, 18, 18), GOvalScaleModeFitCircle,
                    DEG_TO_TRIGANGLE(45), DEG_TO_TRIGANGLE(135));
}

static int np_more_count(void);

// Modal "More" popup over the now-playing screen. Styled to match the "beak fm"
// now-playing layout - white canvas, large title/subtitle text, and the same accent
// icon language as the playback controls - rather than the settings-menu list style.
static void draw_np_more(GContext *ctx) {
  // Plain solid panel (a dimmed-artwork background was tried but was too slow to redraw).
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  // Eyebrow is black like every other 14px label, and names its parent screen so
  // the popup reads as part of Now Playing rather than a foreign modal; the 18px
  // state value keeps the gray so the big action name stays on top of the hierarchy.
  GColor dim_text = GColorDarkGray;
  GColor name_text = GColorBlack;
  GColor idle_glyph = GColorBlack;

  draw_text(ctx, "NOW PLAYING", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GColorBlack,
            GRect(18, 18, 164, 18), GTextAlignmentLeft);

  // The selected action's name and current state, echoing the track title/artist.
  static const char *const names[NP_MORE_COUNT] = {
    "Shuffle", "Repeat", "Favorite", "Output", "New search", "Queue",
  };
  char value[12];
  switch (s_np_more_selection) {
    case 0: snprintf(value, sizeof value, "%s", s_shuffle_enabled ? "On" : "Off"); break;
    case 1: snprintf(value, sizeof value, "%s",
              s_loop_mode == LoopModeAll ? "All" : s_loop_mode == LoopModeOne ? "One" : "Off"); break;
    case 2: snprintf(value, sizeof value, "%s", s_current_favorite ? "On" : "Off"); break;
    case 3: snprintf(value, sizeof value, "%s", s_phone_audio ? "Phone" : "Watch"); break;
    default: value[0] = '\0'; break;
  }
  draw_text(ctx, names[s_np_more_selection], fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
            name_text, GRect(18, 54, 164, 40), GTextAlignmentLeft);
  if (value[0]) {
    draw_text(ctx, value, fonts_get_system_font(FONT_KEY_GOTHIC_18), dim_text,
              GRect(18, 96, 164, 24), GTextAlignmentLeft);
  }

  // Row of action icons; the selected one sits inside an accent circle, and toggles
  // that are currently on are tinted with the accent even when not selected.
  const int cy = 158;
  const int icon_pitch = 30;
  int count = np_more_count();
  for (int i = 0; i < count; i++) {
    int cx = 100 + (2 * i - (count - 1)) * icon_pitch / 2;
    bool selected = i == s_np_more_selection;
    bool active = (i == 0 && s_shuffle_enabled) || (i == 1 && s_loop_enabled) ||
                  (i == 2 && s_current_favorite);
    GColor glyph;
    if (selected) {
      graphics_context_set_fill_color(ctx, accent_color());
      graphics_fill_circle(ctx, GPoint(cx, cy), 17);
      glyph = GColorWhite;
    } else {
      glyph = active ? accent_color() : idle_glyph;
    }
    GPoint gc = GPoint(cx, cy);
    switch (i) {
      case 0: draw_shuffle_icon(ctx, gc, glyph); break;
      case 1: draw_repeat_icon(ctx, gc, glyph); break;
      // Always filled - black when not favorited, accent (pink by default) when
      // favorited or selected - rather than an outline for the idle state.
      case 2: draw_heart_icon(ctx, gc, 8, glyph, true); break;
      case 3:
        if (s_phone_audio) {
          draw_phone_icon(ctx, gc, 13, glyph);
        } else {
          draw_watch_icon(ctx, gc, 14, glyph);
        }
        break;
      case 4: draw_search_icon(ctx, gc, glyph); break;
      default: draw_queue_icon(ctx, gc, glyph); break;
    }
  }

  draw_text(ctx, "SELECT choose    BACK close",
            fonts_get_system_font(FONT_KEY_GOTHIC_14), GColorBlack,
            GRect(8, 202, 184, 18), GTextAlignmentCenter);
}

// "Beak FM" style Now Playing layout: a plain white canvas with a small
// source glyph (phone/watch) up top, large title/artist, an optional
// progress bar (see the "Progress bar" setting), and a floating accent-color
// play/pause button in place of the old colored header/footer bars. Used for
// every theme; only the accent (and, for the Mono theme, the action bar)
// colors change between themes.
static void draw_modern_song(GContext *ctx, AppScreen state) {
  bool paused = state == ScreenPaused;
  int content_width = s_action_bar_visible ? 154 : 180;
  const SearchResult *result = current_playing_result();
  if (!result) return;
  const int pad_x = 18;
  const int inner_w = content_width - pad_x - 8;

  // When cover art is showing, it *is* the background - no card behind it to dim or
  // crop the art, just white/black UI elements laid straight over the artwork.
  bool on_art = s_cover_art_background && s_cover_art_ready;

  // Artwork-only mode (touchscreen long-press): fill the screen with just the cover
  // art and nothing else. Only honored while art is actually available.
  if (s_artwork_only && on_art) {
    draw_cover_art_background(ctx, GRect(0, 0, 200, 228));
    return;
  }

  // Pick the color that won't blend into the artwork rather than always white -
  // light covers (white backgrounds, pastel art) need black UI elements instead.
  GColor art_ui_color = s_cover_art_dark ? GColorWhite : GColorBlack;
  GColor ui_primary = on_art ? art_ui_color : GColorBlack;
  GColor ui_secondary = on_art ? art_ui_color : GColorBlack;

  if (on_art) {
    draw_cover_art_background(ctx, GRect(0, 0, 200, 228));
  } else {
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  }

  // Source row: small phone/watch glyph plus a lowercase "on phone"/"on watch" label.
  GPoint source_icon = GPoint(pad_x + 7, 25);
  if (s_phone_audio) {
    draw_phone_icon(ctx, source_icon, 13, ui_secondary);
  } else {
    draw_watch_icon(ctx, source_icon, 14, ui_secondary);
  }
  draw_text(ctx, s_phone_audio ? "on phone" : "on watch",
            fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), ui_secondary,
            GRect(pad_x + 20, 16, inner_w - 20, 18), GTextAlignmentLeft);
  // Persistent favorite badge (hidden while the action bar covers it).
  if (s_current_favorite && !s_action_bar_visible) {
    draw_heart_icon(ctx, GPoint(content_width - 12, 25), 8, accent_color(), true);
  }

  draw_text(ctx, result->title, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD), ui_primary,
            GRect(pad_x, 47, inner_w, s_show_progress ? 64 : 86), GTextAlignmentLeft);
  draw_text(ctx, result->artist, fonts_get_system_font(FONT_KEY_GOTHIC_18), ui_secondary,
            GRect(pad_x, s_show_progress ? 113 : 129, inner_w, 24), GTextAlignmentLeft);

  int play_button_y;
  if (s_show_progress) {
    int filled_width = 0;
    if (s_show_volume) {
      filled_width = inner_w * displayed_volume() / 100;
    } else if (s_duration_seconds > 0) {
      uint32_t elapsed = s_elapsed_seconds > s_duration_seconds ? s_duration_seconds : s_elapsed_seconds;
      filled_width = inner_w * elapsed / s_duration_seconds;
    }
    GColor track_color = on_art ? (s_cover_art_dark ? GColorLightGray : GColorDarkGray) : GColorLightGray;
    graphics_context_set_fill_color(ctx, track_color);
    graphics_fill_rect(ctx, GRect(pad_x, 149, inner_w, 5), 3, GCornersAll);
    graphics_context_set_fill_color(ctx, accent_color());
    graphics_fill_rect(ctx, GRect(pad_x, 149, filled_width, 5), 3, GCornersAll);

    char left[16];
    char right[16];
    if (s_show_volume) {
      snprintf(left, sizeof(left), "VOLUME");
      snprintf(right, sizeof(right), "%d%%", displayed_volume());
    } else {
      format_time(s_elapsed_seconds, left, sizeof(left));
      format_time(s_duration_seconds, right, sizeof(right));
    }
    draw_text(ctx, left, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), ui_primary,
              GRect(pad_x, 158, inner_w / 2, 18), GTextAlignmentLeft);
    draw_text(ctx, right, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), ui_primary,
              GRect(pad_x + inner_w / 2, 158, inner_w - inner_w / 2, 18), GTextAlignmentRight);
    play_button_y = 200;
  } else {
    // Battery-saver layout: no progress bar (and no per-second redraws to
    // update it), just the track info and the play/pause indicator.
    play_button_y = 186;
  }

  int controls_y = s_show_progress ? 200 : 186;
  GPoint shuffle_center = GPoint(pad_x + 12, controls_y);
  GPoint repeat_center = GPoint(pad_x + inner_w - 12, controls_y);
  GColor inactive_control = on_art ? art_ui_color : GColorLightGray;
  draw_shuffle_icon(ctx, shuffle_center, s_shuffle_enabled ? accent_color() : inactive_control);
  draw_repeat_icon(ctx, repeat_center, s_loop_enabled ? accent_color() : inactive_control);
  if (s_loop_enabled) {
    graphics_context_set_fill_color(ctx, accent_color());
    if (s_loop_mode == LoopModeAll) {
      graphics_fill_rect(ctx, GRect(repeat_center.x + 3, repeat_center.y + 2, 13, 9),
                         3, GCornersAll);
      draw_text(ctx, "A", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GColorWhite,
                GRect(repeat_center.x + 4, repeat_center.y - 2, 10, 12), GTextAlignmentCenter);
    } else {
      graphics_fill_circle(ctx, GPoint(repeat_center.x + 8, repeat_center.y + 7), 5);
      draw_text(ctx, "1", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GColorWhite,
                GRect(repeat_center.x + 3, repeat_center.y - 2, 10, 12), GTextAlignmentCenter);
    }
  }
  draw_play_button(ctx, GPoint(pad_x + inner_w / 2, play_button_y), 18, paused);

  // Battery-saver layout has no progress bar to host the volume readout, so show a
  // transient centered volume popup when the volume changes.
  if (!s_show_progress && s_show_volume) {
    const int pw = 164, ph = 46;
    GRect card = GRect(100 - pw / 2, 100, pw, ph);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, card, 8, GCornersAll);
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_round_rect(ctx, card, 8);
    char pct[8];
    snprintf(pct, sizeof pct, "%d%%", displayed_volume());
    draw_text(ctx, "VOLUME", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GColorWhite,
              GRect(card.origin.x + 10, card.origin.y + 5, pw - 20, 16), GTextAlignmentLeft);
    draw_text(ctx, pct, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GColorWhite,
              GRect(card.origin.x + 10, card.origin.y + 5, pw - 20, 16), GTextAlignmentRight);
    int bx = card.origin.x + 10, by = card.origin.y + 28, barw = pw - 20;
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    graphics_fill_rect(ctx, GRect(bx, by, barw, 6), 3, GCornersAll);
    graphics_context_set_fill_color(ctx, accent_color());
    graphics_fill_rect(ctx, GRect(bx, by, barw * displayed_volume() / 100, 6), 3, GCornersAll);
  }

  if (s_action_bar_visible) draw_action_bar(ctx, paused);
  if (s_np_more_open) {
    // The More popup shows toggle state directly, so skip the transient feedback flash.
    draw_np_more(ctx);
  } else {
    draw_feedback_overlay(ctx);
  }
}

static void draw_song(GContext *ctx, AppScreen state) {
  draw_modern_song(ctx, state);
}

static void draw_error(GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  // Chromeless, but failure keeps its accent as a thin eyebrow band so an error can
  // never be mistaken for an empty list.
  graphics_context_set_fill_color(ctx, accent_color());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 40), 0, GCornerNone);
  draw_text(ctx, "SOMETHING WENT WRONG", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
            GColorWhite, GRect(18, 18, 164, 18), GTextAlignmentLeft);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, s_status, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(18, 92, 164, 44), GTextOverflowModeWordWrap,
                     GTextAlignmentCenter, NULL);
  draw_text(ctx, s_result_count > 0 ? "SELECT retry    BACK close"
                                    : "SELECT search    BACK close",
            fonts_get_system_font(FONT_KEY_GOTHIC_14), GColorBlack,
            GRect(8, 202, 184, 18), GTextAlignmentCenter);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  ThemeColors c = colors();
  graphics_context_set_fill_color(ctx, c.background);
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
  draw_dreamhouse_backdrop(ctx);
  switch (s_screen) {
    case ScreenHome: draw_home(ctx); break;
    case ScreenLibrary:
      if (s_bespoke_ui) {
        draw_library_bespoke(ctx);
      } else {
        draw_native_menu(ctx, "LIBRARY", LIBRARY_ITEMS, library_item_count());
      }
      break;
    case ScreenLibraryItems: draw_library_items(ctx); break;
    case ScreenQueue: draw_queue(ctx); break;
    case ScreenMenu:
      if (s_bespoke_ui) {
        bespoke_ground(ctx, "DREAMWAVE");
        int offset = scroll_list_layout(BESPOKE_ROW_PITCH, ARRAY_LENGTH(MENU_ITEMS),
                                        s_menu_selection, BESPOKE_LIST_TOP,
                                        BESPOKE_VIEWPORT_H, true);
        for (int i = 0; i < (int) ARRAY_LENGTH(MENU_ITEMS); i++) {
          bespoke_row1(ctx, BESPOKE_LIST_TOP + i * BESPOKE_ROW_PITCH - offset,
                       BESPOKE_ROW_H, MENU_ITEMS[i], NULL, i == s_menu_selection);
        }
        bespoke_frame(ctx, "DREAMWAVE", "SELECT open    BACK home");
        bespoke_scrollbar(ctx, offset);
      } else {
        draw_native_menu(ctx, "DREAMWAVE", MENU_ITEMS, ARRAY_LENGTH(MENU_ITEMS));
      }
      break;
    case ScreenSettings: draw_settings(ctx); break;
    case ScreenAdvanced: draw_advanced(ctx); break;
    case ScreenAbout: draw_about(ctx); break;
    case ScreenInputChoice: draw_input_choice(ctx); break;
    case ScreenSearchType: draw_search_type(ctx); break;
    case ScreenKeyboard: draw_keyboard(ctx); break;
    case ScreenPlaceholder: draw_placeholder_screen(ctx); break;
    case ScreenSearching: draw_searching(ctx); break;
    case ScreenResults: draw_results(ctx); break;
    case ScreenBuffering: draw_buffering(ctx); break;
    case ScreenPlaying: draw_song(ctx, ScreenPlaying); break;
    case ScreenPaused: draw_song(ctx, ScreenPaused); break;
    case ScreenError: draw_error(ctx); break;
  }
}

static void root_canvas_update(Layer *layer, GContext *ctx) {
  if (screen_uses_overlay_window(s_screen)) return;
  canvas_update(layer, ctx);
}

static void overlay_canvas_update(Layer *layer, GContext *ctx) {
  if (!screen_uses_overlay_window(s_screen)) return;
  canvas_update(layer, ctx);
}

static void stop_audio(void) {
  if (s_stream_open) {
    speaker_stream_close();
    s_stream_open = false;
  }
  stop_progress_timer();
}

static bool start_audio(void) {
  stop_audio();
  s_expected_sequence = 0;
  s_stream_open = speaker_stream_open(SpeakerPcmFormat_16kHz_16bit, s_watch_volume);
  return s_stream_open;
}

static int16_t decode_adpcm_nibble(uint8_t code, int32_t *predictor, int32_t *step_index) {
  int32_t step = ADPCM_STEP_TABLE[*step_index];
  int32_t delta = step >> 3;
  if (code & 4) delta += step;
  if (code & 2) delta += step >> 1;
  if (code & 1) delta += step >> 2;
  *predictor += (code & 8) ? -delta : delta;
  if (*predictor > 32767) *predictor = 32767;
  if (*predictor < -32768) *predictor = -32768;
  *step_index += ADPCM_INDEX_TABLE[code & 7];
  if (*step_index > 88) *step_index = 88;
  if (*step_index < 0) *step_index = 0;
  return (int16_t) *predictor;
}

static bool decode_adpcm_block(const uint8_t *data, uint32_t size) {
  if (size != ADPCM_BLOCK_SIZE) return false;
  int32_t predictor = (int16_t) ((uint16_t) data[0] | ((uint16_t) data[1] << 8));
  int32_t step_index = data[2];
  if (step_index > 88) return false;

  s_pcm_buffer[0] = (int16_t) predictor;
  int sample_index = 1;
  for (uint32_t i = ADPCM_HEADER_SIZE; i < size; i++) {
    s_pcm_buffer[sample_index++] = decode_adpcm_nibble(data[i] & 0x0f, &predictor, &step_index);
    s_pcm_buffer[sample_index++] = decode_adpcm_nibble(data[i] >> 4, &predictor, &step_index);
  }
  return sample_index == ADPCM_SAMPLES_PER_BLOCK;
}

static void write_audio(const uint8_t *data, uint32_t size) {
  if (!s_stream_open) return;
  if (!decode_adpcm_block(data, size)) return;
  const uint8_t *pcm = (const uint8_t *) s_pcm_buffer;
  const uint32_t pcm_size = sizeof(s_pcm_buffer);
  uint32_t offset = 0;
  while (offset < pcm_size) {
    uint32_t written = speaker_stream_write(pcm + offset, pcm_size - offset);
    if (written == 0) {
      psleep(2);
    } else {
      offset += written;
    }
  }
  s_played_samples += ADPCM_SAMPLES_PER_BLOCK;
}

static bool send_command(int32_t command, const char *text, uint32_t text_key) {
  DictionaryIterator *iterator;
  if (app_message_outbox_begin(&iterator) != APP_MSG_OK) return false;
  dict_write_int32(iterator, MESSAGE_KEY_COMMAND, command);
  if (text) dict_write_cstring(iterator, text_key, text);
  dict_write_end(iterator);
  return app_message_outbox_send() == APP_MSG_OK;
}

static bool send_hello(void) {
  DictionaryIterator *iterator;
  if (app_message_outbox_begin(&iterator) != APP_MSG_OK) return false;
  dict_write_int32(iterator, MESSAGE_KEY_COMMAND, CommandHello);
  dict_write_int32(iterator, MESSAGE_KEY_PROTOCOL_VERSION, PROTOCOL_VERSION);
  dict_write_int32(iterator, MESSAGE_KEY_CAPABILITIES,
                   CAPABILITY_STATE_SNAPSHOT | CAPABILITY_SEARCH_REQUEST_ID);
  dict_write_end(iterator);
  return app_message_outbox_send() == APP_MSG_OK;
}

static bool send_settings_sync(void) {
  DictionaryIterator *iterator;
  if (app_message_outbox_begin(&iterator) != APP_MSG_OK) return false;
  APP_LOG(APP_LOG_LEVEL_INFO,
          "[ClaySync] Sending settings route=%d watchVol=%d phoneVol=%d input=%d progress=%d cacheEn=%d cacheMB=%d coverArt=%d watchQ=%d phoneQ=%d cacheRadio=%d",
          s_phone_audio ? 1 : 0,
          s_watch_volume,
          s_phone_volume,
          s_input_mode,
          s_show_progress ? 1 : 0,
          s_cache_enabled ? 1 : 0,
          s_cache_size_mb,
          s_cover_art_background ? 1 : 0,
          s_watch_audio_quality ? 1 : 0,
          s_phone_audio_quality ? 1 : 0,
          s_cache_radio ? 1 : 0);
  dict_write_int32(iterator, MESSAGE_KEY_COMMAND, CommandSyncSettings);
  dict_write_int32(iterator, MESSAGE_KEY_CONFIG_AUDIO_ROUTE, s_phone_audio ? 1 : 0);
  dict_write_int32(iterator, MESSAGE_KEY_CONFIG_WATCH_VOLUME, s_watch_volume);
  dict_write_int32(iterator, MESSAGE_KEY_CONFIG_PHONE_VOLUME, s_phone_volume);
  dict_write_int32(iterator, MESSAGE_KEY_CONFIG_INPUT_MODE, s_input_mode);
  dict_write_int32(iterator, MSG_CONFIG_SHOW_PROGRESS, s_show_progress ? 1 : 0);
  dict_write_int32(iterator, MSG_CONFIG_CACHE_ENABLED, s_cache_enabled ? 1 : 0);
  dict_write_int32(iterator, MSG_CONFIG_CACHE_SIZE_MB, s_cache_size_mb);
  dict_write_int32(iterator, MSG_CONFIG_COVER_ART_BG, s_cover_art_background ? 1 : 0);
  dict_write_int32(iterator, MSG_THEME, s_theme);
  dict_write_int32(iterator, MSG_CONFIG_WATCH_AUDIO_QUALITY, s_watch_audio_quality ? 1 : 0);
  dict_write_int32(iterator, MSG_CONFIG_PHONE_AUDIO_QUALITY, s_phone_audio_quality ? 1 : 0);
  dict_write_int32(iterator, MSG_CONFIG_CACHE_RADIO, s_cache_radio ? 1 : 0);
  dict_write_int32(iterator, MSG_ROUTE_EPOCH, s_route_epoch);
  dict_write_end(iterator);
  return app_message_outbox_send() == APP_MSG_OK;
}

static void handshake_retry(void *context) {
  s_handshake_timer = NULL;
  if (s_bridge_ready) return;
  send_hello();
  s_handshake_timer = app_timer_register(2000, handshake_retry, NULL);
}

static void bridge_connected(void) {
  // Once per session: this is called from the EventReady AND the EventStateSnapshot
  // handlers, and re-pushing the whole settings blob on every snapshot let a snapshot
  // that was in flight during a local settings change clobber the newer value back
  // onto the companion. The companion also pulls settings explicitly on Hello now, so
  // nothing depends on this firing more than once.
  if (s_bridge_ready) return;
  s_bridge_ready = true;
  if (s_handshake_timer) {
    app_timer_cancel(s_handshake_timer);
    s_handshake_timer = NULL;
  }
  send_settings_sync();
}

static bool send_generation_command(int32_t command, const char *text, uint32_t text_key) {
  DictionaryIterator *iterator;
  if (app_message_outbox_begin(&iterator) != APP_MSG_OK) return false;
  dict_write_int32(iterator, MESSAGE_KEY_COMMAND, command);
  dict_write_int32(iterator, MESSAGE_KEY_GENERATION, s_stream_generation);
  if (text) dict_write_cstring(iterator, text_key, text);
  dict_write_end(iterator);
  return app_message_outbox_send() == APP_MSG_OK;
}

static bool send_audio_route(void) {
  DictionaryIterator *iterator;
  if (app_message_outbox_begin(&iterator) != APP_MSG_OK) return false;
  dict_write_int32(iterator, MESSAGE_KEY_COMMAND, CommandSetAudioRoute);
  dict_write_int32(iterator, MESSAGE_KEY_AUDIO_ROUTE, s_phone_audio ? 1 : 0);
  dict_write_int32(iterator, MESSAGE_KEY_GENERATION, s_stream_generation);
  dict_write_int32(iterator, MSG_ROUTE_EPOCH, s_route_epoch);
  dict_write_end(iterator);
  return app_message_outbox_send() == APP_MSG_OK;
}

static bool send_phone_volume(void) {
  DictionaryIterator *iterator;
  if (app_message_outbox_begin(&iterator) != APP_MSG_OK) return false;
  dict_write_int32(iterator, MESSAGE_KEY_COMMAND, CommandSetVolume);
  dict_write_int32(iterator, MESSAGE_KEY_VOLUME, s_phone_volume);
  dict_write_end(iterator);
  return app_message_outbox_send() == APP_MSG_OK;
}

static bool request_library(int library_type) {
  DictionaryIterator *iterator;
  if (app_message_outbox_begin(&iterator) != APP_MSG_OK) return false;
  dict_write_int32(iterator, MESSAGE_KEY_COMMAND, CommandRequestLibrary);
  dict_write_int32(iterator, MESSAGE_KEY_LIBRARY_TYPE, library_type);
  // Recently Played and Recent Searches are capped by their user-configurable
  // display limits. Other library types send the full-array cap so the companion
  // returns everything available.
  int32_t limit = library_type == LibraryRecent ? s_history_limit :
                  library_type == LibraryRecentSearches ? s_recent_search_limit : MAX_LIBRARY;
  dict_write_int32(iterator, MESSAGE_KEY_LIBRARY_LIMIT, limit);
  dict_write_end(iterator);
  return app_message_outbox_send() == APP_MSG_OK;
}

static bool request_queue(void) {
  DictionaryIterator *iterator;
  if (app_message_outbox_begin(&iterator) != APP_MSG_OK) return false;
  dict_write_int32(iterator, MESSAGE_KEY_COMMAND, CommandRequestQueue);
  dict_write_end(iterator);
  return app_message_outbox_send() == APP_MSG_OK;
}

static void open_queue(void) {
  s_result_count = 0;
  s_selected_result = 0;
  s_queue_loading = true;
  s_queue_return_screen = s_screen;
  nav_push(ScreenQueue);
  if (!request_queue()) s_queue_loading = false;
}

static bool request_shuffle_play(void) {
  DictionaryIterator *iterator;
  if (app_message_outbox_begin(&iterator) != APP_MSG_OK) return false;
  dict_write_int32(iterator, MESSAGE_KEY_COMMAND, CommandToggleShuffle);
  dict_write_int32(iterator, MESSAGE_KEY_GENERATION, s_stream_generation);
  dict_write_end(iterator);
  return app_message_outbox_send() == APP_MSG_OK;
}

static void start_search(void);
static void open_keyboard(void);
static bool submit_search_query(void);

// Dispatches on s_input_mode once the search type (Song / Artist Radio) has already
// been chosen - the second half of what begin_configured_search used to do in one step.
static void continue_configured_search(void) {
  if (s_input_mode == InputKeyboard) {
    open_keyboard();
  } else if (s_input_mode == InputAsk) {
    s_menu_selection = 0;
    nav_push(ScreenInputChoice);
  } else {
    start_search();
  }
}

// Entry point for both "start a search" triggers (Home screen select, Menu > Search):
// always ask Song vs Artist Radio first; continue_configured_search() picks up from
// there once select_click's ScreenSearchType branch records the choice.
static void begin_configured_search(void) {
  s_menu_selection = 0;
  nav_push(ScreenSearchType);
}

static void reset_keyboard_level(void) {
  s_keyboard_start = 0;
  s_keyboard_size = 27;
}

static void open_keyboard(void) {
  s_query[0] = '\0';
  s_query_length = 0;
  s_keyboard_mode = s_keyboard_pt2 ? 0 : 0;
  reset_keyboard_level();
  nav_push(ScreenKeyboard);
  s_menu_selection = 4;
  layer_mark_dirty(s_canvas);
}

static bool submit_search_query(void) {
  if (!s_bridge_ready || s_query_length == 0) return false;
  s_result_count = 0;
  s_selected_result = 0;
  s_search_active = true;
  s_search_request_id++;
  DictionaryIterator *iterator;
  if (app_message_outbox_begin(&iterator) != APP_MSG_OK) {
    s_search_active = false;
    return false;
  }
  dict_write_int32(iterator, MESSAGE_KEY_COMMAND, CommandSearch);
  dict_write_cstring(iterator, MESSAGE_KEY_QUERY, s_query);
  dict_write_int32(iterator, MESSAGE_KEY_SEARCH_REQUEST_ID, s_search_request_id);
  dict_write_int32(iterator, MESSAGE_KEY_SEARCH_LIMIT, s_search_limit);
  dict_write_int32(iterator, MSG_SEARCH_MODE, s_search_mode);
  dict_write_end(iterator);
  if (app_message_outbox_send() != APP_MSG_OK) {
    s_search_active = false;
    return false;
  }
  // Drop any transient search-input screens (Keyboard / SearchWith / SearchType) from
  // the Back history so leaving the results returns to whatever launched the search.
  while (s_nav_depth > 0 &&
         (s_nav_stack[s_nav_depth - 1].screen == ScreenKeyboard ||
          s_nav_stack[s_nav_depth - 1].screen == ScreenInputChoice ||
          s_nav_stack[s_nav_depth - 1].screen == ScreenSearchType)) {
    s_nav_depth--;
  }
  set_screen(ScreenSearching);
  return true;
}

// Re-runs a previously issued search from the Recent Searches list. The query is
// carried in the entry's video_id field (see the companion's recent-search items).
// Recent searches predate Artist Radio mode and have no stored mode, so always
// re-run them as a Song search.
static bool submit_recent_search(const char *query) {
  if (!query || query[0] == '\0') return false;
  snprintf(s_query, sizeof(s_query), "%s", query);
  s_query_length = strlen(s_query);
  s_search_mode = SearchModeSong;
  return submit_search_query();
}

static void keyboard_choose(int choice) {
  if (s_keyboard_pt2) {
    int index = s_menu_selection;
    if (index < 0) index = 0;
    if (index > 8) index = 8;
    const char *pick = s_keyboard_mode == 2 ? PT2_NUMBERS_TAP[index] : PT2_LETTERS_TAP[index];
    if (pick[0] != '\0' && s_query_length < TEXT_LENGTH - 1) {
      char character = pick[0];
      if (s_keyboard_mode == 1 && character >= 'a' && character <= 'z') {
        character -= 'a' - 'A';
      }
      s_query[s_query_length++] = character;
      s_query[s_query_length] = '\0';
    }
    layer_mark_dirty(s_canvas);
    (void) choice;
    return;
  }
  int option_size = s_keyboard_size / 3;
  if (option_size > 1) {
    s_keyboard_start += choice * option_size;
    s_keyboard_size = option_size;
  } else if (s_query_length < TEXT_LENGTH - 1) {
    s_query[s_query_length++] = keyboard_characters()[s_keyboard_start + choice];
    s_query[s_query_length] = '\0';
    reset_keyboard_level();
  }
  layer_mark_dirty(s_canvas);
}

static bool keyboard_back_level(void) {
  if (s_keyboard_pt2) {
    if (s_query_length == 0) return false;
    s_query[--s_query_length] = '\0';
    layer_mark_dirty(s_canvas);
    return true;
  }
  if (s_keyboard_size == 27) {
    if (s_query_length == 0) return false;
    s_query[--s_query_length] = '\0';
  } else if (s_keyboard_size == 9) {
    reset_keyboard_level();
  } else {
    s_keyboard_start = (s_keyboard_start / 9) * 9;
    s_keyboard_size = 9;
  }
  layer_mark_dirty(s_canvas);
  return true;
}

#ifdef PBL_PLATFORM_EMERY
static int pt2_touch_key_at(int16_t x, int16_t y) {
  const int grid_left = 4;
  const int grid_top = 58;
  const int grid_right = 196;
  const int grid_bottom = 224;
  if (x < grid_left || x >= grid_right || y < grid_top || y >= grid_bottom) return -1;
  int col = (x - grid_left) * 3 / (grid_right - grid_left);
  int row = (y - grid_top) * 3 / (grid_bottom - grid_top);
  if (col < 0 || col > 2 || row < 0 || row > 2) return -1;
  return row * 3 + col;
}

// Padded hit-rect for the HELP badge (larger than the glyph itself at
// GRect(153, 4, 38, 16) so it's easy to tap).
static bool pt2_point_in_rect(GRect rect, int16_t x, int16_t y) {
  return x >= rect.origin.x && x < rect.origin.x + rect.size.w &&
         y >= rect.origin.y && y < rect.origin.y + rect.size.h;
}
static GRect pt2_help_badge_rect(void) { return GRect(140, 2, 56, 22); }

static char pt2_character_for_choice(int origin, int choice) {
  if (origin < 0 || origin > 8 || choice < 0 || choice > 2) return '\0';
  char character = PT2_TOUCH_MAP[origin][choice].character;
  if (s_keyboard_mode == 1 && character >= 'a' && character <= 'z') {
    character -= 'a' - 'A';
  }
  return character;
}

// The handful of distinct grid-delta lengths possible on a 3x3 layout
// (1, sqrt2, 2, sqrt5, sqrt8 cells away), scaled by 1000. Avoids pulling in
// <math.h>/sqrtf for what's otherwise a 3-way comparison.
static int32_t pt2_dir_magnitude(int32_t mag_sq) {
  switch (mag_sq) {
    case 1: return 1000;
    case 2: return 1414;
    case 4: return 2000;
    case 5: return 2236;
    case 8: return 2828;
    default: return 1000;
  }
}

#define PT2_SWIPE_MIN_DIST 14
#define PT2_SWIPE_MIN_DIST_SQ (PT2_SWIPE_MIN_DIST * PT2_SWIPE_MIN_DIST)

// Picks which of the origin key's 3 directional choices best matches the
// swipe vector (dx, dy) in screen pixels, based on angle rather than
// requiring the finger to actually land inside the destination cell.
// Returns -1 if the swipe is too short to have a clear direction.
static int8_t pt2_touch_best_choice(int origin, int16_t dx, int16_t dy) {
  if (origin < 0 || origin > 8) return -1;
  int32_t dist_sq = (int32_t) dx * dx + (int32_t) dy * dy;
  if (dist_sq < PT2_SWIPE_MIN_DIST_SQ) return -1;
  int orow = origin / 3, ocol = origin % 3;
  int8_t best = -1;
  int32_t best_score = 0;
  bool have_best = false;
  for (int i = 0; i < 3; i++) {
    int target = PT2_TOUCH_MAP[origin][i].target;
    int32_t tx = (target % 3) - ocol;
    int32_t ty = (target / 3) - orow;
    int32_t mag_sq = tx * tx + ty * ty;
    if (mag_sq == 0) continue;
    int32_t dot = (int32_t) dx * tx + (int32_t) dy * ty;
    // Proportional to the cosine of the angle between the swipe and this
    // choice's direction; the shared |swipe| factor is dropped since it's
    // constant across all 3 candidates and doesn't affect the argmax.
    int32_t score = (dot * 1000) / pt2_dir_magnitude(mag_sq);
    if (!have_best || score > best_score) {
      best_score = score;
      best = (int8_t) i;
      have_best = true;
    }
  }
  return best;
}

// Fires when a stationary touch has been held long enough to count as a long-press:
// toggle the artwork-only view. Only meaningful when cover art is actually showing.
static void np_hold_timer_cb(void *context) {
  (void) context;
  s_np_hold_timer = NULL;
  if (!s_np_touching) return;
  if (s_screen != ScreenPlaying && s_screen != ScreenPaused) return;
  if (!s_cover_art_background || !s_cover_art_ready) return;
  s_artwork_only = !s_artwork_only;
  s_np_hold_fired = true;
  vibes_short_pulse();
  layer_mark_dirty(s_canvas);
}

// True when (x, y) is within radius r of center (cx, cy) - a generous circular
// hit-test for the on-screen now-playing controls.
static bool np_hit(int x, int y, int cx, int cy, int r) {
  int dx = x - cx, dy = y - cy;
  return dx * dx + dy * dy <= r * r;
}

// Handles touch on the now-playing screen. A stationary hold toggles artwork-only
// (via np_hold_timer_cb); a short tap hits the on-screen controls (play/pause,
// shuffle, loop, output). Swipes deliberately do nothing here - they used to map to
// previous/next and volume, but the now-playing screen is easy to brush against and
// those gestures fired far too readily. Track skips and volume are buttons-only.
static void np_touch_handle(const TouchEvent *event) {
  switch (event->type) {
    case TouchEvent_Touchdown:
      s_np_touching = true;
      s_np_hold_fired = false;
      s_np_touch_x = event->x;
      s_np_touch_y = event->y;
      if (s_np_hold_timer) app_timer_cancel(s_np_hold_timer);
      s_np_hold_timer = app_timer_register(550, np_hold_timer_cb, NULL);
      break;
    case TouchEvent_PositionUpdate: {
      int16_t dx = event->x - s_np_touch_x;
      int16_t dy = event->y - s_np_touch_y;
      if (dx * dx + dy * dy > 20 * 20 && s_np_hold_timer) {
        app_timer_cancel(s_np_hold_timer);
        s_np_hold_timer = NULL;
      }
      break;
    }
    case TouchEvent_Liftoff: {
      if (s_np_hold_timer) {
        app_timer_cancel(s_np_hold_timer);
        s_np_hold_timer = NULL;
      }
      bool was_touching = s_np_touching;
      bool hold_fired = s_np_hold_fired;
      s_np_touching = false;
      s_np_hold_fired = false;
      // The hold gesture already acted; ignore its liftoff. Buttons drive the More popup.
      if (!was_touching || hold_fired || s_np_more_open) break;
      int16_t dx = event->x - s_np_touch_x;
      int16_t dy = event->y - s_np_touch_y;
      // Anything that travelled is a swipe, and swipes are ignored on this screen.
      if ((int32_t) dx * dx + (int32_t) dy * dy >= 24 * 24) break;
      // Tap. A tap in artwork-only mode just restores the UI. Otherwise hit-test the
      // controls using the same geometry as draw_modern_song (action bar gone, so the
      // content spans the full 180px width).
      if (s_artwork_only) {
        s_artwork_only = false;
        layer_mark_dirty(s_canvas);
        break;
      }
      // Geometry must be derived exactly as draw_modern_song derives it. It used to
      // hardcode the action-bar-hidden width, which put the repeat hit-test at x=160
      // while the icon was actually drawn at x=134 whenever the action bar was
      // showing - 26px away from a 22px target, so the tap always missed and loop
      // could not be toggled by touch. Shuffle sits at a fixed offset from the left
      // and play/pause is within its larger radius, which is why only repeat broke.
      const int pad_x = 18;
      const int content_width = s_action_bar_visible ? 154 : 180;
      const int inner_w = content_width - pad_x - 8;
      const int cy = s_show_progress ? 200 : 186;  // controls row
      if (np_hit(event->x, event->y, pad_x + inner_w / 2, cy, 28)) {
        np_toggle_play_pause();
      } else if (np_hit(event->x, event->y, pad_x + 12, cy, 22)) {
        np_toggle_shuffle();
      } else if (np_hit(event->x, event->y, pad_x + inner_w - 12, cy, 22)) {
        np_cycle_loop();
      } else if (event->y < 42) {
        np_toggle_output();   // tapping the source row switches phone/watch
      }
      layer_mark_dirty(s_canvas);
      break;
    }
  }
}

static void pt2_touch_handler(const TouchEvent *event, void *context) {
  (void) context;
  if (!event) return;
  if (s_screen == ScreenPlaying || s_screen == ScreenPaused) {
    np_touch_handle(event);
    return;
  }
  if (s_screen != ScreenKeyboard || !s_keyboard_pt2) return;
  if (event->type == TouchEvent_Touchdown) {
    if (pt2_point_in_rect(pt2_help_badge_rect(), event->x, event->y)) {
      s_touch_badge = Pt2BadgeHelp;
      vibes_short_pulse();
    } else {
      int key = pt2_touch_key_at(event->x, event->y);
      if (key < 0) return;
      s_touch_active = true;
      s_touch_origin_key = key;
      s_touch_active_key = key;
      s_touch_start_x = event->x;
      s_touch_start_y = event->y;
      vibes_short_pulse();
    }
  } else if (event->type == TouchEvent_PositionUpdate && s_touch_active &&
             (s_keyboard_mode != 2 || s_touch_origin_key == PT2_ZERO_CELL)) {
    int16_t dx = event->x - s_touch_start_x;
    int16_t dy = event->y - s_touch_start_y;
    if (s_keyboard_mode == 2) {
      // Numbers: only the "8" cell fans. Swipe up-left highlights 8 (cell 0),
      // up-right highlights 0 (cell 2); the horizontal sign decides.
      int32_t dist_sq = (int32_t) dx * dx + (int32_t) dy * dy;
      s_touch_active_key = dist_sq >= PT2_SWIPE_MIN_DIST_SQ ? (dx > 0 ? 2 : 0)
                                                            : s_touch_origin_key;
    } else {
      int8_t choice = pt2_touch_best_choice(s_touch_origin_key, dx, dy);
      s_touch_active_key = choice >= 0 ? PT2_TOUCH_MAP[s_touch_origin_key][choice].target
                                        : s_touch_origin_key;
    }
  } else if (event->type == TouchEvent_Liftoff) {
    if (s_touch_badge == Pt2BadgeHelp) {
      if (pt2_point_in_rect(pt2_help_badge_rect(), event->x, event->y)) {
        show_feedback_timed(FeedbackKeyboardHint, 1800);
        vibes_short_pulse();
      }
    } else if (s_touch_active) {
      char character = '\0';
      if (s_keyboard_mode == 2) {
        if (s_touch_origin_key == PT2_ZERO_CELL) {
          // The "8" cell fans like the letter cells: swipe up-right for 0, otherwise
          // (swipe up-left or a plain tap) 8.
          int16_t dx = event->x - s_touch_start_x;
          int16_t dy = event->y - s_touch_start_y;
          int32_t dist_sq = (int32_t) dx * dx + (int32_t) dy * dy;
          character = (dist_sq >= PT2_SWIPE_MIN_DIST_SQ && dx > 0) ? '0' : '8';
        } else {
          character = PT2_NUMBERS_TAP[s_touch_origin_key][0];
        }
      } else {
        int16_t dx = event->x - s_touch_start_x;
        int16_t dy = event->y - s_touch_start_y;
        int8_t choice = pt2_touch_best_choice(s_touch_origin_key, dx, dy);
        if (choice >= 0) {
          character = pt2_character_for_choice(s_touch_origin_key, choice);
        } else if (s_touch_origin_key == 4) {
          character = ' ';
        }
      }
      if (character != '\0' && s_query_length < TEXT_LENGTH - 1) {
        s_query[s_query_length++] = character;
        s_query[s_query_length] = '\0';
        vibes_short_pulse();
      }
    }
    s_touch_badge = Pt2BadgeNone;
    s_touch_active = false;
    s_touch_origin_key = -1;
    s_touch_active_key = -1;
  }
  layer_mark_dirty(s_canvas);
}

static void sync_touch_service(void) {
  bool should_subscribe = (s_keyboard_pt2 && s_screen == ScreenKeyboard) ||
                          s_screen == ScreenPlaying || s_screen == ScreenPaused;
  if (should_subscribe && !s_touch_subscribed) {
    touch_service_subscribe(pt2_touch_handler, NULL);
    s_touch_subscribed = true;
  } else if (!should_subscribe && s_touch_subscribed) {
    touch_service_unsubscribe();
    s_touch_subscribed = false;
    s_touch_active = false;
    s_np_touching = false;
    if (s_np_hold_timer) {
      app_timer_cancel(s_np_hold_timer);
      s_np_hold_timer = NULL;
    }
  }
}
#else
static void sync_touch_service(void) {}
#endif

static void dictation_callback(DictationSession *session, DictationSessionStatus status,
                               char *transcription, void *context) {
  if (status != DictationSessionStatusSuccess || !transcription) {
    snprintf(s_status, sizeof(s_status), "Voice search failed (%d)", (int) status);
    set_screen(ScreenError);
    return;
  }
  snprintf(s_query, sizeof(s_query), "%s", transcription);
  s_query_length = strlen(s_query);
  if (!submit_search_query()) {
    snprintf(s_status, sizeof(s_status), "Could not send search");
    set_screen(ScreenError);
  }
}

static void start_search(void) {
  if (!s_bridge_ready) {
    snprintf(s_status, sizeof(s_status), "Android bridge unavailable");
    set_screen(ScreenError);
    return;
  }
  dictation_session_start(s_dictation);
}

// Sends a seek to an absolute position (ms) for the current playback generation.
// Seeking is only meaningful for phone-routed playback; the watch speaker path
// streams sequential PCM and cannot reposition, so it is ignored there.
static void play_selected(void);

static void play_selected(void) {
  if (s_result_count == 0) {
    start_search();
    return;
  }
  s_now_playing = s_results[s_selected_result];
  s_has_now_playing = true;
  s_played_samples = 0;
  s_elapsed_seconds = 0;
  s_duration_seconds = 0;
  s_stream_generation++;
  send_generation_command(CommandPlay, s_results[s_selected_result].video_id,
                          MESSAGE_KEY_VIDEO_ID);
  // Record the screen we launched from (search Results or a Library list) so Back
  // out of playback returns there rather than defaulting to search results.
  nav_push(ScreenBuffering);
}

static void clear_playing_track(void) {
  s_has_now_playing = false;
  s_playback_active = false;
  memset(&s_now_playing, 0, sizeof(s_now_playing));
  clear_cover_art();
}

// Safely copies a string tuple's payload into a fixed buffer. Payloads from the
// companion may arrive without a NUL terminator (PebbleKit2 sizes strings without
// one), may exceed the destination, and byte-level truncation can split a
// multi-byte UTF-8 character - the renderer draws such strings as garbage. The
// result is always a NUL-terminated string ending on a codepoint boundary.
static void copy_tuple_text(char *dst, size_t dst_size, const Tuple *tuple) {
  if (dst_size == 0) return;
  dst[0] = '\0';
  if (!tuple || tuple->type != TUPLE_CSTRING || tuple->length == 0) return;
  size_t len = tuple->length;
  if (len > dst_size - 1) len = dst_size - 1;
  const char *src = tuple->value->cstring;
  size_t n = 0;
  while (n < len && src[n] != '\0') n++;
  memcpy(dst, src, n);
  // Trim a trailing incomplete UTF-8 sequence left behind by truncation.
  size_t cont = 0;
  while (cont < n && cont < 3 && ((uint8_t) dst[n - 1 - cont] & 0xC0) == 0x80) cont++;
  if (n > 0) {
    if (cont == n) {
      n = 0;  // Nothing but continuation bytes: not valid UTF-8 at all.
    } else if (((uint8_t) dst[n - 1 - cont] & 0x80) != 0 || cont > 0) {
      uint8_t lead = (uint8_t) dst[n - 1 - cont];
      size_t expected = (lead & 0xE0) == 0xC0 ? 2 :
                        (lead & 0xF0) == 0xE0 ? 3 :
                        (lead & 0xF8) == 0xF0 ? 4 : 1;
      if ((lead & 0x80) == 0) {
        n -= cont;      // Stray continuation bytes after ASCII: drop them.
      } else if (expected == 1 || cont + 1 < expected) {
        n -= cont + 1;  // Invalid lead byte or truncated sequence: drop it all.
      }
    }
  }
  dst[n] = '\0';
}

static void inbox_received(DictionaryIterator *iterator, void *context) {
  Tuple *command_tuple = dict_find(iterator, MESSAGE_KEY_COMMAND);
  int32_t command = command_tuple ? command_tuple->value->int32 : -1;
  Tuple *generation_tuple = dict_find(iterator, MESSAGE_KEY_GENERATION);
  int32_t generation = generation_tuple ? generation_tuple->value->int32 : -1;
  if (command_tuple && command == EventReady) {
    bridge_connected();
    snprintf(s_status, sizeof(s_status), "Android bridge ready");
    layer_mark_dirty(s_canvas);
  } else if (command_tuple && command == EventSearchResult) {
    Tuple *request = dict_find(iterator, MESSAGE_KEY_SEARCH_REQUEST_ID);
    if (request && request->value->int32 != s_search_request_id) return;
    Tuple *index = dict_find(iterator, MESSAGE_KEY_RESULT_INDEX);
    Tuple *video = dict_find(iterator, MESSAGE_KEY_VIDEO_ID);
    Tuple *title = dict_find(iterator, MESSAGE_KEY_TITLE);
    Tuple *artist = dict_find(iterator, MESSAGE_KEY_ARTIST);
    if (!index || !video || !title || !artist) return;
    int i = index->value->int32;
    if (i < 0 || i >= MAX_RESULTS) return;
    copy_tuple_text(s_results[i].video_id, TEXT_LENGTH, video);
    copy_tuple_text(s_results[i].title, TEXT_LENGTH, title);
    copy_tuple_text(s_results[i].artist, TEXT_LENGTH, artist);
    if (i + 1 > s_result_count) s_result_count = i + 1;
  } else if (command_tuple && command == EventSearchComplete) {
    Tuple *request = dict_find(iterator, MESSAGE_KEY_SEARCH_REQUEST_ID);
    if (request && request->value->int32 != s_search_request_id) return;
    if (!s_search_active) return;
    s_search_active = false;
    set_screen(ScreenResults);
    sync_native_menu(false);
  } else if (command_tuple && command == EventAudioStart) {
    if (generation != s_stream_generation) return;
    if (s_phone_audio || start_audio()) {
      set_screen(ScreenPlaying);
      start_progress_timer();
    }
  } else if (command_tuple && command == EventPaused) {
    if (generation != s_stream_generation) return;
    stop_audio();
    set_screen(ScreenPaused);
  } else if (command_tuple && command == EventPlaybackInfo) {
    if (generation_tuple && generation != s_stream_generation) return;
    Tuple *duration = dict_find(iterator, MESSAGE_KEY_DURATION);
    if (duration) {
      s_duration_seconds = duration->value->int32 / 1000;
      layer_mark_dirty(s_canvas);
    }
  } else if (command_tuple && command == EventPlaybackPosition) {
    if (generation_tuple && generation != s_stream_generation) return;
    Tuple *position = dict_find(iterator, MESSAGE_KEY_POSITION);
    if (position) {
      s_elapsed_seconds = position->value->int32 / 1000;
      if (!s_phone_audio) s_played_samples = s_elapsed_seconds * 16000;
      layer_mark_dirty(s_canvas);
    }
  } else if (command_tuple && command == EventAudioChunk) {
    if (generation != s_stream_generation || !s_stream_open ||
        (s_screen != ScreenPlaying && s_screen != ScreenBuffering)) return;
    Tuple *sequence = dict_find(iterator, MESSAGE_KEY_SEQUENCE);
    Tuple *audio = dict_find(iterator, MESSAGE_KEY_AUDIO);
    if (!sequence || !audio || sequence->value->int32 != s_expected_sequence) {
      stop_audio();
      snprintf(s_status, sizeof(s_status), "Audio packet was lost");
      set_screen(ScreenError);
      return;
    }
    s_expected_sequence++;
    write_audio(audio->value->data, audio->length);
  } else if (command_tuple && command == EventCoverArtStart) {
    Tuple *width = dict_find(iterator, MSG_IMAGE_WIDTH);
    Tuple *height = dict_find(iterator, MSG_IMAGE_HEIGHT);
    Tuple *total = dict_find(iterator, MSG_IMAGE_TOTAL_BYTES);
    if (!width || !height || !total) return;
    // Cover art is addressed by *track*, not by stream generation. Requiring an exact
    // generation match (as this did) meant a cover the phone had begun sending moments
    // before any local next/previous/resume/route-toggle/back was thrown away, even
    // though it was the art for the track still on screen. That is the single biggest
    // reason art "sometimes" arrived: the companion logged every chunk delivered while
    // the watch logged no completions at all.
    //
    // The id alone cannot decide it either, because there is no ordering to rely on: on
    // Next the phone sends the new cover and the new state snapshot at about the same
    // moment, so at Start time s_now_playing may still name the *previous* track. So:
    // an id we recognise is accepted outright, and an id we do not is accepted only if
    // the sender is at least as current as we are - which is what distinguishes "art
    // that has outrun its snapshot" from "art for a track we already left".
    Tuple *video = dict_find(iterator, MESSAGE_KEY_VIDEO_ID);
    char incoming_id[TEXT_LENGTH];
    incoming_id[0] = '\0';
    if (video) copy_tuple_text(incoming_id, sizeof(incoming_id), video);
    bool id_matches_now_playing =
        incoming_id[0] != '\0' && s_now_playing.video_id[0] != '\0' &&
        strncmp(incoming_id, s_now_playing.video_id, TEXT_LENGTH) == 0;
    // No videoId at all means an older companion (or the Symfonium backend, whose
    // media id can be absent); those peers only support the generation gate.
    if (!id_matches_now_playing && generation < s_stream_generation) return;
    int total_bytes = total->value->int32;
    int w = width->value->int32;
    int h = height->value->int32;
    // Accept either supported square size; the companion chooses by audio route.
    bool valid_dim = w == h && (w == COVER_ART_LOW_DIM || w == COVER_ART_HIGH_DIM);
    int color_bytes = w * h;
    int mono_bytes = (w / 8) * h;
    if (!valid_dim || (total_bytes != mono_bytes && total_bytes != color_bytes)) {
      clear_cover_art();
      return;
    }
    clear_cover_art();
    // Exactly what this transfer needs, not the 20 KB worst case. A 64x64 mono cover is
    // 512 bytes; asking for 20736 of a fragmented heap fails far more often than asking
    // for what is actually required, and a failed malloc here silently means no art.
    s_cover_art_data = malloc((size_t) total_bytes);
    if (!s_cover_art_data) {
      APP_LOG(APP_LOG_LEVEL_ERROR, "[CoverArt] out of heap for %d bytes", total_bytes);
      return;  // out of heap: leave cover art disabled for this track
    }
    s_cover_art_w = w;
    s_cover_art_h = h;
    s_cover_art_receiving = true;
    s_cover_art_color = total_bytes == color_bytes;
    s_cover_art_expected_bytes = total_bytes;
    strncpy(s_cover_art_video_id, incoming_id, TEXT_LENGTH - 1);
    s_cover_art_video_id[TEXT_LENGTH - 1] = '\0';
    arm_cover_art_timeout();
  } else if (command_tuple && command == EventCoverArtChunk) {
    if (!s_cover_art_receiving) return;
    // A transfer opened for a named track is owned by that track until it finishes or
    // stalls; only an anonymous one still answers to the generation counter. Re-gating
    // every chunk on the generation is what let a mid-transfer button press silently
    // truncate an image the watch had already committed a buffer to.
    if (s_cover_art_video_id[0] == '\0' && generation < s_stream_generation) return;
    Tuple *sequence = dict_find(iterator, MSG_IMAGE_SEQUENCE);
    Tuple *data = dict_find(iterator, MSG_IMAGE_DATA);
    if (!sequence || !data) {
      clear_cover_art();
      return;
    }
    int seq = sequence->value->int32;
    if (seq != s_cover_art_expected_sequence) {
      // A repeat of the chunk we just took is not corruption - it is the companion
      // re-sending one whose ack it never saw. Dropping the whole transfer for that
      // (as this used to) threw away the image over a message that arrived twice.
      if (seq == s_cover_art_expected_sequence - 1) {
        arm_cover_art_timeout();
        return;
      }
      clear_cover_art();
      return;
    }
    int remaining = s_cover_art_expected_bytes - s_cover_art_received_bytes;
    if (remaining <= 0 || (int) data->length > remaining) {
      clear_cover_art();
      return;
    }
    memcpy(s_cover_art_data + s_cover_art_received_bytes, data->value->data, data->length);
    s_cover_art_received_bytes += (int) data->length;
    s_cover_art_expected_sequence++;
    arm_cover_art_timeout();
    if (s_cover_art_received_bytes >= s_cover_art_expected_bytes) {
      cancel_cover_art_timeout();
      s_cover_art_receiving = false;
      s_cover_art_ready = true;
      update_cover_art_brightness();
      APP_LOG(APP_LOG_LEVEL_INFO,
              "[CoverArt] complete bytes=%d mode=%s dark=%s",
              s_cover_art_received_bytes,
              s_cover_art_color ? "color" : "mono",
              s_cover_art_dark ? "yes" : "no");
      layer_mark_dirty(s_canvas);
    }
  } else if (command_tuple && command == EventCoverArtClear) {
    if (generation_tuple && generation != s_stream_generation) return;
    clear_cover_art();
    layer_mark_dirty(s_canvas);
  } else if (command_tuple && command == EventAudioEnd) {
    if (generation != s_stream_generation) return;
    if (s_stream_open) speaker_stream_close();
    s_stream_open = false;
    snprintf(s_status, sizeof(s_status), "Playback finished");
    // Only leave the playback screen if we are still on it (the user may have
    // already navigated away); return to wherever playback was launched from.
    if (s_screen == ScreenPlaying || s_screen == ScreenPaused || s_screen == ScreenBuffering) {
      if (!nav_back()) set_screen(ScreenHome);
    }
    clear_playing_track();
  } else if (command_tuple && command == EventError) {
    if (generation_tuple && generation != s_stream_generation) return;
    Tuple *request = dict_find(iterator, MESSAGE_KEY_SEARCH_REQUEST_ID);
    if (request && request->value->int32 != s_search_request_id) return;
    if (!generation_tuple && !s_search_active && s_screen != ScreenBuffering) return;
    s_search_active = false;
    clear_playing_track();
    Tuple *status = dict_find(iterator, MESSAGE_KEY_STATUS);
    if (status) {
      copy_tuple_text(s_status, sizeof(s_status), status);
    }
    if (!status || s_status[0] == '\0') {
      snprintf(s_status, sizeof(s_status), "Unknown playback error");
    }
    stop_audio();
    set_screen(ScreenError);
  } else if (command_tuple && command == EventStateSnapshot) {
    Tuple *version = dict_find(iterator, MESSAGE_KEY_PROTOCOL_VERSION);
    Tuple *capabilities = dict_find(iterator, MESSAGE_KEY_CAPABILITIES);
    Tuple *state = dict_find(iterator, MESSAGE_KEY_PLAYBACK_STATE);
    if (!version || version->value->int32 < PROTOCOL_VERSION || !state) return;
    bridge_connected();
    s_companion_capabilities = capabilities ? capabilities->value->int32 : 0;
    // Never let a snapshot roll the stream generation backwards. Local actions (next,
    // previous, resume, new search, route-to-watch) bump it, and a snapshot that was
    // in flight from before the bump carries the older value — applying it would make
    // the companion's next audio start arrive with a "future" generation and be
    // discarded as stale, leaving the watch on a Playing screen with no stream open.
    if (generation_tuple && generation > s_stream_generation) {
      s_stream_generation = generation;
    }

    Tuple *route = dict_find(iterator, MESSAGE_KEY_AUDIO_ROUTE);
    Tuple *route_epoch = dict_find(iterator, MSG_ROUTE_EPOCH);
    Tuple *loop = dict_find(iterator, MESSAGE_KEY_LOOP_ENABLED);
    Tuple *loop_mode = dict_find(iterator, MSG_LOOP_MODE);
    Tuple *shuffle = dict_find(iterator, MSG_SHUFFLE_ENABLED);
    Tuple *duration = dict_find(iterator, MESSAGE_KEY_DURATION);
    Tuple *position = dict_find(iterator, MESSAGE_KEY_POSITION);
    Tuple *volume = dict_find(iterator, MESSAGE_KEY_VOLUME);
    Tuple *theme = dict_find(iterator, MSG_THEME);
    // Route application is epoch-gated: a snapshot generated before a local route
    // toggle carries the older epoch and must not revert it. Missing epoch means an
    // old companion — apply unconditionally, the pre-epoch behavior.
    if (route) {
      bool snapshot_route = route->value->int32 == 1;
      if (!route_epoch) {
        s_phone_audio = snapshot_route;
        persist_write_int(AUDIO_ROUTE_KEY, s_phone_audio);
      } else if (route_epoch->value->int32 > s_route_epoch) {
        s_route_epoch = route_epoch->value->int32;
        s_phone_audio = snapshot_route;
        persist_write_int(ROUTE_EPOCH_KEY, s_route_epoch);
        persist_write_int(AUDIO_ROUTE_KEY, s_phone_audio);
      } else if (route_epoch->value->int32 == s_route_epoch && snapshot_route != s_phone_audio) {
        // Equal epoch but a different route means both sides changed at once; the
        // watch wins ties, so keep ours and re-assert it.
        send_audio_route();
      }
    }
    if (volume) {
      s_phone_volume = volume->value->int32 < 0 ? 0 :
                       volume->value->int32 > 100 ? 100 : volume->value->int32;
      persist_write_int(PHONE_VOLUME_KEY, s_phone_volume);
    }
    if (theme) {
      int value = theme->value->int32;
      if (value < ThemeTeal) value = ThemeTeal;
      if (value > ThemeMono) value = ThemeMono;
      s_theme = (AppTheme) value;
      persist_write_int(THEME_KEY, s_theme);
      menu_layer_set_highlight_colors(s_native_menu_layer, accent_color(), GColorWhite);
      menu_layer_reload_data(s_native_menu_layer);
    }
    if (loop_mode) {
      int mode = loop_mode->value->int32;
      if (mode < LoopModeOff) mode = LoopModeOff;
      if (mode > LoopModeAll) mode = LoopModeAll;
      s_loop_mode = (uint8_t) mode;
      s_loop_enabled = s_loop_mode != LoopModeOff;
    } else {
      // Backward-compat fallback for older companions that only report loop-one.
      s_loop_enabled = loop && loop->value->int32 != 0;
      s_loop_mode = s_loop_enabled ? LoopModeOne : LoopModeOff;
    }
    s_shuffle_enabled = shuffle && shuffle->value->int32 != 0;
    s_duration_seconds = duration ? duration->value->int32 / 1000 : 0;
    s_elapsed_seconds = position ? position->value->int32 / 1000 : 0;
    s_played_samples = s_elapsed_seconds * 16000;

    Tuple *video = dict_find(iterator, MESSAGE_KEY_VIDEO_ID);
    Tuple *title = dict_find(iterator, MESSAGE_KEY_TITLE);
    Tuple *artist = dict_find(iterator, MESSAGE_KEY_ARTIST);
    if (video && title && artist) {
      s_has_now_playing = true;
      copy_tuple_text(s_now_playing.video_id, TEXT_LENGTH, video);
      copy_tuple_text(s_now_playing.title, TEXT_LENGTH, title);
      copy_tuple_text(s_now_playing.artist, TEXT_LENGTH, artist);
      // The snapshot is the authority on which track we are on, so it is also what
      // decides whether the cover we are holding still belongs. This is the other half
      // of the relaxed acceptance in EventCoverArtStart: art that turns out to be
      // tagged for a different track is dropped here rather than lingering over the new
      // one. Only *finished* art is evicted - a transfer still arriving is left alone,
      // because a snapshot that was already in flight can name the previous track and
      // would otherwise truncate the very image it is about to be superseded by.
      if (s_cover_art_ready &&
          s_cover_art_video_id[0] != '\0' &&
          s_now_playing.video_id[0] != '\0' &&
          strncmp(s_cover_art_video_id, s_now_playing.video_id, TEXT_LENGTH) != 0) {
        clear_cover_art();
        layer_mark_dirty(s_canvas);
      }
    }
    Tuple *favorite = dict_find(iterator, MESSAGE_KEY_IS_FAVORITE);
    s_current_favorite = favorite && favorite->value->int32 != 0;

    int32_t playback_state = state->value->int32;
    // The snapshot is the authority on playing-vs-paused. This used to be latched
    // only by set_screen(), which the block below runs solely on the first snapshot
    // or while already on a playback screen - and never at all when s_result_count
    // is 0, as it is whenever playback was started from the phone rather than from a
    // watch search. Home therefore read a flag nothing had ever set and captioned
    // every track "PAUSED".
    s_playback_active = playback_state == PlaybackPlaying;
    if (s_phone_audio || playback_state != PlaybackPlaying) stop_audio();

    // Snapshots never choose the screen; they only keep the screen we are already on
    // in step. Opening the watchapp while the phone happens to be playing used to land
    // straight on Now Playing, because the first snapshot after launch was allowed to
    // navigate - so the app opened somewhere the user had not asked to go, and Home
    // (which shows the now-playing hero perfectly well) was skipped entirely. Playback
    // the user starts *from the watch* still gets there: play_selected() pushes
    // ScreenBuffering itself, which is a playback screen, so the branch below applies.
    bool on_playback_screen = s_screen == ScreenPlaying || s_screen == ScreenPaused ||
                              s_screen == ScreenBuffering;
    s_snapshot_applied = true;
    if (on_playback_screen) {
      // Key off having a now-playing track, not s_result_count: playback started from
      // the phone (or any path that never filled the results array) left the screen
      // stuck on ScreenPlaying after a pause, so the play/pause toggle kept sending
      // Pause and resume was unreachable. current_playing_result() draws from
      // s_now_playing, which the snapshot fills in above whenever a track is active.
      bool has_track = s_has_now_playing || s_result_count > 0;
      if (has_track && playback_state == PlaybackPlaying) {
        set_screen(ScreenPlaying);
        start_progress_timer();
      } else if (has_track && playback_state == PlaybackPaused) {
        set_screen(ScreenPaused);
      } else if (has_track && playback_state == PlaybackBuffering) {
        set_screen(ScreenBuffering);
      } else if (playback_state == PlaybackIdle && on_playback_screen) {
        // Playback stopped/ended externally while we were watching it: leave the
        // playback screen by retracing the Back history (falls back to Home).
        if (!nav_back()) set_screen(ScreenHome);
        clear_playing_track();
      }
    }
  } else if (command_tuple && command == EventLibraryItem) {
    Tuple *type = dict_find(iterator, MESSAGE_KEY_LIBRARY_TYPE);
    Tuple *index = dict_find(iterator, MESSAGE_KEY_RESULT_INDEX);
    Tuple *video = dict_find(iterator, MESSAGE_KEY_VIDEO_ID);
    Tuple *title = dict_find(iterator, MESSAGE_KEY_TITLE);
    Tuple *artist = dict_find(iterator, MESSAGE_KEY_ARTIST);
    if (!type || type->value->int32 != s_library_type || !index || !video || !title || !artist) return;
    int i = index->value->int32;
    if (i < 0 || i >= MAX_LIBRARY) return;
    copy_tuple_text(s_results[i].video_id, TEXT_LENGTH, video);
    copy_tuple_text(s_results[i].title, TEXT_LENGTH, title);
    copy_tuple_text(s_results[i].artist, TEXT_LENGTH, artist);
    if (i + 1 > s_result_count) s_result_count = i + 1;
  } else if (command_tuple && command == EventLibraryComplete) {
    Tuple *type = dict_find(iterator, MESSAGE_KEY_LIBRARY_TYPE);
    if (!type || type->value->int32 != s_library_type) return;
    s_library_loading = false;
    sync_native_menu(false);
    layer_mark_dirty(s_canvas);
  } else if (command_tuple && command == EventQueueItem) {
    // Guarded on the current screen (rather than a request id, like Library's
    // library-type guard) so a response that arrives after the user already backed
    // out of the Queue screen is silently dropped instead of corrupting s_results.
    if (s_screen != ScreenQueue) return;
    Tuple *index = dict_find(iterator, MESSAGE_KEY_RESULT_INDEX);
    Tuple *video = dict_find(iterator, MESSAGE_KEY_VIDEO_ID);
    Tuple *title = dict_find(iterator, MESSAGE_KEY_TITLE);
    Tuple *artist = dict_find(iterator, MESSAGE_KEY_ARTIST);
    if (!index || !video || !title || !artist) return;
    int i = index->value->int32;
    if (i < 0 || i >= MAX_LIBRARY) return;
    copy_tuple_text(s_results[i].video_id, TEXT_LENGTH, video);
    copy_tuple_text(s_results[i].title, TEXT_LENGTH, title);
    copy_tuple_text(s_results[i].artist, TEXT_LENGTH, artist);
    if (i + 1 > s_result_count) s_result_count = i + 1;
    layer_mark_dirty(s_canvas);
  } else if (command_tuple && command == EventQueueComplete) {
    if (s_screen != ScreenQueue) return;
    s_queue_loading = false;
    // In stock style this is the point screen_uses_native_menu(ScreenQueue) flips
    // true (loading just cleared) - sync_native_menu() is what actually shows/
    // populates the MenuLayer, same as EventLibraryComplete. Without it the overlay
    // canvas stayed on screen showing nothing until some other input (e.g. UP/DOWN)
    // incidentally called sync_native_menu() as a side effect.
    sync_native_menu(false);
    layer_mark_dirty(s_canvas);
  } else if (command_tuple && command == EventFavoriteState) {
    // Companion confirms/corrects the favorite state for the current track.
    Tuple *favorite = dict_find(iterator, MESSAGE_KEY_IS_FAVORITE);
    Tuple *video = dict_find(iterator, MESSAGE_KEY_VIDEO_ID);
    // The payload may not be NUL-terminated, so compare via a bounded copy.
    char event_video_id[TEXT_LENGTH];
    copy_tuple_text(event_video_id, sizeof(event_video_id), video);
    const SearchResult *playing = current_playing_result();
    if (favorite &&
        (!video || (playing && strcmp(event_video_id, playing->video_id) == 0))) {
      s_current_favorite = favorite->value->int32 != 0;
      layer_mark_dirty(s_canvas);
    }
  } else if (command_tuple && command == CommandSyncSettings) {
    APP_LOG(APP_LOG_LEVEL_INFO, "[ClaySync] Received CommandSyncSettings request from JS");
    send_settings_sync();
  }

  Tuple *config_route = dict_find(iterator, MESSAGE_KEY_CONFIG_AUDIO_ROUTE);
  Tuple *config_watch_volume = dict_find(iterator, MESSAGE_KEY_CONFIG_WATCH_VOLUME);
  Tuple *config_phone_volume = dict_find(iterator, MESSAGE_KEY_CONFIG_PHONE_VOLUME);
  Tuple *config_input_mode = dict_find(iterator, MESSAGE_KEY_CONFIG_INPUT_MODE);
  Tuple *config_show_progress = dict_find(iterator, MSG_CONFIG_SHOW_PROGRESS);
  Tuple *config_cache_enabled = dict_find(iterator, MSG_CONFIG_CACHE_ENABLED);
  Tuple *config_cache_size_mb = dict_find(iterator, MSG_CONFIG_CACHE_SIZE_MB);
  Tuple *config_cover_art_bg = dict_find(iterator, MSG_CONFIG_COVER_ART_BG);
  Tuple *config_theme = dict_find(iterator, MSG_THEME);
  Tuple *config_watch_quality = dict_find(iterator, MSG_CONFIG_WATCH_AUDIO_QUALITY);
  Tuple *config_phone_quality = dict_find(iterator, MSG_CONFIG_PHONE_AUDIO_QUALITY);
  Tuple *config_cache_radio = dict_find(iterator, MSG_CONFIG_CACHE_RADIO);
  bool config_changed = false;
  if (config_route) {
    s_phone_audio = config_route->value->int32 == 1;
    APP_LOG(APP_LOG_LEVEL_INFO, "[ClaySync] CONFIG_AUDIO_ROUTE=%d", s_phone_audio ? 1 : 0);
    s_route_epoch++;
    persist_write_int(ROUTE_EPOCH_KEY, s_route_epoch);
    persist_write_int(AUDIO_ROUTE_KEY, s_phone_audio);
    send_audio_route();
    config_changed = true;
  }
  if (config_watch_volume) {
    int value = config_watch_volume->value->int32;
    s_watch_volume = value < 0 ? 0 : value > 100 ? 100 : value;
    APP_LOG(APP_LOG_LEVEL_INFO, "[ClaySync] CONFIG_WATCH_VOLUME=%d", s_watch_volume);
    persist_write_int(WATCH_VOLUME_KEY, s_watch_volume);
    config_changed = true;
  }
  if (config_phone_volume) {
    int value = config_phone_volume->value->int32;
    s_phone_volume = value < 0 ? 0 : value > 100 ? 100 : value;
    APP_LOG(APP_LOG_LEVEL_INFO, "[ClaySync] CONFIG_PHONE_VOLUME=%d", s_phone_volume);
    persist_write_int(PHONE_VOLUME_KEY, s_phone_volume);
    send_phone_volume();
    config_changed = true;
  }
  if (config_input_mode) {
    int value = config_input_mode->value->int32;
    if (value >= InputVoice && value <= InputAsk) {
      s_input_mode = value;
      APP_LOG(APP_LOG_LEVEL_INFO, "[ClaySync] CONFIG_INPUT_MODE=%d", s_input_mode);
      persist_write_int(INPUT_MODE_KEY, s_input_mode);
      config_changed = true;
    }
  }
  if (config_show_progress) {
    s_show_progress = config_show_progress->value->int32 != 0;
    APP_LOG(APP_LOG_LEVEL_INFO, "[ClaySync] CONFIG_SHOW_PROGRESS=%d", s_show_progress ? 1 : 0);
    persist_write_bool(SHOW_PROGRESS_KEY, s_show_progress);
    config_changed = true;
  }
  if (config_cache_enabled) {
    s_cache_enabled = config_cache_enabled->value->int32 != 0;
    APP_LOG(APP_LOG_LEVEL_INFO, "[ClaySync] CONFIG_CACHE_ENABLED=%d", s_cache_enabled ? 1 : 0);
    persist_write_bool(CACHE_ENABLED_KEY, s_cache_enabled);
    config_changed = true;
  }
  if (config_cache_size_mb) {
    int value = config_cache_size_mb->value->int32;
    if (value < 100) value = 100;
    if (value > 1000) value = 1000;
    value = (value / 50) * 50;
    if (value < 100) value = 100;
    s_cache_size_mb = (uint16_t) value;
    APP_LOG(APP_LOG_LEVEL_INFO, "[ClaySync] CONFIG_CACHE_SIZE_MB=%d", s_cache_size_mb);
    persist_write_int(CACHE_SIZE_MB_KEY, s_cache_size_mb);
    config_changed = true;
  }
  if (config_cover_art_bg) {
    s_cover_art_background = config_cover_art_bg->value->int32 != 0;
    APP_LOG(APP_LOG_LEVEL_INFO, "[ClaySync] CONFIG_COVER_ART_BG=%d", s_cover_art_background ? 1 : 0);
    persist_write_bool(COVER_ART_BG_KEY, s_cover_art_background);
    config_changed = true;
  }
  if (config_theme) {
    int value = config_theme->value->int32;
    if (value < ThemeTeal) value = ThemeTeal;
    if (value > ThemeMono) value = ThemeMono;
    s_theme = (AppTheme) value;
    APP_LOG(APP_LOG_LEVEL_INFO, "[ClaySync] THEME=%d", (int) s_theme);
    persist_write_int(THEME_KEY, s_theme);
    menu_layer_set_highlight_colors(s_native_menu_layer, accent_color(), GColorWhite);
    menu_layer_reload_data(s_native_menu_layer);
    config_changed = true;
  }
  if (config_watch_quality) {
    s_watch_audio_quality = config_watch_quality->value->int32 != 0;
    APP_LOG(APP_LOG_LEVEL_INFO, "[ClaySync] CONFIG_WATCH_AUDIO_QUALITY=%d", s_watch_audio_quality ? 1 : 0);
    persist_write_bool(WATCH_AUDIO_QUALITY_KEY, s_watch_audio_quality);
    config_changed = true;
  }
  if (config_phone_quality) {
    s_phone_audio_quality = config_phone_quality->value->int32 != 0;
    APP_LOG(APP_LOG_LEVEL_INFO, "[ClaySync] CONFIG_PHONE_AUDIO_QUALITY=%d", s_phone_audio_quality ? 1 : 0);
    persist_write_bool(PHONE_AUDIO_QUALITY_KEY, s_phone_audio_quality);
    config_changed = true;
  }
  if (config_cache_radio) {
    s_cache_radio = config_cache_radio->value->int32 != 0;
    APP_LOG(APP_LOG_LEVEL_INFO, "[ClaySync] CONFIG_CACHE_RADIO=%d", s_cache_radio ? 1 : 0);
    persist_write_bool(CACHE_RADIO_KEY, s_cache_radio);
    config_changed = true;
  }
  if (config_changed) send_settings_sync();
  if (config_route || config_watch_volume || config_phone_volume || config_input_mode ||
      config_show_progress || config_cache_enabled || config_cache_size_mb ||
      config_cover_art_bg || config_theme || config_watch_quality || config_phone_quality ||
      config_cache_radio) {
    layer_mark_dirty(s_canvas);
  }
}

// While artwork-only mode is showing, the first hardware-button press just restores
// the UI (and performs no other action) so nothing is triggered blindly behind the
// hidden controls. Safe to call from any handler: s_artwork_only is only ever set on
// the now-playing screen.
#define RESTORE_UI_IF_ARTWORK_ONLY() do { \
    if (s_artwork_only) { s_artwork_only = false; layer_mark_dirty(s_canvas); return; } \
  } while (0)

// ---------------------------------------------------------------------------
// Now-playing actions, shared by the hardware buttons, the touchscreen gestures,
// and the "More" popup so each control has a single implementation.
// ---------------------------------------------------------------------------

static void np_toggle_play_pause(void) {
  if (s_screen == ScreenPlaying) {
    send_generation_command(CommandPause, NULL, 0);
    show_feedback(FeedbackPause);
  } else if (s_screen == ScreenPaused) {
    s_stream_generation++;
    send_generation_command(CommandResume, NULL, 0);
    show_feedback(FeedbackPlay);
  }
}

static void np_next(void) {
  s_stream_generation++;
  send_generation_command(CommandNext, NULL, 0);
  show_feedback(FeedbackNext);
}

static void np_previous(void) {
  s_stream_generation++;
  send_generation_command(CommandPrevious, NULL, 0);
  show_feedback(FeedbackPrev);
}

static void np_volume_up(void) {
  if (s_phone_audio) {
    s_phone_volume = s_phone_volume >= 90 ? 100 : s_phone_volume + 10;
    persist_write_int(PHONE_VOLUME_KEY, s_phone_volume);
    send_phone_volume();
  } else {
    s_watch_volume = s_watch_volume >= 90 ? 100 : s_watch_volume + 10;
    persist_write_int(WATCH_VOLUME_KEY, s_watch_volume);
    if (s_stream_open) speaker_set_volume(s_watch_volume);
  }
  show_volume_temporarily();
}

static void np_volume_down(void) {
  if (s_phone_audio) {
    s_phone_volume = s_phone_volume <= 10 ? 0 : s_phone_volume - 10;
    persist_write_int(PHONE_VOLUME_KEY, s_phone_volume);
    send_phone_volume();
  } else {
    s_watch_volume = s_watch_volume <= 10 ? 0 : s_watch_volume - 10;
    persist_write_int(WATCH_VOLUME_KEY, s_watch_volume);
    if (s_stream_open) speaker_set_volume(s_watch_volume);
  }
  show_volume_temporarily();
}

static void np_cycle_loop(void) {
  s_loop_mode = (uint8_t) ((s_loop_mode + 1) % 3);
  s_loop_enabled = s_loop_mode != LoopModeOff;
  persist_write_int(LOOP_MODE_KEY, s_loop_mode);
  send_command(CommandToggleLoop, NULL, 0);
  layer_mark_dirty(s_canvas);
}

static void np_toggle_shuffle(void) {
  s_shuffle_enabled = !s_shuffle_enabled;
  if (!request_shuffle_play()) {
    s_shuffle_enabled = !s_shuffle_enabled;
    vibes_short_pulse();
    return;
  }
  show_feedback(s_shuffle_enabled ? FeedbackShuffleOn : FeedbackShuffleOff);
  vibes_short_pulse();
}

static void np_toggle_favorite(void) {
  const SearchResult *playing = current_playing_result();
  if (!playing) return;
  s_current_favorite = !s_current_favorite;
  send_command(CommandToggleFavorite, playing->video_id, MESSAGE_KEY_VIDEO_ID);
  show_feedback(s_current_favorite ? FeedbackFavoriteOn : FeedbackFavoriteOff);
  vibes_short_pulse();
}

static void np_toggle_output(void) {
  s_phone_audio = !s_phone_audio;
  s_route_epoch++;
  persist_write_int(ROUTE_EPOCH_KEY, s_route_epoch);
  persist_write_int(AUDIO_ROUTE_KEY, s_phone_audio);
  if (!s_phone_audio) s_stream_generation++;
  send_audio_route();
  if (s_phone_audio && s_stream_open) {
    speaker_stream_close();
    s_stream_open = false;
  }
  if (s_screen == ScreenPlaying) start_progress_timer();
  show_feedback(s_phone_audio ? FeedbackOutputPhone : FeedbackOutputWatch);
}

static void np_new_search(void) {
  // A quick voice search that skips the Song/Artist picker, so reset the mode
  // explicitly - otherwise it inherits SearchModeArtist from a prior Artist Radio
  // search and silently runs an artist search instead.
  s_search_mode = SearchModeSong;
  send_generation_command(CommandStop, NULL, 0);
  s_stream_generation++;
  stop_audio();
  clear_playing_track();
  start_search();
}

// ---- "More" popup (button-driven list of the less-frequent now-playing actions) ----

// Queue only has a defined "up next" when Next/Previous do (see skipTrack() on the
// companion side, mirrored by currentQueueList()) - loop-all or shuffle. Otherwise
// there's nothing to show, so the action is hidden rather than opening an empty list.
static int np_more_count(void) {
  return (s_loop_mode == LoopModeAll || s_shuffle_enabled) ? NP_MORE_COUNT : NP_MORE_COUNT - 1;
}

static void np_more_open(void) {
  s_np_more_open = true;
  s_np_more_selection = 0;
  layer_mark_dirty(s_canvas);
}

static void np_more_close(void) {
  s_np_more_open = false;
  layer_mark_dirty(s_canvas);
}

static void np_more_move(int delta) {
  int count = np_more_count();
  s_np_more_selection = (s_np_more_selection + count + delta) % count;
  layer_mark_dirty(s_canvas);
}

static void np_more_activate(void) {
  switch (s_np_more_selection) {
    case 0: np_toggle_shuffle(); break;
    case 1: np_cycle_loop(); break;
    case 2: np_toggle_favorite(); break;
    case 3: np_toggle_output(); break;
    case 4: np_more_close(); np_new_search(); return;  // leaves the now-playing screen
    case 5: np_more_close(); open_queue(); return;     // leaves the now-playing screen
  }
  // Toggles keep the popup open so several can be changed in a row; it reflects the new
  // state and is dismissed with BACK.
  layer_mark_dirty(s_canvas);
}

static void select_click(ClickRecognizerRef recognizer, void *context) {
  RESTORE_UI_IF_ARTWORK_ONLY();
  if (screen_uses_native_menu(s_screen) && s_native_menu_layer) {
    MenuIndex selected = menu_layer_get_selected_index(s_native_menu_layer);
    s_menu_selection = selected.row;
  }
  if (s_screen == ScreenKeyboard) {
    if (s_keyboard_pt2) {
      if (s_query_length < TEXT_LENGTH - 1) {
        s_query[s_query_length++] = '\n';
        s_query[s_query_length] = '\0';
        layer_mark_dirty(s_canvas);
      }
    } else {
      keyboard_choose(1);
    }
  } else if (s_screen == ScreenHome && s_bespoke_ui) {
    // The dock Home is a menu, so SELECT opens the highlighted destination using the
    // same actions ScreenMenu's own SELECT runs (MENU_ITEMS is shared between the two).
    const int hero_rows = home_hero_selectable() ? 1 : 0;
    int row = s_home_selection - hero_rows;
    // A stale selection can outlive the track that made the hero selectable.
    if (row < 0 || row >= (int) ARRAY_LENGTH(MENU_ITEMS)) row = 0;
    if (hero_rows && s_home_selection == 0) {
      // The hero opens what it is describing. nav_push so Back returns to Home.
      nav_push(s_playback_active ? ScreenPlaying : ScreenPaused);
      if (s_playback_active) start_progress_timer();
    } else if (row == 0) {
      begin_configured_search();
    } else if (row == 1) {
      s_menu_selection = 0;
      nav_push(ScreenLibrary);
    } else if (row == 2) {
      s_menu_selection = 0;
      nav_push(ScreenSettings);
    } else {
      s_about_select_count = 0;
      nav_push(ScreenAbout);
    }
  } else if (s_screen == ScreenHome || (s_screen == ScreenResults && s_result_count == 0)) {
    begin_configured_search();
  } else if (s_screen == ScreenResults) {
    if (s_native_menu_layer && screen_uses_native_menu(s_screen)) {
      s_selected_result = menu_layer_get_selected_index(s_native_menu_layer).row;
    }
    play_selected();
  } else if (s_screen == ScreenPlaying || s_screen == ScreenPaused) {
    if (s_np_more_open) {
      np_more_activate();
    } else {
      np_toggle_play_pause();
    }
  } else if (s_screen == ScreenBuffering) {
    send_command(CommandStop, NULL, 0);
    s_stream_generation++;
    stop_audio();
    clear_playing_track();
    set_screen(ScreenResults);
  } else if (s_screen == ScreenError) {
    if (s_result_count > 0) play_selected(); else start_search();
  } else if (s_screen == ScreenLibrary) {
    int menu = s_menu_selection;
    if (menu < 0 || menu >= (int) ARRAY_LENGTH(LIBRARY_TYPES)) menu = 0;
    s_library_type = LIBRARY_TYPES[menu];
    s_result_count = 0;
    s_selected_result = 0;
    s_library_loading = true;
    nav_push(ScreenLibraryItems);
    if (!request_library(s_library_type)) s_library_loading = false;
  } else if (s_screen == ScreenLibraryItems) {
    if (!s_library_loading && s_result_count > 0) {
      if (s_native_menu_layer && screen_uses_native_menu(s_screen)) {
        s_selected_result = menu_layer_get_selected_index(s_native_menu_layer).row;
      }
      if (s_library_type == LibraryRecentSearches) {
        // Selecting a recent search re-runs it and shows the results.
        if (!submit_recent_search(s_results[s_selected_result].video_id)) vibes_short_pulse();
      } else {
        play_selected();
      }
    }
  } else if (s_screen == ScreenQueue) {
    if (!s_queue_loading && s_result_count > 0) {
      // Jump within the active queue (CommandQueueJump) rather than CommandPlay, so
      // the phone keeps the shuffle/radio session going instead of tearing it down
      // the way picking a song from Results/Library does. The next state snapshot
      // refreshes s_now_playing, same as a hardware Next/Previous press.
      send_generation_command(CommandQueueJump, s_results[s_selected_result].video_id,
                              MESSAGE_KEY_VIDEO_ID);
      nav_back();
    }
  } else if (s_screen == ScreenMenu) {
    if (s_menu_selection == 0) {
      begin_configured_search();
    } else if (s_menu_selection == 1) {
      s_menu_selection = 0;
      nav_push(ScreenLibrary);
    } else if (s_menu_selection == 2) {
      s_menu_selection = 0;
      nav_push(ScreenSettings);
    } else {
      s_about_select_count = 0;
      nav_push(ScreenAbout);
    }
  } else if (s_screen == ScreenAbout) {
    // Hidden unlock: 7 SELECT presses on the About screen toggles whether Advanced
    // shows just Keyboard or everything (see s_advanced_unlocked/advanced_item_count()).
    // No visible per-press feedback beyond a vibe on the 7th.
    s_about_select_count++;
    if (s_about_select_count >= 7) {
      s_about_select_count = 0;
      s_advanced_unlocked = !s_advanced_unlocked;
      persist_write_bool(ADVANCED_UNLOCKED_KEY, s_advanced_unlocked);
      vibes_short_pulse();
    }
  } else if (s_screen == ScreenSettings) {
    if (s_menu_selection == 0) {
      s_input_mode = (InputMode) ((s_input_mode + 1) % 3);
      persist_write_int(INPUT_MODE_KEY, s_input_mode);
    } else if (s_menu_selection == 1) {
      bool previous_route = s_phone_audio;
      s_phone_audio = !s_phone_audio;
      s_route_epoch++;
      if (!s_phone_audio) s_stream_generation++;
      if (send_audio_route()) {
        persist_write_int(ROUTE_EPOCH_KEY, s_route_epoch);
        persist_write_int(AUDIO_ROUTE_KEY, s_phone_audio);
      } else {
        if (!s_phone_audio) s_stream_generation--;
        s_route_epoch--;
        s_phone_audio = previous_route;
      }
    } else if (s_menu_selection == 2) {
      s_watch_volume = s_watch_volume >= 100 ? 0 : s_watch_volume + 10;
      persist_write_int(WATCH_VOLUME_KEY, s_watch_volume);
    } else if (s_menu_selection == 3) {
      uint8_t previous_volume = s_phone_volume;
      s_phone_volume = s_phone_volume >= 100 ? 0 : s_phone_volume + 10;
      if (send_phone_volume()) {
        persist_write_int(PHONE_VOLUME_KEY, s_phone_volume);
      } else {
        s_phone_volume = previous_volume;
      }
    } else if (s_menu_selection == 4) {
      s_show_progress = !s_show_progress;
      persist_write_bool(SHOW_PROGRESS_KEY, s_show_progress);
    } else if (s_menu_selection == 5) {
      s_menu_selection = 0;
      nav_push(ScreenAdvanced);
      return;
    }
    layer_mark_dirty(s_canvas);
  } else if (s_screen == ScreenAdvanced) {
    if (s_menu_selection == 0) {
#ifdef PBL_PLATFORM_EMERY
      s_keyboard_pt2 = !s_keyboard_pt2;
      persist_write_bool(KEYBOARD_STYLE_KEY, s_keyboard_pt2);
#else
      vibes_short_pulse();
#endif
    } else if (s_menu_selection == 1) {
      s_bespoke_ui = !s_bespoke_ui;
      persist_write_bool(BESPOKE_UI_KEY, s_bespoke_ui);
      s_home_selection = 0;
      if (s_bespoke_ui) {
        // Bespoke Home draws no artwork, so hand the full-screen bitmap's heap back.
        // Turning it off reloads the art via ensure_home_background().
        if (s_home_background) {
          gbitmap_destroy(s_home_background);
          s_home_background = NULL;
        }
        s_home_background_variant = -1;
      } else {
        s_home_bg_failed_variant = -1;
        ensure_home_background();
      }
      // This screen is itself a list, so swap it between the MenuLayer and the canvas
      // right now rather than leaving the old renderer on screen until the next nav.
      sync_native_menu(false);
    } else if (s_menu_selection == 2) {
      s_theme = s_theme == ThemeDefault ? ThemeTeal :
                s_theme == ThemeTeal ? ThemePurple :
                s_theme == ThemePurple ? ThemeSunset :
                s_theme == ThemeSunset ? ThemeMono : ThemeDefault;
      persist_write_int(THEME_KEY, s_theme);
      send_settings_sync();
      menu_layer_set_highlight_colors(s_native_menu_layer, accent_color(), GColorWhite);
      menu_layer_reload_data(s_native_menu_layer);
      // The Mono theme swaps to dedicated black & white Home art; swap it now so
      // Home is ready before it is shown, otherwise the first Home paint would
      // flash the previous background while loading.
      s_home_bg_failed_variant = -1;
      if (!s_bespoke_ui) ensure_home_background();
    } else if (s_menu_selection == 3) {
      s_alt_home = !s_alt_home;
      persist_write_bool(ALT_HOME_KEY, s_alt_home);
      // Swap the background now so Home is ready before it is shown, otherwise the
      // first Home paint would flash the empty (white) background while loading.
      s_home_bg_failed_variant = -1;
      if (!s_bespoke_ui) ensure_home_background();
    } else if (s_menu_selection == 4) {
      s_show_home_quotes = !s_show_home_quotes;
      persist_write_bool(SHOW_HOME_QUOTES_KEY, s_show_home_quotes);
    } else if (s_menu_selection == 5) {
      s_cover_art_background = !s_cover_art_background;
      persist_write_bool(COVER_ART_BG_KEY, s_cover_art_background);
      send_settings_sync();
    } else if (s_menu_selection == 6) {
      s_history_limit += HISTORY_LIMIT_STEP;
      if (s_history_limit > HISTORY_LIMIT_MAX) s_history_limit = HISTORY_LIMIT_MIN;
      persist_write_int(HISTORY_LIMIT_KEY, s_history_limit);
    } else if (s_menu_selection == 7) {
      s_search_limit = s_search_limit == 5 ? 10 : 5;
      persist_write_int(SEARCH_LIMIT_KEY, s_search_limit);
    } else if (s_menu_selection == 8) {
      s_extra_library = !s_extra_library;
      persist_write_bool(EXTRA_LIBRARY_KEY, s_extra_library);
    } else if (s_menu_selection == 9) {
      s_watch_audio_quality = !s_watch_audio_quality;
      persist_write_bool(WATCH_AUDIO_QUALITY_KEY, s_watch_audio_quality);
      send_settings_sync();
    } else if (s_menu_selection == 10) {
      s_phone_audio_quality = !s_phone_audio_quality;
      persist_write_bool(PHONE_AUDIO_QUALITY_KEY, s_phone_audio_quality);
      send_settings_sync();
    } else {
      s_cache_radio = !s_cache_radio;
      persist_write_bool(CACHE_RADIO_KEY, s_cache_radio);
      send_settings_sync();
    }
    layer_mark_dirty(s_canvas);
  } else if (s_screen == ScreenInputChoice) {
    if (s_menu_selection == 0) start_search(); else open_keyboard();
  } else if (s_screen == ScreenSearchType) {
    s_search_mode = (SearchMode) s_menu_selection;
    continue_configured_search();
  }
}

static void select_long_click(ClickRecognizerRef recognizer, void *context) {
  RESTORE_UI_IF_ARTWORK_ONLY();
  if (s_screen == ScreenKeyboard) {
    if (!submit_search_query()) vibes_short_pulse();
  } else if (s_screen == ScreenPlaying || s_screen == ScreenPaused) {
    // Long-press SELECT opens the More popup (shuffle / loop / favorite / output /
    // new search). If it is already open, ignore.
    if (!s_np_more_open) np_more_open();
  } else if (s_screen == ScreenLibraryItems && s_library_type == LibraryCached &&
             !s_library_loading && s_result_count > 0) {
    // Long-press SELECT on the Cached Music list deletes the highlighted song from
    // the on-device cache. Removed from s_results immediately (rather than waiting
    // on a round trip) so the list updates right away; sync_native_menu() re-clamps
    // the selection and flips to the empty state if that was the last cached song.
    if (s_native_menu_layer && screen_uses_native_menu(s_screen)) {
      s_selected_result = menu_layer_get_selected_index(s_native_menu_layer).row;
    }
    send_command(CommandDeleteCached, s_results[s_selected_result].video_id, MESSAGE_KEY_VIDEO_ID);
    for (int i = s_selected_result; i < s_result_count - 1; i++) {
      s_results[i] = s_results[i + 1];
    }
    s_result_count--;
    vibes_short_pulse();
    sync_native_menu(true);
  }
}

static void up_click(ClickRecognizerRef recognizer, void *context) {
  RESTORE_UI_IF_ARTWORK_ONLY();
  if (s_screen == ScreenAbout) {
    scroll_to(s_scroll_target - 48);
  } else if ((s_screen == ScreenLibraryItems || s_screen == ScreenQueue) && s_result_count > 0) {
    // The app always drives the selection. On native-menu screens the MenuLayer is
    // stepped directly with the stock call; on canvas (bespoke) lists the app owns
    // the highlight and just repaints.
    if (screen_uses_native_menu(s_screen)) {
      native_menu_scroll_step(-1);
    } else {
      s_selected_result = (s_selected_result + s_result_count - 1) % s_result_count;
      layer_mark_dirty(s_canvas);
    }
  } else if (s_screen == ScreenKeyboard) {
    if (s_keyboard_pt2) {
      s_keyboard_mode = (s_keyboard_mode + 1) % 3;
      layer_mark_dirty(s_canvas);
    } else {
      keyboard_choose(0);
    }
  } else if (s_screen == ScreenHome) {
    if (s_bespoke_ui) {
      int count = (int) ARRAY_LENGTH(MENU_ITEMS) + (home_hero_selectable() ? 1 : 0);
      s_home_selection = (s_home_selection + count - 1) % count;
      layer_mark_dirty(s_canvas);
    } else {
      s_menu_selection = 0;
      s_ignore_menu_repeat = true;
      nav_push(ScreenLibrary);
    }
  } else if (s_screen == ScreenLibrary || s_screen == ScreenMenu ||
             s_screen == ScreenSettings || s_screen == ScreenAdvanced ||
             s_screen == ScreenInputChoice || s_screen == ScreenSearchType) {
    if (s_ignore_menu_repeat) return;
    if (screen_uses_native_menu(s_screen)) {
      native_menu_scroll_step(-1);
    } else {
      int count = current_menu_item_count();
      s_menu_selection = (s_menu_selection + count - 1) % count;
      sync_native_menu(true);
    }
  } else if (s_screen == ScreenResults && s_result_count > 0) {
    if (screen_uses_native_menu(s_screen)) {
      native_menu_scroll_step(-1);
    } else {
      s_selected_result = (s_selected_result + s_result_count - 1) % s_result_count;
      sync_native_menu(true);
    }
  } else if (s_screen == ScreenPlaying || s_screen == ScreenPaused) {
    if (s_np_more_open) {
      np_more_move(-1);
    } else {
      np_volume_up();
    }
  }
}

static void down_click(ClickRecognizerRef recognizer, void *context) {
  RESTORE_UI_IF_ARTWORK_ONLY();
  if (s_screen == ScreenAbout) {
    int16_t target = s_scroll_target + 48;
    if (target > s_scroll_max) target = s_scroll_max;
    scroll_to(target);
  } else if ((s_screen == ScreenLibraryItems || s_screen == ScreenQueue) && s_result_count > 0) {
    // Same as up_click: app-driven selection, stock MenuLayer step on native menus.
    if (screen_uses_native_menu(s_screen)) {
      native_menu_scroll_step(1);
    } else {
      s_selected_result = (s_selected_result + 1) % s_result_count;
      layer_mark_dirty(s_canvas);
    }
  } else if (s_screen == ScreenKeyboard) {
    if (s_keyboard_pt2) {
      keyboard_back_level();
    } else {
      keyboard_choose(2);
    }
  } else if (s_screen == ScreenHome) {
    if (s_bespoke_ui) {
      s_home_selection = (s_home_selection + 1) %
                         ((int) ARRAY_LENGTH(MENU_ITEMS) + (home_hero_selectable() ? 1 : 0));
      layer_mark_dirty(s_canvas);
    } else {
      s_menu_selection = 0;
      s_ignore_menu_repeat = true;
      nav_push(ScreenMenu);
    }
  } else if (s_screen == ScreenLibrary || s_screen == ScreenMenu ||
             s_screen == ScreenSettings || s_screen == ScreenAdvanced ||
             s_screen == ScreenInputChoice || s_screen == ScreenSearchType) {
    if (s_ignore_menu_repeat) return;
    if (screen_uses_native_menu(s_screen)) {
      native_menu_scroll_step(1);
    } else {
      int count = current_menu_item_count();
      s_menu_selection = (s_menu_selection + 1) % count;
      sync_native_menu(true);
    }
  } else if (s_screen == ScreenResults && s_result_count > 0) {
    if (screen_uses_native_menu(s_screen)) {
      native_menu_scroll_step(1);
    } else {
      s_selected_result = (s_selected_result + 1) % s_result_count;
      sync_native_menu(true);
    }
  } else if (s_screen == ScreenPlaying || s_screen == ScreenPaused) {
    if (s_np_more_open) {
      np_more_move(1);
    } else {
      np_volume_down();
    }
  }
}

static void back_click(ClickRecognizerRef recognizer, void *context) {
  RESTORE_UI_IF_ARTWORK_ONLY();
  // The keyboard consumes Back for delete/level navigation until it is empty.
  if (s_screen == ScreenKeyboard) {
    if (!s_keyboard_pt2 && keyboard_back_level()) return;
    // Nothing left to delete: fall through to normal history navigation.
  }

  if (s_screen == ScreenError) {
    if (!nav_back()) set_screen(ScreenHome);
    return;
  }

  // Queue was entered from Now Playing while it was a transient (unrecorded) screen
  // (see s_queue_return_screen), so it needs its own explicit way back rather than
  // nav_back(), which would otherwise retrace past Now Playing to whatever screen was
  // open before playback started. It was also opened from the More popup, so Back
  // reopens that rather than dropping straight to the bare Now Playing screen.
  if (s_screen == ScreenQueue) {
    set_screen(s_queue_return_screen);
    np_more_open();
    return;
  }

  // On the now-playing screen, Back first dismisses the More popup rather than
  // leaving playback.
  if ((s_screen == ScreenPlaying || s_screen == ScreenPaused) && s_np_more_open) {
    np_more_close();
    return;
  }

  // Leaving live playback/buffering: tear down the stream, then retrace history.
  if (s_screen == ScreenPlaying || s_screen == ScreenPaused ||
      s_screen == ScreenBuffering) {
    send_command(CommandStop, NULL, 0);
    s_stream_generation++;
    stop_audio();
    clear_playing_track();
  } else if (s_screen == ScreenSearching) {
    s_search_active = false;
  }

  // History-driven Back: return to the exact previous screen. When the stack is
  // empty we are at the root, so exit the app.
  if (!nav_back()) {
    if (s_screen == ScreenHome) {
      window_stack_pop(true);
    } else {
      set_screen(ScreenHome);
    }
  }
}

static void up_long_click(ClickRecognizerRef recognizer, void *context) {
  RESTORE_UI_IF_ARTWORK_ONLY();
  if (s_screen == ScreenKeyboard) {
    if (s_keyboard_pt2) {
      s_keyboard_mode = (s_keyboard_mode + 1) % 3;
      layer_mark_dirty(s_canvas);
      return;
    }
    s_keyboard_mode = (s_keyboard_mode + 1) % 3;
    reset_keyboard_level();
    layer_mark_dirty(s_canvas);
    return;
  }
  if (s_screen == ScreenPlaying || s_screen == ScreenPaused) {
    // Long-press UP goes to the PREVIOUS track (ignored while the More popup is open).
    //
    // UP/DOWN used to be the other way round here, which put Now Playing at odds with
    // every other screen in the app - UP steps a list toward earlier items everywhere
    // else - and with the Pebble convention the stock Music app set: UP is previous,
    // DOWN is next.
    if (s_np_more_open) return;
    np_previous();
  }
}

static void down_long_click(ClickRecognizerRef recognizer, void *context) {
  RESTORE_UI_IF_ARTWORK_ONLY();
  if (s_screen == ScreenKeyboard) {
    keyboard_back_level();
    return;
  }
  if (s_screen == ScreenPlaying || s_screen == ScreenPaused) {
    // Long-press DOWN skips to the NEXT track (ignored while More is open). Pairs with
    // up_long_click's previous - see the note there on why this order.
    if (s_np_more_open) return;
    np_next();
  }
}

static void button_raw_down(ClickRecognizerRef recognizer, void *context) {
  ButtonId button = click_recognizer_get_button_id(recognizer);
  bool playing_bar = (s_screen == ScreenPlaying || s_screen == ScreenPaused) && s_action_bar_visible;
  bool keyboard_bar = s_screen == ScreenKeyboard &&
                      (button == BUTTON_ID_UP || button == BUTTON_ID_SELECT || button == BUTTON_ID_DOWN);
  if (!playing_bar && !keyboard_bar) return;
  s_pressed_button = button;
  s_button_pressed = true;
  layer_mark_dirty(s_canvas);
}

static void button_raw_up(ClickRecognizerRef recognizer, void *context) {
  s_ignore_menu_repeat = false;
  if (s_button_pressed && s_pressed_button == click_recognizer_get_button_id(recognizer)) {
    s_button_pressed = false;
    layer_mark_dirty(s_canvas);
  }
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 600, select_long_click, NULL);
  // 100 ms repeat matches the stock MenuLayer's own Up/Down subscription.
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, up_click);
  window_long_click_subscribe(BUTTON_ID_UP, 600, up_long_click, NULL);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, down_click);
  window_long_click_subscribe(BUTTON_ID_DOWN, 600, down_long_click, NULL);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
  window_raw_click_subscribe(BUTTON_ID_UP, button_raw_down, button_raw_up, NULL);
  window_raw_click_subscribe(BUTTON_ID_SELECT, button_raw_down, button_raw_up, NULL);
  window_raw_click_subscribe(BUTTON_ID_DOWN, button_raw_down, button_raw_up, NULL);
  Window *window = context;
  bool on_overlay = window == s_overlay_window;
  bool menu_target = on_overlay ? s_overlay_visible : !s_overlay_visible;
  if (menu_target && s_native_menu_layer && screen_uses_native_menu(s_screen)) {
    menu_layer_set_click_config_onto_window(s_native_menu_layer, window);
  }
}

static void init(void) {
  // The results array is too big for static storage (see its declaration). If this
  // allocation fails the app cannot render any list — log and limp along; the heap
  // has tens of KB of headroom at this point, so this is a never-happen guard.
  s_results = malloc(sizeof(SearchResult) * MAX_ENTRIES);
  if (!s_results) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to allocate results array");
  }
  srand(time(NULL));
  s_home_quote = rand() % ARRAY_LENGTH(HOME_QUOTES);
  if (persist_exists(THEME_KEY)) {
    int theme = persist_read_int(THEME_KEY);
    if (theme == ThemeDefault || theme == ThemePurple ||
        theme == ThemeSunset || theme == ThemeTeal || theme == ThemeMono) {
      s_theme = theme;
    }
  }
  if (persist_exists(WATCH_VOLUME_KEY)) {
    int volume = persist_read_int(WATCH_VOLUME_KEY);
    s_watch_volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
  }
  if (persist_exists(PHONE_VOLUME_KEY)) {
    int volume = persist_read_int(PHONE_VOLUME_KEY);
    s_phone_volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
  }
  if (persist_exists(AUDIO_ROUTE_KEY)) s_phone_audio = persist_read_int(AUDIO_ROUTE_KEY) == 1;
  if (persist_exists(ROUTE_EPOCH_KEY)) s_route_epoch = persist_read_int(ROUTE_EPOCH_KEY);
  if (persist_exists(ALT_HOME_KEY)) s_alt_home = persist_read_bool(ALT_HOME_KEY);
  if (persist_exists(EXTRA_LIBRARY_KEY)) s_extra_library = persist_read_bool(EXTRA_LIBRARY_KEY);
  if (persist_exists(SHOW_HOME_QUOTES_KEY)) s_show_home_quotes = persist_read_bool(SHOW_HOME_QUOTES_KEY);
  if (persist_exists(HISTORY_LIMIT_KEY)) {
    int limit = persist_read_int(HISTORY_LIMIT_KEY);
    // Snap any stored value onto the 5/10/15/20 grid and clamp to the range.
    if (limit < HISTORY_LIMIT_MIN) limit = HISTORY_LIMIT_MIN;
    if (limit > HISTORY_LIMIT_MAX) limit = HISTORY_LIMIT_MAX;
    limit = (limit / HISTORY_LIMIT_STEP) * HISTORY_LIMIT_STEP;
    if (limit < HISTORY_LIMIT_MIN) limit = HISTORY_LIMIT_MIN;
    s_history_limit = limit;
  }
  if (persist_exists(SEARCH_LIMIT_KEY)) {
    int limit = persist_read_int(SEARCH_LIMIT_KEY);
    s_search_limit = limit >= 10 ? 10 : 5;
  }
  if (persist_exists(RECENT_SEARCH_LIMIT_KEY)) {
    int limit = persist_read_int(RECENT_SEARCH_LIMIT_KEY);
    if (limit < RECENT_SEARCH_LIMIT_MIN) limit = RECENT_SEARCH_LIMIT_MIN;
    if (limit > RECENT_SEARCH_LIMIT_MAX) limit = RECENT_SEARCH_LIMIT_MAX;
    limit = (limit / RECENT_SEARCH_LIMIT_STEP) * RECENT_SEARCH_LIMIT_STEP;
    if (limit < RECENT_SEARCH_LIMIT_MIN) limit = RECENT_SEARCH_LIMIT_MIN;
    s_recent_search_limit = limit;
  }
  if (persist_exists(INPUT_MODE_KEY)) {
    int input_mode = persist_read_int(INPUT_MODE_KEY);
    if (input_mode >= InputVoice && input_mode <= InputAsk) s_input_mode = input_mode;
  }
  if (persist_exists(KEYBOARD_STYLE_KEY)) {
    s_keyboard_pt2 = persist_read_bool(KEYBOARD_STYLE_KEY);
  }
  if (persist_exists(SHOW_PROGRESS_KEY)) {
    s_show_progress = persist_read_bool(SHOW_PROGRESS_KEY);
  }
  if (persist_exists(CACHE_ENABLED_KEY)) {
    s_cache_enabled = persist_read_bool(CACHE_ENABLED_KEY);
  }
  if (persist_exists(CACHE_SIZE_MB_KEY)) {
    int size_mb = persist_read_int(CACHE_SIZE_MB_KEY);
    if (size_mb < 100) size_mb = 100;
    if (size_mb > 1000) size_mb = 1000;
    size_mb = (size_mb / 50) * 50;
    if (size_mb < 100) size_mb = 100;
    s_cache_size_mb = (uint16_t) size_mb;
  }
  if (persist_exists(COVER_ART_BG_KEY)) {
    s_cover_art_background = persist_read_bool(COVER_ART_BG_KEY);
  }
  if (persist_exists(WATCH_AUDIO_QUALITY_KEY)) {
    s_watch_audio_quality = persist_read_bool(WATCH_AUDIO_QUALITY_KEY);
  }
  if (persist_exists(PHONE_AUDIO_QUALITY_KEY)) {
    s_phone_audio_quality = persist_read_bool(PHONE_AUDIO_QUALITY_KEY);
  }
  if (persist_exists(CACHE_RADIO_KEY)) {
    s_cache_radio = persist_read_bool(CACHE_RADIO_KEY);
  }
  if (persist_exists(BESPOKE_UI_KEY)) {
    s_bespoke_ui = persist_read_bool(BESPOKE_UI_KEY);
  }
  if (persist_exists(ADVANCED_UNLOCKED_KEY)) {
    s_advanced_unlocked = persist_read_bool(ADVANCED_UNLOCKED_KEY);
  }
  // LEGACY_THEME_KEY is not loaded - the legacy mascot theme it fed is disabled,
  // see s_legacy_theme's comment.
  clear_cover_art();
  if (persist_exists(LOOP_MODE_KEY)) {
    int loop_mode = persist_read_int(LOOP_MODE_KEY);
    if (loop_mode < LoopModeOff) loop_mode = LoopModeOff;
    if (loop_mode > LoopModeAll) loop_mode = LoopModeAll;
    s_loop_mode = (uint8_t) loop_mode;
    s_loop_enabled = s_loop_mode != LoopModeOff;
  }
#ifndef PBL_PLATFORM_EMERY
  s_keyboard_pt2 = false;
#endif
  s_home_quote = rand() % active_home_quote_count();
  update_time_text();
  tick_timer_service_subscribe(MINUTE_UNIT, minute_tick);
  s_window = window_create();
  s_overlay_window = window_create();
  // Load the background that matches the persisted Home style/theme; the other
  // variants are loaded on demand if the user toggles styles (only one is kept
  // in memory).
  s_home_background_variant = home_background_variant();
  if (home_is_pink_variant(s_home_background_variant)) {
    // Pink Unicorn home: flat fill + corner mascot, no full-screen background bitmap.
    s_home_background = NULL;
  } else {
    s_home_background = gbitmap_create_with_resource(
        home_background_resource(s_home_background_variant));
  }
  s_mascot_modern = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DREAMWAVE_MASCOT_MODERN);
  s_mascot_dreamhouse = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DREAMWAVE_MASCOT_DREAMHOUSE);
  s_mascot_ticket = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DREAMWAVE_MASCOT_TICKET);
  if (home_is_pink_variant(s_home_background_variant)) {
    s_mascot_home = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DREAMWAVE_MASCOT_HOME);
  }
  s_root_canvas = layer_create(GRect(0, 0, 200, 228));
  s_canvas = s_root_canvas;
  layer_set_update_proc(s_root_canvas, root_canvas_update);
  layer_add_child(window_get_root_layer(s_window), s_root_canvas);

  s_overlay_canvas = layer_create(GRect(0, 0, 200, 228));
  layer_set_update_proc(s_overlay_canvas, overlay_canvas_update);
  layer_add_child(window_get_root_layer(s_overlay_window), s_overlay_canvas);
  window_set_background_color(s_overlay_window, GColorBlack);
  s_native_menu_layer = menu_layer_create(GRect(0, 0, 200, 228));
  menu_layer_set_highlight_colors(s_native_menu_layer, accent_color(), GColorWhite);
  menu_layer_set_callbacks(s_native_menu_layer, NULL, (MenuLayerCallbacks) {
      .get_num_rows = native_menu_get_num_rows_callback,
      .get_cell_height = native_menu_get_cell_height_callback,
      .draw_row = native_menu_draw_row_callback,
      .select_click = native_menu_select_callback,
      .selection_changed = native_menu_selection_changed_callback,
      .draw_header = native_menu_draw_header_callback,
      .get_header_height = native_menu_get_header_height_callback,
  });
  layer_add_child(window_get_root_layer(s_overlay_window), menu_layer_get_layer(s_native_menu_layer));
  layer_set_hidden(menu_layer_get_layer(s_native_menu_layer), true);
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_click_config_provider(s_overlay_window, click_config_provider);
  window_stack_push(s_window, true);
  s_overlay_visible = false;
  sync_native_menu(false);

  s_dictation = dictation_session_create(TEXT_LENGTH, dictation_callback, NULL);
  app_message_register_inbox_received(inbox_received);
  app_message_open(1024, 256);
  send_hello();
  s_handshake_timer = app_timer_register(2000, handshake_retry, NULL);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
#ifdef PBL_PLATFORM_EMERY
  if (s_touch_subscribed) touch_service_unsubscribe();
#endif
  stop_audio();
  if (s_volume_timer) app_timer_cancel(s_volume_timer);
  if (s_animation_timer) app_timer_cancel(s_animation_timer);
  if (s_action_bar_timer) app_timer_cancel(s_action_bar_timer);
  if (s_handshake_timer) app_timer_cancel(s_handshake_timer);
  if (s_scroll_timer) app_timer_cancel(s_scroll_timer);
  dictation_session_destroy(s_dictation);
  if (window_stack_contains_window(s_overlay_window)) {
    window_stack_remove(s_overlay_window, false);
  }
  menu_layer_destroy(s_native_menu_layer);
  layer_destroy(s_overlay_canvas);
  layer_destroy(s_root_canvas);
  gbitmap_destroy(s_home_background);
  gbitmap_destroy(s_mascot_modern);
  gbitmap_destroy(s_mascot_dreamhouse);
  gbitmap_destroy(s_mascot_ticket);
  if (s_mascot_home) gbitmap_destroy(s_mascot_home);
  s_canvas = NULL;
  window_destroy(s_overlay_window);
  window_destroy(s_window);
  free(s_results);
  s_results = NULL;
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}

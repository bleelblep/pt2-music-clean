#include <pebble.h>

// Verbose protocol/navigation tracing, compiled out by default. The format strings plus
// their call sites cost ~1.5KB of virtual_size, and virtual_size is a uint16_t in the
// process header - the ceiling this app actually runs into. Warnings and errors are
// never gated; only the play-by-play is. Build with -DDW_TRACE_ENABLED=1 to get it back.
#ifndef DW_TRACE_ENABLED
#define DW_TRACE_ENABLED 0
#endif

#if DW_TRACE_ENABLED
#define DW_TRACE(...) APP_LOG(APP_LOG_LEVEL_INFO, __VA_ARGS__)
#else
#define DW_TRACE(...) do { } while (0)
#endif

#define MAX_RESULTS 10
// Library screens can hold many more entries than a search page. The shared
// s_results array is sized to the larger of the two so both features fit.
// 100 rather than 60: the Symfonium History grid reaches that high, and this array
// is the ceiling every unpaged library list is requested against.
#define MAX_LIBRARY 100
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
#define BACK_STOPS_KEY 29
#define SOPHIE_MODE_KEY 30
#define MUSIC_SOURCE_KEY 31
#define SOURCE_EPOCH_KEY 32
#define SYMFONIUM_AUTO_SHUFFLE_KEY 33
// Progress became three-valued in 0.7.0. A new key rather than reusing SHOW_PROGRESS_KEY:
// that one holds a bool, and persist_read_int() over a persist_write_bool() value is not
// something to rely on. The old key is still read once, to carry the setting over.
#define PROGRESS_MODE_KEY 34
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
// Which backend on the phone serves this app: MusicSourceYouTube or MusicSourceSymfonium.
// One watch app, two companion backends - the choice lives on the wire rather than in two
// separate watch apps.
#define MSG_CONFIG_MUSIC_SOURCE 46
// Monotonic epoch for the music source, working exactly like MSG_ROUTE_EPOCH. Needed
// because this watch re-sends its whole settings blob on every state snapshot: without an
// epoch, a source picked on the phone would be overwritten by the next sync seconds later.
#define MSG_SOURCE_EPOCH 47
// Paged search. The request names the first global index it wants; the completion carries
// how many matches exist in total. Only the Symfonium backend understands these.
#define MSG_SEARCH_OFFSET 48
#define MSG_SEARCH_TOTAL 49
// Paged library, exactly the same shape as the two above: the request names the first
// global index it wants and LIBRARY_LIMIT becomes the page size, and the completion
// carries the list's real length. Sent only for the library types that page (see
// library_is_paged), so a companion that ignores them behaves as it always did.
#define MSG_LIBRARY_OFFSET 50
#define MSG_LIBRARY_TOTAL 51
// Ask the Symfonium backend to turn shuffle on whenever it starts a playlist. Only
// that backend reads it - the YouTube one has its own radio/shuffle handling.
#define MSG_CONFIG_SYMFONIUM_AUTO_SHUFFLE 52
// Recently Played count cycles through these values (5 > 10 > 15 > 20).
// How long a flick keeps the progress rail on screen.
#define PROGRESS_PEEK_MS 5000
#define HISTORY_LIMIT_MIN 5
#define HISTORY_LIMIT_MAX 20
#define HISTORY_LIMIT_STEP 5
// Under Symfonium the History setting runs a coarser grid - 20/40/60/80/100 - because
// Recently Played there is backed by a smart playlist that can hold far more than the
// YouTube backend's own history ever does.
#define HISTORY_LIMIT_SYMFONIUM_MIN 20
#define HISTORY_LIMIT_SYMFONIUM_MAX 100
#define HISTORY_LIMIT_SYMFONIUM_STEP 20
// Recent Searches display count (default 10). Increasable like Recently Played;
// the UI control is intentionally not wired up yet.
#define RECENT_SEARCH_LIMIT_MIN 5
#define RECENT_SEARCH_LIMIT_MAX 20
#define RECENT_SEARCH_LIMIT_STEP 5
#define RECENT_SEARCH_LIMIT_DEFAULT 10
#define ADPCM_HEADER_SIZE 4
#define ADPCM_BLOCK_SIZE 512
#define ADPCM_SAMPLES_PER_BLOCK 1017
#define ADPCM_PCM_BYTES (ADPCM_SAMPLES_PER_BLOCK * (uint32_t) sizeof(int16_t))
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
  ScreenWhatsNew,
  // Both reached from About, and both readouts rather than lists: spec rows drawn on
  // the same bespoke_row1 rhythm, scrolled as a document like About itself.
  ScreenWatch,
  ScreenBridge,
  ScreenAcks,
} AppScreen;

// Keep in step with package.json - both About screens print it.
#define APP_VERSION "0.7.0"

typedef enum {
  ThemeTeal = 0,
  ThemePurple = 1,
  ThemeSunset = 2,
  ThemeDefault = 3,
  ThemeMono = 4,   // Black & white theme.
  ThemeArcade = 5,  // Inverted: cyan on hot pink. Bespoke UI only.
  // The neutral pair, and the app's starting point. Appended rather than inserted so a
  // persisted theme from an older build still names the same theme it did.
  ThemeDefaultLight = 6,
  ThemeDefaultDark = 7,
} AppTheme;

typedef enum {
  InputVoice,
  InputKeyboard,
  InputAsk,
} InputMode;

// "Sophie mode": swaps every piece of type in the app for LynoJean. It rides with the
// Mono theme and is only offered there - the face is a display cut, and it holds
// together on black and white in a way it does not against the colored grounds.
//
// The four sizes below are every size the app draws at; bold and regular both land on
// the same face, since the file has one weight. They are loaded on demand rather than
// at startup, because four rasterized faces is real heap and nobody who leaves this
// off should pay for it.
typedef enum {
  SophieFont14,
  SophieFont18,
  SophieFont24,
  SophieFont28,
  SophieFontCount,
} SophieFontSize;

static bool s_sophie_mode;
static GFont s_sophie_fonts[SophieFontCount];

// LECO 1976 - the face Pebble's own firmware carries for its numeric clock fonts, here
// used for every header in the bespoke language: the eyebrow, the Advanced group labels
// and the document mastheads. Two sizes is the whole set, because those are the only two
// the headers draw at. Unlike Sophie's faces these are loaded at startup and kept: the
// bespoke UI is the default, so a header is on screen almost all the time and paying the
// load on every screen change would be worse than holding ~10KB.
typedef enum {
  HeaderFont14,
  HeaderFont26,
  HeaderFontCount,
} HeaderFontSize;

static GFont s_header_fonts[HeaderFontCount];

typedef enum {
  SearchModeSong,
  SearchModeArtist,
  SearchModeSongRadio,
  // Album search (Symfonium). Value 3 matches the companion's Protocol.searchModeAlbum;
  // these enums are the wire, so existing values never move.
  SearchModeAlbum,
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
  EventSourceChanged = 119,
};

// Which companion backend serves this app. Sent in every settings sync, and pushed the
// other way by EventSourceChanged when it is changed on the phone.
enum {
  MusicSourceYouTube = 0,
  MusicSourceSymfonium = 1,
};

enum {
  LibraryRecent = 0,
  LibraryCached = 1,
  LibraryFavorites = 2,
  LibraryContinue = 3,
  LibraryRecentSearches = 4,
  // Phone-owned playlists; the watch lists and plays them but never edits them.
  LibraryPlaylists = 5,
  // Symfonium only: backed by the user's "most played" smart playlist on the phone.
  LibraryMostPlayed = 6,
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
  // Ink for text and glyphs sitting on top of the accent - selected rows, the dock
  // and More discs, the play button. White is not automatically right: these accents
  // are mid-tone, and white on Tiffany Blue or Sunset Orange is barely 3:1 where
  // black clears 6:1. Chosen per theme rather than derived, so Arcade can invert to
  // its own ground color instead of introducing a third one.
  GColor on_accent;
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
static AppTheme s_theme = ThemeDefaultDark;
static bool s_alt_home;
// When enabled, the Library menu also lists the extra sections: Favorites,
// Continue, and Recent Searches.
static bool s_extra_library;
// When disabled, the Home banner box (rotating quotes) is hidden entirely,
// leaving just the background art. Useful for a cleaner/quieter Home screen.
static bool s_show_home_quotes = true;
static uint8_t s_history_limit = HISTORY_LIMIT_MAX;
// Search result count: cycles 5 > 10 > Deep, the last only offered on Symfonium (see
// search_is_deep). Deep is stored as 0 so the two fixed counts keep reading as themselves
// both in the settings row and on the wire.
//
// "Deep" rather than "Unlimited" because the number is real and knowable: Symfonium's
// search caps at 15 songs however broad the query, and the companion widens that by
// walking the albums the same search matched - about 40 songs in practice. Calling that
// unlimited would be a promise the source cannot keep.
#define SEARCH_LIMIT_DEEP 0
static uint8_t s_search_limit = 10;
static uint8_t s_recent_search_limit = RECENT_SEARCH_LIMIT_DEFAULT;  // Recent Searches display count.
// Heap-allocated in init() (see the call site). ~14.6 KB of rows: as a static array
// this counts against the 64 KB app virtual-size limit (a uint16 field in the app
// metadata), which the app was already within ~60 bytes of; heap does not.
static SearchResult *s_results;
static SearchResult s_now_playing;
static bool s_has_now_playing;
// Rows resident in s_results right now. For every list except a paged search this is
// also the list's whole length.
static int s_result_count;
// The selected row as a *global* index into the full list the phone holds - which is the
// same number as the row for every list that is not paged, because s_span_base is 0 there.
static int s_selected_result;

// ---- Sliding window over a list too long to hold ---------------------------------
// Only an unlimited search ever slides. Every other list leaves these at base 0 / not
// paged, where result_at() and result_row() reduce to plain indexing and nothing about
// Library, Queue or a capped search changes.
//
// s_span_base   global index of the row living in s_results[0]
// s_span_total  rows the phone says exist, or -1 while we have not been told
// s_span_paged  this list is a window; walking off its end fetches rather than wraps
// s_span_pending offset of the page we are waiting on, or -1 when nothing is in flight
static int s_span_base;
static int s_span_total = -1;
static bool s_span_paged;
static int s_span_pending = -1;
static AppTimer *s_span_timer;

// How many rows a page carries, and how close to the edge the selection gets before the
// next one is asked for. The window itself is MAX_ENTRIES; when it overflows, whole pages
// are dropped from the far end (see place_result).
#define SPAN_PAGE 12
#define SPAN_PREFETCH 3
// A page request that goes unanswered this long is abandoned, so moving the selection can
// ask again instead of the list sitting there permanently "loading". Same reasoning as
// the cover-art stall timeout above.
#define SPAN_STALL_MS 6000

// Rows the whole list has, as best we know: the phone's total once it has told us,
// otherwise everything up to the end of what we hold.
static int span_total(void) {
  return s_span_total >= 0 ? s_span_total : s_span_base + s_result_count;
}

// The resident row showing global index `g`, or NULL if it has been paged out. Callers
// that dereference a selection must handle NULL: with a sliding window the selection can
// legitimately name a row we no longer hold.
static SearchResult *result_at(int g) {
  int row = g - s_span_base;
  if (row < 0 || row >= s_result_count) return NULL;
  return &s_results[row];
}

// Drops the window back to "the whole list starts here", which is the state every
// non-paged screen runs in permanently.
static void span_reset(void) {
  s_span_base = 0;
  s_span_total = -1;
  s_span_paged = false;
  s_span_pending = -1;
  s_result_count = 0;
}

/**
 * Decides which resident row global index `g` belongs in, sliding the window if the page
 * being delivered runs past an end of it. Returns the row to write, or -1 to ignore.
 *
 * Placement is worked out per row rather than per page because results arrive as
 * individual AppMessages carrying only their own index - there is no page frame to key
 * off, and a page can arrive short.
 */
static int place_result(int g) {
  if (g < 0) return -1;
  if (!s_span_paged) {
    // Unpaged lists behave exactly as they always did: index straight in, bounded by the
    // array, growing the count to the highest row delivered.
    if (g >= MAX_ENTRIES) return -1;
    if (g + 1 > s_result_count) s_result_count = g + 1;
    return g;
  }
  if (s_result_count == 0) {  // first row of a fresh window
    s_span_base = g;
    s_result_count = 1;
    return 0;
  }
  int row = g - s_span_base;
  if (row >= 0 && row < s_result_count) return row;  // a retransmit; overwrite in place
  if (row == s_result_count) {                       // the next row along: append
    if (s_result_count < MAX_ENTRIES) return s_result_count++;
    // Window full, so a page is dropped off the top to let the far end keep growing. The
    // rows that go are the ones furthest from where the user is reading, and scrolling
    // back up fetches them again.
    memmove(&s_results[0], &s_results[SPAN_PAGE],
            (size_t) (MAX_ENTRIES - SPAN_PAGE) * sizeof(SearchResult));
    s_span_base += SPAN_PAGE;
    s_result_count = MAX_ENTRIES - SPAN_PAGE;
    return s_result_count++;
  }
  if (row < 0 && -row <= SPAN_PAGE) {
    // The page immediately *before* the window - the user scrolled back up. Shift what we
    // hold down and open `shift` slots at the front, rather than starting the window over
    // from here: restarting would page out the rows the selection is sitting on, leaving
    // the highlight nowhere while its own page was still arriving.
    int shift = -row;
    if (s_result_count + shift > MAX_ENTRIES) s_result_count = MAX_ENTRIES - shift;
    memmove(&s_results[shift], &s_results[0], (size_t) s_result_count * sizeof(SearchResult));
    // A short page would otherwise leave the untouched front slots showing whatever was
    // in them; blank rows are at least honest, and the next scroll refetches.
    memset(&s_results[0], 0, (size_t) shift * sizeof(SearchResult));
    s_span_base -= shift;
    s_result_count += shift;
    return 0;
  }
  // Not in the window and not adjacent to it: a page from somewhere else entirely. Start
  // over from here.
  s_span_base = g;
  s_result_count = 1;
  return 0;
}

static bool s_bridge_ready;
// Last applied audio-route epoch; bumped on every locally initiated route change.
// Persisted so a restart cannot roll the route back to a pre-change value.
static int32_t s_route_epoch;
static int s_music_source = MusicSourceYouTube;
static int32_t s_source_epoch;

// Symfonium plays through its own player on the phone, so this source has no watch-speaker
// route, no stream cache and no radio - the screens and rows that expose those are hidden
// rather than left to fail.
static bool source_is_symfonium(void) { return s_music_source == MusicSourceSymfonium; }
// True once a state snapshot has been applied. Snapshots never choose the screen (see
// EventStateSnapshot) - this only records that the companion's state has landed at
// least once, so nothing reads a still-empty playback state as authoritative.
static bool s_snapshot_applied;
// Set at launch when the app was opened by a quick-launch button (see init()). It is
// what lets the first state snapshot navigate to Now Playing, which nothing else is
// allowed to do - see EventStateSnapshot.
static bool s_launch_now_playing;
static bool s_stream_open;
static int32_t s_expected_sequence;
static uint8_t s_watch_volume = 50;
static uint8_t s_phone_volume = 50;
// Heap, not .bss: the decode buffer is only live while the speaker stream is, and
// virtual_size (.text + .data + .bss) is the uint16_t field the loader gates on.
static int16_t *s_pcm_buffer;
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

// The actions the More popup can hold, in display order. The popup's rows are a
// filtered view of this list (see np_more_item_shown), so code that takes a row
// converts through np_more_item_at() rather than indexing this directly.
typedef enum {
  NpMoreShuffle,
  NpMoreRepeat,
  NpMoreFavorite,
  NpMoreOutput,
  NpMoreNewSearch,
  NpMoreQueue,
  NP_MORE_COUNT,
} NpMoreItem;
static bool s_loop_enabled;
static uint8_t s_loop_mode;
static bool s_shuffle_enabled;
static bool s_current_favorite;   // Whether the currently loaded track is favorited.
static bool s_phone_audio;
// Progress rail: off, always on, or shown for a few seconds when Now Playing is
// flicked. Flick is for battery-saver users who still want the occasional answer to
// "how far through am I" - the rail costs its band on screen, not power, so hiding it
// permanently to get the taller card and never being able to check was the gap.
typedef enum { ProgressHide = 0, ProgressShow = 1, ProgressFlick = 2 } ProgressMode;
static uint8_t s_progress_mode = ProgressShow;

static const char *progress_mode_name(void) {
  return s_progress_mode == ProgressShow ? "Show"
       : s_progress_mode == ProgressFlick ? "Flick" : "Hide";
}
// A flick on Now Playing shows the progress rail for a few seconds while the setting
// itself stays off - see np_touch_handle().
static bool s_progress_peek;
static AppTimer *s_progress_peek_timer = NULL;
// Whether the tap service is currently subscribed - see sync_accel_subscription().
static bool s_accel_subscribed;

// Whether the rail is on screen right now, as opposed to whether it is switched on.
// Battery-saver turns the rail off to reclaim its band, but "how far through am I"
// is a question you want answered occasionally rather than never - so a flick on Now
// Playing peeks at it (np_touch_handle) and it hides itself again a few seconds later.
//
// Everything that *draws* the rail asks this; everything that reads or writes the
// preference - the settings rows, the persist, the phone sync - uses s_progress_mode,
// so a peek never leaks into the saved setting.
static bool progress_visible(void) {
  return s_progress_mode == ProgressShow ||
         (s_progress_mode == ProgressFlick && s_progress_peek);
}
// Whether backing out of Now Playing tears the stream down. On is how the app always
// behaved; off leaves the track running so Back is pure navigation and Home keeps its
// hero, which is now the default - Home's card is built around there being something
// playing to show. See back_click().
static bool s_back_stops = false;
// Symfonium only: start playlists shuffled. Held here rather than on the phone so it
// rides the same settings blob as everything else on this screen.
static bool s_symfonium_auto_shuffle = false;
static bool s_cache_enabled = true;
static uint16_t s_cache_size_mb = 250;
// Off by default: radio is endless and effectively unrepeatable, so caching it spends
// the budget on tracks you are least likely to hear again.
static bool s_cache_radio = false;
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
  // Flashed on the now-playing screen to confirm which control a button triggered.
  // Play and Pause are deliberately absent: the artwork's veil already answers that
  // press across the whole screen, and a card repeating it just covered the answer.
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
// See screen_uses_native_menu() and the bespoke_* helpers. On by default: it is the
// language the whole app is designed in now, and the stock look is the fallback.
static bool s_bespoke_ui = true;
// Home's own selection. Bespoke Home is a real menu (UP/DOWN move, SELECT opens)
// rather than three fixed button shortcuts, and it must not disturb s_menu_selection.
static int s_home_selection;
// Whether audio is actually playing, as opposed to a track merely being loaded.
// Playing-vs-paused otherwise lives only in s_screen, which is gone the moment you
// navigate away, so Home could not tell the two apart - see draw_home_card().
static bool s_playback_active;
// Holding SELECT on the menu's About row toggles s_advanced_unlocked, which reveals the
// rest of Advanced beyond Keyboard (see advanced_item_count()). The unlock state is
// persisted; the gesture leaves nothing else behind.
static bool s_advanced_unlocked;
// Reserved for a possible legacy Dreamhouse/Ticket mascot theme - built but disabled
// per product direction (Advanced hides non-essential settings behind the About
// unlock instead); see the commented-out branches in current_status_mascot().
static uint8_t s_legacy_theme;
static AppScreen s_placeholder_parent;
static char s_placeholder_title[TEXT_LENGTH];
static char s_placeholder_message[TEXT_LENGTH];
static char s_time_text[6];
static bool s_ignore_menu_repeat;
// Grid keyboard by default; false selects T9, the same keypad typed by tapping a key
// repeatedly rather than swiping off it. Both are touch keyboards, so both need the
// Time 2's panel - see the PBL_PLATFORM_EMERY guards around the touch handlers.
static bool s_keyboard_pt2 = true;
#ifdef PBL_PLATFORM_EMERY
static bool s_touch_subscribed;
static bool s_touch_active;
static int8_t s_touch_origin_key = -1;
static int8_t s_touch_active_key = -1;
// Raw touch-down coordinates for the current grid gesture, used to measure
// swipe direction rather than requiring the finger to land inside a cell.
static int16_t s_touch_start_x;
static int16_t s_touch_start_y;
// T9's hold-for-a-number gesture. The timer runs while a key is held still; once it has
// fired the press is spent, so lifting off does not also type the key's letter.
static AppTimer *s_t9_hold_timer;
static bool s_t9_hold_fired;
// Which on-screen badge is currently pressed, if any.
typedef enum {
  Pt2BadgeNone,
  Pt2BadgeHelp,
} Pt2Badge;
static Pt2Badge s_touch_badge = Pt2BadgeNone;
static bool s_np_touching;
// Set once a touch travels far enough to be a flick rather than a tap or a hold.
static bool s_np_touch_moved;
static int16_t s_np_touch_x;
static int16_t s_np_touch_y;
typedef enum {
  NpTouchTargetNone = -1,
  NpTouchTargetN = 0,
  NpTouchTargetS,
  NpTouchTargetNW,
  NpTouchTargetNE,
  NpTouchTargetSW,
  NpTouchTargetSE,
} NpTouchTarget;
static int8_t s_np_touch_target = NpTouchTargetNone;
#endif

static void select_click(ClickRecognizerRef recognizer, void *context);
static void click_config_provider(void *context);
// Defined next to doc_stat(), but set_screen() has to release it on the way out.
static void doc_notes_unload(void);
// Defined next to the peek it drives; set_screen() has to re-evaluate it on every move.
static void sync_accel_subscription(void);
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
static int settings_item_id(int row);
static int advanced_item_count(void);
static int advanced_item_id(int row);
static int library_item_count(void);
static bool library_item_shown(int id);
static int library_item_id(int row);
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
static void draw_mic_icon(GContext *ctx, GPoint c, GColor color);
static void draw_keyboard_icon(GContext *ctx, GPoint c, GColor color);

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

// The single composition every artwork surface is a window onto: the cover scaled to
// fill the whole display, aspect-correct, centre-cropped to the display's ratio.
#define ART_FRAME GRect(0, 0, 200, 228)

// Paints the part of the ART_FRAME composition that falls inside 'clip', by writing
// straight to the 8-bit framebuffer (one byte per pixel). This is far cheaper than a
// graphics_fill_rect per source pixel - at 144x144 that was ~20k calls per frame, slow
// enough to visibly repaint on every redraw.
//
// 'frame' is what the cover is composed into; 'clip' is the piece actually painted.
// Every surface passes the same frame and differs only in its clip, so the Home card,
// the Now Playing card and the full-bleed view show the same picture at the same scale
// in the same place - opening one from another grows the frame without moving a pixel
// of art. The art reads as expanding rather than re-flowing.
//
// This replaced a per-surface crop, where each rect was cropped to its own aspect
// ratio. Every surface was aspect-correct on its own terms and none of them agreed:
// each showed a differently-framed window centred on the cover, so the subject slid as
// you moved between them. The full-bleed view did not even crop - it stretched the
// cover to 200x228, so a square sleeve gained 14% of height the moment you opened it,
// and the long-press both revealed and distorted in the same gesture.
static void draw_cover_art_window(GContext *ctx, GRect frame, GRect clip) {
  if (!s_cover_art_background || !s_cover_art_ready || !s_cover_art_data) return;
  const int w = s_cover_art_w;
  const int h = s_cover_art_h;
  const int bytes_per_row = w / 8;
  const int fw = frame.size.w;
  const int fh = frame.size.h;
  if (w <= 0 || h <= 0 || fw <= 0 || fh <= 0) return;
  if (clip.size.w <= 0 || clip.size.h <= 0) return;

  // Centre-crop the source to the frame's aspect. This is computed from the frame and
  // never from the clip, which is the whole point: the composition is fixed, and the
  // clip only decides how much of it is on screen.
  int sx0 = 0, sy0 = 0, sw = w, sh = h;
  if (w * fh > h * fw) {
    // Source wider than the frame: trim the sides.
    sw = h * fw / fh;
    sx0 = (w - sw) / 2;
  } else if (w * fh < h * fw) {
    // Source taller than the frame: trim top and bottom.
    sh = w * fh / fw;
    sy0 = (h - sh) / 2;
  }
  if (sw <= 0 || sh <= 0) { sx0 = sy0 = 0; sw = w; sh = h; }

  // Precompute the source column for each clip column so the inner loop is a plain
  // lookup + byte write. Resolved through the frame, so a given source pixel lands on
  // the same screen x whichever surface is drawing.
  int sx_map[200];
  int cols = clip.size.w > 200 ? 200 : clip.size.w;
  for (int dx = 0; dx < cols; dx++) {
    int fx = clip.origin.x - frame.origin.x + dx;
    int sx = sx0 + fx * sw / fw;
    // Clamped rather than assumed: frame and clip are independent arguments now, and
    // these index the heap art buffer directly.
    sx_map[dx] = sx < 0 ? 0 : (sx > w - 1 ? w - 1 : sx);
  }

  const uint8_t black = GColorBlack.argb;
  const uint8_t white = GColorWhite.argb;

  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  for (int dy = 0; dy < clip.size.h; dy++) {
    int py = clip.origin.y + dy;
    if (py < 0 || py >= 228) continue;
    GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, py);
    int fy = clip.origin.y - frame.origin.y + dy;
    int sy = sy0 + fy * sh / fh;
    if (sy < 0) sy = 0;
    if (sy > h - 1) sy = h - 1;
    const uint8_t *src_row = s_cover_art_data + (s_cover_art_color ? sy * w : sy * bytes_per_row);
    for (int dx = 0; dx < cols; dx++) {
      int px = clip.origin.x + dx;
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

// 50% black checkerboard over a rect: the veil that marks the artwork as paused on
// Now Playing. Written straight into the framebuffer for the same reason
// draw_cover_art_window() is - it is a per-pixel pattern, and there is no alpha to blend
// with. Cheap enough not to matter: it covers the artwork card only, and it never
// runs during playback, which is the redraw that happens once a second.
static void draw_dither_scrim(GContext *ctx, GRect r) {
  const uint8_t black = GColorBlack.argb;
  const int y_end = r.origin.y + r.size.h;
  const int x_end = r.origin.x + r.size.w;
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  for (int y = r.origin.y; y < y_end; y++) {
    if (y < 0 || y >= 228) continue;
    GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, y);
    // Offsetting alternate rows by one is what makes it a checkerboard rather than
    // vertical stripes, which at this pitch would moire against the artwork.
    for (int x = r.origin.x + (y & 1); x < x_end; x += 2) {
      if (x < row.min_x || x > row.max_x) continue;
      row.data[x] = black;
    }
  }
  graphics_release_frame_buffer(ctx, fb);
}

// Stands in for the cover while there isn't one, on both surfaces that show artwork.
// A gray card rather than bare ground: leaving the space unpainted made a missing
// cover read as a broken screen instead of a pending one. While a transfer is actually
// running it also shows how far in it is, which is information neither screen was
// carrying anywhere. 'corners' is what the caller wants rounded - all four on Now
// Playing, where the artwork is a card in its own right; the top two on Home, where
// the band below it closes off the bottom.
static void draw_art_placeholder(GContext *ctx, GRect art, bool loading,
                                 GCornerMask corners) {
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, art, 6, corners);
  const GPoint c = GPoint(art.origin.x + art.size.w / 2, art.origin.y + art.size.h / 2);
  draw_note_icon(ctx, GPoint(c.x, c.y - (loading ? 12 : 0)), GColorLightGray);
  if (!loading) return;
  const int w = art.size.w - 80;
  int filled = 0;
  if (s_cover_art_expected_bytes > 0) {
    filled = w * s_cover_art_received_bytes / s_cover_art_expected_bytes;
    if (filled > w) filled = w;
  }
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(c.x - w / 2, c.y + 20, w, 5), 2, GCornersAll);
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, GRect(c.x - w / 2, c.y + 20, filled, 5), 2, GCornersAll);
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
// Heap rather than .bss, for the same reason as s_results: virtual_size is the
// uint16_t ceiling this app runs into, and the heap it moves to is not counted there.
// Indexing is unchanged; only nav_push guards against a failed allocation.
static NavEntry *s_nav_stack;
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
static uint8_t s_query_length;

// The T9 keyboard: nine keys, a few characters each, cycled by tapping the same key
// again. Each string is both the key's label and its tap order, so a key draws exactly
// what it types.
//
// The letters are a phone's, not the grid's. Every keypad ever made put abc on 2 and
// wxyz on 9, which takes four letters on 7 and 9 and leaves 26 of them spread over eight
// keys - and that is the point of doing it this way, because it leaves the 1 key free for
// the space and the zero. The grid keyboard keeps its own even 3-per-key layout: it has
// to, because there a key's three characters are three swipe directions.
//
// The digits are not a mode. A key's number is its position - cell 0 is the 1 key, cell 8
// is the 9 key - so holding a key types it, exactly as a phone did, and no table is
// needed to say so ('1' + cell). That frees the third mode to be punctuation only.
static const char T9_LETTERS[9][5] = {
  " 0", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz",
};
static const char T9_SYMBOLS[9][4] = {
  ".,?", "!'\"", "@#$", "%&*", "-+=", "()/", ":;_", "~[]", "<>|",
};
// The longest key. Keys are laid out on this pitch whatever they hold, so the little row
// sits on one rhythm across the keypad instead of stretching to fill each key.
#define T9_MAX_CHARS 4
// How long a key must be held for that number. Below the 600ms the app's own long
// clicks use: nothing else competes for the gesture here, and a keyboard wants to feel
// quicker than a menu.
#define T9_HOLD_MS 450
// How long a key stays "open" after a tap. Tapping it again inside this window cycles
// the character already typed; anything else commits it.
#define T9_COMMIT_MS 800
static int8_t s_t9_pending = -1;
static uint8_t s_t9_index;
static AppTimer *s_t9_timer;

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

// Stored as one NUL-separated blob rather than an array of pointers. Under -fPIE every
// `const char *` in a table costs 4 bytes of .data, and .data counts toward virtual_size
// - the uint16_t ceiling this app is up against. Walking the blob costs a few bytes of
// code once and none per string. Keep the counts in step with the literals below.
#define HOME_QUOTES_COUNT 15
static const char HOME_QUOTES[] =
  "Unbridled stoke\0"
  "Mane event: catch a wave\0"
  "Hold your horses, let's surf\0"
  "Life's a beach, it's fantastic\0"
  "Come on unicorn, let's go surfing\0"
  "Hoofin' it, it's electric\0"
  "Brush my mane, comb the tide\0"
  "Sparkle horn, catch the swell\0"
  "Neigh worries, just vibes\0"
  "Pony up for paradise\0"
  "One horn, endless swell\0"
  "Trot to the tunes\0"
  "Magical waves, mythical waves\0"
  "Surf's up, unicorn's out\0"
  "Dream in pink, ride in teal";
// Alternate quote set shown when the hidden "Alt" home style is enabled.
#define HOME_QUOTES_ALT_COUNT 9
static const char HOME_QUOTES_ALT[] =
  "How Beak-zarre\0"
  "Don't Preen It's Over\0"
  "Don't Forget Your Roost\0"
  "Why does surf do this to me?\0"
  "And we'll never be flying\0"
  "Don't come and go swim my way\0"
  "How many birds surf like this\0"
  "Friday surf is the scene tune your frequencies\0"
  "Kiss me till the tide goes out";
static int s_home_quote;

static int active_home_quote_count(void) {
  return s_alt_home ? HOME_QUOTES_ALT_COUNT : HOME_QUOTES_COUNT;
}

// The index-th string in the active blob. Index is taken modulo the count by callers.
static const char *active_home_quote(int index) {
  const char *p = s_alt_home ? HOME_QUOTES_ALT : HOME_QUOTES;
  while (index-- > 0) p += strlen(p) + 1;
  return p;
}

static void shuffle_home_quote(void) {
  int count = active_home_quote_count();
  if (count <= 1) { s_home_quote = 0; return; }
  int next = rand() % (count - 1);
  if (next >= s_home_quote) next++;  // Skip the current index to avoid an immediate repeat.
  s_home_quote = next;
}

// The stock UI's palette: a white ground with a colored accent, unchanged. The system
// MenuLayer it hands its lists to paints its own white background, so this side cannot
// move off white without stranding text on it.
static ThemeColors classic_colors(void) {
  if (s_theme == ThemePurple) {
    return (ThemeColors) {
      .background = GColorWhite,
      .foreground = GColorBlack,
      .accent = GColorPurpureus,
      .on_accent = GColorWhite,
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
      .on_accent = GColorBlack,
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
      .on_accent = GColorBlack,
      .secondary = GColorDarkGray,
      .surface = GColorLightGray,
      .action_bar_bg = GColorWhite,
      .action_bar_icon = GColorBlack,
      .action_bar_press_bg = GColorFashionMagenta,
      .action_bar_press_icon = GColorWhite,
    };
  }
  // Both neutral themes look the same here, and have to: the stock UI hands its lists to
  // a system MenuLayer that paints its own white background, so Default Dark cannot bring
  // its black ground across without stranding white text on white. It degrades to its
  // light sibling instead - the same accommodation Mono already makes - and the pair is
  // only truly distinct under the bespoke UI.
  if (s_theme == ThemeDefaultLight || s_theme == ThemeDefaultDark) {
    return (ThemeColors) {
      .background = GColorWhite,
      .foreground = GColorBlack,
      .accent = GColorLiberty,
      .on_accent = GColorWhite,
      .secondary = GColorDarkGray,
      .surface = GColorLightGray,
      .action_bar_bg = GColorWhite,
      .action_bar_icon = GColorBlack,
      .action_bar_press_bg = GColorLiberty,
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
      .on_accent = GColorWhite,
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
    .on_accent = GColorBlack,
    .secondary = GColorDarkGray,
    .surface = GColorLightGray,
    .action_bar_bg = GColorWhite,
    .action_bar_icon = GColorBlack,
    .action_bar_press_bg = GColorTiffanyBlue,
    .action_bar_press_icon = GColorWhite,
  };
}

// The bespoke UI's palette: a deep tone of the theme's own hue as the ground, bright
// ink on top, and the theme's familiar accent for fills - the shape Arcade established,
// applied to every theme.
//
// Contrast picked every value here. On the 64-color panel a mid-tone accent used as
// *text* on white is under 3:1 (Tiffany Blue 2.9, Sunset Orange 3.2), which is what
// made the old secondary text so hard to read. Inverting to a deep ground with white
// ink puts every text pairing at 4.6:1 or better, and every accent fill stays visibly
// lighter than the ground it sits on:
//
//   theme     ground     ink-on-ground   accent    on-accent    fill-vs-ground
//   Default   #AA0055    white  7.4:1    #FF55FF   black 8.0      2.8:1
//   Teal      #005555    white  8.7:1    #00AAAA   black 7.4      3.0:1
//   Purple    #550055    white 13.9:1    #AA55AA   white 4.6      3.4:1
//   Sunset    #AA0000    white  7.8:1    #FF5555   black 6.7      2.5:1
//   Mono      black      white 21.0:1    white     black 21.0    21.0:1
//   Arcade     #550055    cyan  11.5:1    #00FFFF   #550055 11.5   11.5:1
//
// `secondary` (dim text) and `surface` (scrollbar tracks, an unlit shuffle icon) are
// light gray on every dark ground: clearly quieter than white ink but still 4.6:1,
// where a dark gray would have vanished into the ground. Arcade is the exception - it
// has no white to be quieter than, so it dims cyan to #00AAAA (5.1:1) instead.
static ThemeColors bespoke_colors(void) {
  if (s_theme == ThemePurple) {
    return (ThemeColors) {
      .background = GColorImperialPurple,
      .foreground = GColorWhite,
      .accent = GColorPurpureus,
      .on_accent = GColorWhite,
      .secondary = GColorLightGray,
      .surface = GColorLightGray,
      .action_bar_bg = GColorImperialPurple,
      .action_bar_icon = GColorWhite,
      .action_bar_press_bg = GColorPurpureus,
      .action_bar_press_icon = GColorWhite,
    };
  }
  if (s_theme == ThemeSunset) {
    return (ThemeColors) {
      .background = GColorDarkCandyAppleRed,
      .foreground = GColorWhite,
      .accent = GColorSunsetOrange,
      .on_accent = GColorBlack,
      .secondary = GColorLightGray,
      .surface = GColorLightGray,
      .action_bar_bg = GColorDarkCandyAppleRed,
      .action_bar_icon = GColorWhite,
      .action_bar_press_bg = GColorSunsetOrange,
      .action_bar_press_icon = GColorBlack,
    };
  }
  if (s_theme == ThemeDefault) {
    // The accent steps up from Fashion Magenta to Shocking Pink: against the berry
    // ground the darker pink was only 2.0:1, so a selected row barely read as selected.
    return (ThemeColors) {
      .background = GColorJazzberryJam,
      .foreground = GColorWhite,
      .accent = GColorShockingPink,
      .on_accent = GColorBlack,
      .secondary = GColorLightGray,
      .surface = GColorLightGray,
      .action_bar_bg = GColorJazzberryJam,
      .action_bar_icon = GColorWhite,
      .action_bar_press_bg = GColorShockingPink,
      .action_bar_press_icon = GColorBlack,
    };
  }
  // The neutral pair. Every other theme paints a ground in its own hue; these two do
  // not paint one at all, and let the artwork and the accent be the only colour on the
  // screen. They are siblings on purpose - same accent, same on-accent, inverted ground
  // - so switching between them changes the room's lighting and nothing else.
  //
  // Emery renders 64 colours (two bits a channel), so the only true neutrals available
  // are black, #555555, #AAAAAA and white. The mid greys are what the pair deliberately
  // avoids: on #555555 the dim text has nowhere to sit, because #AAAAAA lands at 3.2:1
  // against it and the next step up is white, which is the bright text. Black and white
  // grounds leave room for both.
  //
  //   theme           ground   ink        dim            accent fill   ink on accent
  //   Default Dark    black    white 21   #AAAAAA 9.0:1   3.3:1         white 6.4:1
  //   Default Light   white    black 21   #555555 7.5:1   6.4:1         white 6.4:1
  //
  // One accent for both, and a deliberately quiet one: GColorLiberty is a muted indigo
  // that no other theme uses, so a selection reads as "this row" rather than announcing
  // itself the way Shocking Pink does. Its fill sits at 3.3:1 on the dark ground, in the
  // same band every colour theme here uses (Default 2.8:1, Teal 3.0:1, Sunset 2.5:1),
  // and the white text on it clears 6.4:1 on both grounds.
  if (s_theme == ThemeDefaultDark) {
    return (ThemeColors) {
      .background = GColorBlack,
      .foreground = GColorWhite,
      .accent = GColorLiberty,
      .on_accent = GColorWhite,
      .secondary = GColorLightGray,
      .surface = GColorDarkGray,
      .action_bar_bg = GColorBlack,
      .action_bar_icon = GColorWhite,
      .action_bar_press_bg = GColorLiberty,
      .action_bar_press_icon = GColorWhite,
    };
  }
  if (s_theme == ThemeDefaultLight) {
    // The one light ground in the bespoke set, so it is also the one place the dim and
    // surface tones invert: light gray would be invisible here, dark gray is the quiet
    // one, and the scrollbar track goes lighter than its thumb rather than darker.
    return (ThemeColors) {
      .background = GColorWhite,
      .foreground = GColorBlack,
      .accent = GColorLiberty,
      .on_accent = GColorWhite,
      .secondary = GColorDarkGray,
      .surface = GColorLightGray,
      .action_bar_bg = GColorWhite,
      .action_bar_icon = GColorBlack,
      .action_bar_press_bg = GColorLiberty,
      .action_bar_press_icon = GColorWhite,
    };
  }
  if (s_theme == ThemeMono) {
    // Mono's "color" is the absence of one: the stock look, inverted.
    return (ThemeColors) {
      .background = GColorBlack,
      .foreground = GColorWhite,
      .accent = GColorWhite,
      .on_accent = GColorBlack,
      .secondary = GColorLightGray,
      .surface = GColorDarkGray,
      .action_bar_bg = GColorBlack,
      .action_bar_icon = GColorWhite,
      .action_bar_press_bg = GColorWhite,
      .action_bar_press_icon = GColorBlack,
    };
  }
  if (s_theme == ThemeArcade) {
    // Cyan on deep magenta. Both hues are the reference's; only the ground's lightness
    // has moved, and it has moved twice. #FF00AA sat at 2.9:1, which no rearrangement
    // of two colors fixes - contrast is symmetric, so lightness is the only lever that
    // does not change the hue. #AA0055 brought that to 5.9:1, and #550055 brings it to
    // 11.5:1, which is where the rest of the themes already were.
    //
    // Going darker also bought the theme a third color for the first time. It had none
    // - dim text and unlit icons had to fall back to the foreground, so "quiet" and
    // "loud" looked identical - and #00AAAA now clears 5:1 on this ground while
    // reading as clearly dimmer than full cyan.
    return (ThemeColors) {
      .background = GColorImperialPurple,
      .foreground = GColorCyan,
      .accent = GColorCyan,
      .on_accent = GColorImperialPurple,
      .secondary = GColorTiffanyBlue,
      .surface = GColorTiffanyBlue,
      .action_bar_bg = GColorImperialPurple,
      .action_bar_icon = GColorCyan,
      .action_bar_press_bg = GColorCyan,
      .action_bar_press_icon = GColorImperialPurple,
    };
  }
  return (ThemeColors) {
    .background = GColorMidnightGreen,
    .foreground = GColorWhite,
    .accent = GColorTiffanyBlue,
    .on_accent = GColorBlack,
    .secondary = GColorLightGray,
    .surface = GColorLightGray,
    .action_bar_bg = GColorMidnightGreen,
    .action_bar_icon = GColorWhite,
    .action_bar_press_bg = GColorTiffanyBlue,
    .action_bar_press_icon = GColorBlack,
  };
}

static ThemeColors colors(void) {
  return s_bespoke_ui ? bespoke_colors() : classic_colors();
}

static GColor accent_color(void) {
  return colors().accent;
}

static GColor on_accent_color(void) {
  return colors().on_accent;
}

// The bespoke UI's ground and ink. Every theme but Arcade is white-on-black, so these
// read as GColorWhite/GColorBlack did before; Arcade is the reason they are lookups.
static GColor ui_bg(void) {
  return colors().background;
}

static GColor ui_fg(void) {
  return colors().foreground;
}

// Ink one step down from ui_fg() - hints, subtitles, the value under a name. Gray for
// the white-ground themes; a dimmed cyan for Arcade, which has no white to step down
// from. It used to return the foreground unchanged there, so nothing on that theme
// could look quieter than anything else.
static GColor ui_dim(void) {
  return colors().secondary;
}

static const char *theme_name(void) {
  if (s_theme == ThemePurple) return "Electric Purple";
  if (s_theme == ThemeSunset) return "Sunset";
  if (s_theme == ThemeTeal) return "Dreamwave Teal";
  if (s_theme == ThemeMono) return "Mono";
  if (s_theme == ThemeArcade) return "Arcade";
  if (s_theme == ThemeDefaultLight) return "Default Light";
  if (s_theme == ThemeDefaultDark) return "Default Dark";
  // Was "Default" until the neutral pair took that name. The colours are untouched; this
  // is the ground it has always used (GColorJazzberryJam) finally saying so.
  return "Jazzberry";
}

// Arcade repaints the ground itself, which only the bespoke screens draw - the stock
// UI hands its lists to a system MenuLayer that would keep its white background and
// leave cyan text stranded on it. So the theme is offered to the bespoke UI only.
static bool theme_is_available(AppTheme theme) {
  return theme != ThemeArcade || s_bespoke_ui;
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
  // The doc screens' explainer prose is a resource loaded on demand (see doc_note).
  // Hand the heap back as soon as the only two screens that show it are gone - it is
  // ~1.7KB, and Now Playing wants every contiguous byte it can get for cover art.
  if (changed && screen != ScreenWatch && screen != ScreenBridge) doc_notes_unload();
  // A peeked progress rail belongs to the screen it was flicked on; leaving ends it
  // rather than letting the timer fire later and repaint something else.
  if (changed && screen != ScreenPlaying && screen != ScreenPaused && s_progress_peek) {
    if (s_progress_peek_timer) {
      app_timer_cancel(s_progress_peek_timer);
      s_progress_peek_timer = NULL;
    }
    s_progress_peek = false;
  }
  s_screen = screen;
  sync_touch_service();
  sync_accel_subscription();
  sync_overlay_window(changed);
  sync_native_menu(changed);
  // The button config is screen-dependent now - see screen_uses_vertical_long_press() -
  // so it has to be rebuilt on every screen change, not just when a window is pushed.
  // Re-setting the provider is what makes the firmware re-run it.
  if (changed) {
    window_set_click_config_provider(s_overlay_visible ? s_overlay_window : s_window,
                                     click_config_provider);
  }
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
  DW_TRACE("[MenuSync] nav_push from screen=%d selected_result=%d to screen=%d",
          (int) s_screen, s_selected_result, (int) screen);
  if (s_nav_stack && !screen_is_transient(s_screen)) {
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
  // A paged list may have slid while the user was away, leaving the row they left on no
  // longer resident - which would restore the selection onto nothing and draw no
  // highlight at all. Pull it back to the nearest row we actually hold.
  if (s_span_paged && s_result_count > 0) {
    if (s_selected_result < s_span_base) s_selected_result = s_span_base;
    if (s_selected_result > s_span_base + s_result_count - 1) {
      s_selected_result = s_span_base + s_result_count - 1;
    }
  }
  DW_TRACE("[MenuSync] nav_back -> screen=%d selected_result=%d result_count=%d",
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
  // Clamped into the *resident* rows rather than into the whole list: on a paged search
  // the selection may name a row that has since been paged out, and the nearest row we
  // actually hold is a better answer than reading off the end of the window.
  int row = s_selected_result - s_span_base;
  if (row < 0) row = 0;
  if (row >= s_result_count) row = s_result_count - 1;
  return &s_results[row];
}

// Frees the custom faces. Safe to call when they were never loaded.
static void sophie_fonts_unload(void) {
  for (int i = 0; i < SophieFontCount; i++) {
    if (s_sophie_fonts[i]) {
      fonts_unload_custom_font(s_sophie_fonts[i]);
      s_sophie_fonts[i] = NULL;
    }
  }
}

static void sophie_fonts_load(void) {
  static const uint32_t ids[SophieFontCount] = {
    RESOURCE_ID_FONT_LYNO_14, RESOURCE_ID_FONT_LYNO_18,
    RESOURCE_ID_FONT_LYNO_24, RESOURCE_ID_FONT_LYNO_28,
  };
  for (int i = 0; i < SophieFontCount; i++) {
    if (!s_sophie_fonts[i]) s_sophie_fonts[i] = fonts_load_custom_font(resource_get_handle(ids[i]));
  }
}

// Every piece of type in the app goes through here rather than calling
// ui_font() directly, so Sophie mode is one lookup instead of an edit at
// each of the ~90 draw sites. Off, it is exactly the call it replaced.
//
// The system keys are literals, so comparing pointers would work on this compiler and
// break on the next one; the string compare costs nothing at the handful of draws per
// frame this screen does. Any face that failed to load falls back to the system font
// rather than drawing nothing.
static GFont ui_font(const char *key) {
  // Gated on the theme as well as the switch. The row that toggles this is only shown
  // under Mono, so a Sophie mode that outlived a theme change would be a setting with
  // no way left to turn it off. Keeping the preference but ignoring it elsewhere means
  // going back to Mono restores it.
  if (s_sophie_mode && s_theme == ThemeMono) {
    SophieFontSize size;
    if (strcmp(key, FONT_KEY_GOTHIC_28_BOLD) == 0) size = SophieFont28;
    else if (strcmp(key, FONT_KEY_GOTHIC_24_BOLD) == 0) size = SophieFont24;
    else if (strcmp(key, FONT_KEY_GOTHIC_18) == 0 ||
             strcmp(key, FONT_KEY_GOTHIC_18_BOLD) == 0) size = SophieFont18;
    else size = SophieFont14;
    if (s_sophie_fonts[size]) return s_sophie_fonts[size];
  }
  return fonts_get_system_font(key);
}

static void header_fonts_load(void) {
  static const uint32_t ids[HeaderFontCount] = {
    RESOURCE_ID_FONT_LECO_14, RESOURCE_ID_FONT_LECO_26,
  };
  for (int i = 0; i < HeaderFontCount; i++) {
    if (!s_header_fonts[i]) s_header_fonts[i] = fonts_load_custom_font(resource_get_handle(ids[i]));
  }
}

static void header_fonts_unload(void) {
  for (int i = 0; i < HeaderFontCount; i++) {
    if (s_header_fonts[i]) {
      fonts_unload_custom_font(s_header_fonts[i]);
      s_header_fonts[i] = NULL;
    }
  }
}

// The face a bespoke header draws in, with the Gothic weight it replaced as the fallback
// - a font resource that failed to load leaves the header stock rather than blank.
//
// Sophie mode wins outright. It is a deliberate whole-app face override, and LynoJean
// rows under a LECO masthead is the one pairing neither face was picked for.
static GFont header_font(HeaderFontSize size) {
  const char *fallback = size == HeaderFont14 ? FONT_KEY_GOTHIC_14_BOLD
                                              : FONT_KEY_GOTHIC_28_BOLD;
  if (s_sophie_mode && s_theme == ThemeMono) return ui_font(fallback);
  return s_header_fonts[size] ? s_header_fonts[size] : ui_font(fallback);
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
// The lozenge. A 2px hairline rail pinned to the right edge of `track`, and a 10px fully
// rounded capsule that overhangs it - the value pill's silhouette stood on its end.
//
// The overhang is the whole point, not styling. The old bar drew thumb and track at the
// same width, which put accent_color() on GColorLightGray; those two land within about
// 2:1 of each other in all six themes, which is why the indicator was invisible rather
// than merely thin. Because the capsule is wider than the rail it sits on the page ground
// instead, and accent-on-ground is the pairing every theme was actually tuned for - the
// same one a selected row already uses.
//
// Colors are not parameters any more: every caller wanted the same pair, and taking them
// as arguments is how one screen ended up with a different (also failing) grey.
// The capsule was 10px and the rows ran to x=195, so the lozenge was drawn *inside* the
// selection pill - and an accent thumb on an accent pill is not a thin indicator, it is
// no indicator at all for exactly the row you are looking at. Two changes, together:
// the capsule is 8px, and the bespoke rows now stop at x=183 (see BESPOKE_ROW_W) so the
// 7px of ground between row edge and lozenge matches the inset the row keeps on its
// other three sides. The rail is unchanged - it was never the part that crowded.
#define SCROLL_RAIL_W 2
#define SCROLL_THUMB_W 8

static void draw_scrollbar(GContext *ctx, GRect track, int32_t offset,
                           int32_t viewport_size, int32_t content_size) {
  if (content_size <= viewport_size || viewport_size <= 0) return;
  // Callers describe the gutter they own; the lozenge hangs off its right edge.
  const int right = track.origin.x + track.size.w;

  graphics_context_set_fill_color(ctx, ui_dim());
  graphics_fill_rect(ctx, GRect(right - SCROLL_RAIL_W, track.origin.y,
                                SCROLL_RAIL_W, track.size.h),
                     SCROLL_RAIL_W / 2, GCornersAll);

  // 32-bit throughout: Cortex-M3 divides these in one instruction, where the 64-bit
  // form dragged in __udivmoddi4 + __aeabi_ldivmod (~900 bytes). track.size.h is a
  // screen dimension (<=228), so the products below stay far inside int32_t.
  //
  // The floor is 14 rather than the old 12: below about that the capsule stops reading as
  // a lozenge and starts reading as a dot, and a long list is exactly where you need it.
  const int min_thumb = 14;
  int thumb_h = (int) ((int32_t) track.size.h * viewport_size / content_size);
  if (thumb_h < min_thumb) thumb_h = min_thumb;
  if (thumb_h > track.size.h) thumb_h = track.size.h;

  int32_t max_offset = content_size - viewport_size;
  if (offset < 0) offset = 0;
  if (offset > max_offset) offset = max_offset;
  int travel = track.size.h - thumb_h;
  int thumb_y = max_offset > 0 ? (int) ((int32_t) travel * offset / max_offset) : 0;

  graphics_context_set_fill_color(ctx, accent_color());
  graphics_fill_rect(ctx, GRect(right - SCROLL_THUMB_W, track.origin.y + thumb_y,
                                SCROLL_THUMB_W, thumb_h),
                     SCROLL_THUMB_W / 2, GCornersAll);
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
  draw_text(ctx, label, ui_font(FONT_KEY_GOTHIC_18_BOLD), on_accent_color(),
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
// What's New draws its own fixed header (title + version) instead of an eyebrow.
#define WN_HEADER_H 76
#define BESPOKE_FOOTER_TOP 198
#define BESPOKE_VIEWPORT_H (BESPOKE_FOOTER_TOP - BESPOKE_LIST_TOP)
// Every bespoke list uses the Queue's row metrics - one rhythm across the whole app.
// Single-line rows keep the same height and pitch as the two-line song rows so the
// lists line up with each other rather than each screen inventing its own spacing.
#define BESPOKE_ROW_PITCH 43
#define BESPOKE_ROW_H 39
// Row box and text column. The row starts at x=5 and stops at 183, leaving the strip out
// to the scrollbar as ground; the text keeps a 7px inset inside that, so the column runs
// 12..176. Named because five draw sites have to agree on them - they did not, and the
// one that disagreed is how the lozenge ended up under the rows.
#define BESPOKE_ROW_X 5
#define BESPOKE_ROW_W 178
#define BESPOKE_TEXT_X 12
#define BESPOKE_TEXT_W 164
// The icon-dock line. Home's dock put its icons at cy=178; the pagers (Search type,
// Search with) and the Searching/Buffering animations centre on the same line, so
// every icon and spinner in the app sits at one height. The pitch is Home's: it
// tightens to 38 only when a fifth dock icon appears (see draw_home_list()).
#define BESPOKE_DOCK_Y 178
#define BESPOKE_DOCK_PITCH 40

static void bespoke_eyebrow(GContext *ctx, const char *label) {
  draw_text(ctx, label, header_font(HeaderFont14), ui_fg(),
            GRect(18, 13, 164, 20), GTextAlignmentLeft);
}

static void bespoke_ground(GContext *ctx, const char *label) {
  graphics_context_set_fill_color(ctx, ui_bg());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  bespoke_eyebrow(ctx, label);
}

// Re-stamps the top and bottom bands after the rows are drawn, so a row scrolled to
// the edge of the viewport can never bleed into the eyebrow or the hint.
//
// `footer_top` is where the list's viewport ends. A screen with a hint band stops at
// BESPOKE_FOOTER_TOP and the strip below it is the hint's; a screen with no hint (About)
// passes 228 and runs to the display edge instead. Without that, the bottom 30px were
// stamped blank on a screen that had nothing to put there, and - because the caller's
// scroll extent was measured against the full 228 - the last rows could never be
// scrolled into view at all.
static void bespoke_frame_to(GContext *ctx, const char *label, const char *hint,
                             int footer_top) {
  graphics_context_set_fill_color(ctx, ui_bg());
  graphics_fill_rect(ctx, GRect(0, 0, 200, BESPOKE_LIST_TOP), 0, GCornerNone);
  bespoke_eyebrow(ctx, label);
  if (footer_top < 228) {
    graphics_context_set_fill_color(ctx, ui_bg());
    graphics_fill_rect(ctx, GRect(0, footer_top, 200, 228 - footer_top), 0, GCornerNone);
  }
  if (hint) {
    draw_text(ctx, hint, ui_font(FONT_KEY_GOTHIC_14), ui_fg(),
              GRect(8, 204, 184, 18), GTextAlignmentCenter);
  }
}

static void bespoke_frame(GContext *ctx, const char *label, const char *hint) {
  bespoke_frame_to(ctx, label, hint, BESPOKE_FOOTER_TOP);
}

static void bespoke_scrollbar_to(GContext *ctx, int offset, int footer_top) {
  const int viewport = footer_top - BESPOKE_LIST_TOP;
  draw_scrollbar(ctx, GRect(194, BESPOKE_LIST_TOP, 4, viewport), offset,
                 viewport, viewport + s_scroll_max);
}

static void bespoke_scrollbar(GContext *ctx, int offset) {
  bespoke_scrollbar_to(ctx, offset, BESPOKE_FOOTER_TOP);
}

// Scrollbar for a song list. A paged list draws its thumb against the *whole* list even
// though only a window of it is laid out, so the bar answers "where am I in 900 matches?"
// rather than "where am I in the two dozen I happen to hold?" - which would otherwise
// show a full-height thumb sitting still while the user scrolls through hundreds of rows.
static void bespoke_scrollbar_span(GContext *ctx, int offset, int row_pitch) {
  if (!s_span_paged) {
    bespoke_scrollbar(ctx, offset);
    return;
  }
  const int viewport = BESPOKE_FOOTER_TOP - BESPOKE_LIST_TOP;
  int total = span_total() * row_pitch;
  if (total < viewport) total = viewport;
  draw_scrollbar(ctx, GRect(194, BESPOKE_LIST_TOP, 4, viewport),
                 s_span_base * row_pitch + offset, viewport, total);
}

// Two-line row: bold title over a gray subtitle. `title_color` lets a caller flag a
// row (song lists tint the track that is playing) without duplicating the row body.
static void bespoke_row2(GContext *ctx, int y, int h, const char *title, const char *sub,
                         bool selected, GColor title_color) {
  if (selected) {
    graphics_context_set_fill_color(ctx, accent_color());
    graphics_fill_rect(ctx, GRect(BESPOKE_ROW_X, y, BESPOKE_ROW_W, h), 4, GCornersAll);
  }
  draw_text(ctx, title, ui_font(FONT_KEY_GOTHIC_18_BOLD),
            selected ? on_accent_color() : title_color,
            GRect(BESPOKE_TEXT_X, y + 1, BESPOKE_TEXT_W, 22), GTextAlignmentLeft);
  draw_text(ctx, sub, ui_font(FONT_KEY_GOTHIC_14),
            selected ? on_accent_color() : ui_fg(),
            GRect(BESPOKE_TEXT_X, y + 20, BESPOKE_TEXT_W, 18), GTextAlignmentLeft);
}

// Single-line row: bold label left, optional value right-aligned in the accent. Same
// height and margins as bespoke_row2(); the label is centred in the row rather than
// sitting where a two-line row's title would.
static void bespoke_row1(GContext *ctx, int y, int h, const char *label, const char *value,
                         bool selected) {
  if (selected) {
    graphics_context_set_fill_color(ctx, accent_color());
    graphics_fill_rect(ctx, GRect(BESPOKE_ROW_X, y, BESPOKE_ROW_W, h), 4, GCornersAll);
  }
  int text_y = y + (h - 22) / 2;
  int label_w = BESPOKE_TEXT_W;
  if (value) {
    // The value used to be plain 14px in the accent, right-aligned in a fixed 76px
    // box. That was the least legible thing in the app - accent-on-white is under 3:1
    // for the teal and orange themes - and the box clipped anything as long as
    // "Keyboard" or a theme name. It is now a filled pill sized to its own text, so
    // it reads as the current choice rather than as faint decoration, and the label
    // yields whatever width the value actually needs.
    GFont value_font = ui_font(FONT_KEY_GOTHIC_14_BOLD);
    GFont label_font = ui_font(FONT_KEY_GOTHIC_18_BOLD);
    int value_w = graphics_text_layout_get_content_size(
        value, value_font, GRect(0, 0, 160, 20),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft).w;
    int label_text_w = graphics_text_layout_get_content_size(
        label, label_font, GRect(0, 0, BESPOKE_TEXT_W, 24),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft).w;
    int pill_w = value_w + 14;
    int room = BESPOKE_TEXT_W - label_text_w - 8;
    if (pill_w > room) pill_w = room;
    if (pill_w < 34) pill_w = 34;
    int pill_x = BESPOKE_TEXT_X + BESPOKE_TEXT_W - pill_w;
    // On an unselected row the pill carries the accent; on a selected row the row
    // itself is the accent, so the pill inverts to keep the value off its own ground.
    graphics_context_set_fill_color(ctx, selected ? ui_bg() : accent_color());
    graphics_fill_rect(ctx, GRect(pill_x, text_y + 1, pill_w, 20), 8, GCornersAll);
    draw_text(ctx, value, value_font, selected ? accent_color() : on_accent_color(),
              GRect(pill_x, text_y + 3, pill_w, 18), GTextAlignmentCenter);
    label_w = pill_x - BESPOKE_TEXT_X - 6;
  }
  draw_text(ctx, label, ui_font(FONT_KEY_GOTHIC_18_BOLD),
            selected ? on_accent_color() : ui_fg(),
            GRect(BESPOKE_TEXT_X, text_y, label_w, 22), GTextAlignmentLeft);
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
  draw_text(ctx, message, ui_font(FONT_KEY_GOTHIC_18_BOLD), ui_fg(),
            GRect(18, 96, 164, 26), GTextAlignmentCenter);
  if (detail) {
    draw_text(ctx, detail, ui_font(FONT_KEY_GOTHIC_14), ui_fg(),
              GRect(18, 120, 164, 36), GTextAlignmentCenter);
  }
  if (hint) {
    draw_text(ctx, hint, ui_font(FONT_KEY_GOTHIC_14), ui_fg(),
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
  // Laid out against the rows we hold, not the list's true length: a window of 24 out of
  // 900 matches has 24 rows of geometry, and pretending otherwise would put the viewport
  // hundreds of rows below anything drawable. The scrollbar is the piece that still
  // speaks for the whole list - see bespoke_scrollbar_span().
  const int sel_row = s_selected_result - s_span_base;
  int offset = scroll_list_layout(row_pitch, s_result_count, sel_row,
                                  BESPOKE_LIST_TOP, BESPOKE_VIEWPORT_H, true);
  graphics_context_set_fill_color(ctx, ui_bg());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  for (int i = 0; i < s_result_count; i++) {
    int y = BESPOKE_LIST_TOP + i * row_pitch - offset;
    if (y + row_h < BESPOKE_LIST_TOP || y > BESPOKE_FOOTER_TOP) continue;
    bool is_current = current_id[0] && strcmp(s_results[i].video_id, current_id) == 0;
    bespoke_row2(ctx, y, row_h, s_results[i].title, s_results[i].artist,
                 i == sel_row, is_current ? accent_color() : ui_fg());
  }
  bespoke_frame(ctx, label, hint);
  bespoke_scrollbar_span(ctx, offset, row_pitch);
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
  const char *quote = s_bridge_ready ? active_home_quote(quote_index) : "Finding phone...";
  // Longer quotes need a smaller font to avoid clipping inside the banner.
  const char *quote_font = strlen(quote) > 18 ? FONT_KEY_GOTHIC_14_BOLD : FONT_KEY_GOTHIC_18_BOLD;
  GFont font = ui_font(quote_font);

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
  "Recently Played", "Most Played", "Cached Music", "Favorites", "Playlists", "Continue",
  "Recent Searches",
};
// Maps a Library menu row to its library type (parallel to LIBRARY_ITEMS).
static const int LIBRARY_TYPES[] = {
  LibraryRecent, LibraryMostPlayed, LibraryCached, LibraryFavorites, LibraryPlaylists,
  LibraryContinue, LibraryRecentSearches,
};
static const char *const MENU_ITEMS[] = {"Search", "Library", "Settings", "About"};
// Slots worth naming: About carries the Advanced unlock, Settings the theme cycle.
#define MENU_ITEM_SETTINGS 2

// Bespoke Home: a shared upper surface over an icon dock. The surface carries the
// three things worth saying the instant Home appears, in priority order: the bridge
// is down, something is playing, or a destination is highlighted. The dock is the
// More popup's icon row promoted to the top level - UP/DOWN walk it, SELECT opens,
// and the destinations reuse MENU_ITEMS so SELECT runs the same actions ScreenMenu
// does.
//
// The upper surface is never selectable. Selection lives only on the dock icons;
// highlighting the Now Playing icon fills the surface with the now-playing card
// (accent band - the same "selection fills in the accent" idiom as every other
// screen), and the not-connected alert surface has nowhere to go at all.

// draw_cover_art_window() writes straight into the framebuffer, so it has no way
// to honor a rounded corner. Stamping the corner steps back in afterwards is cheaper
// than any clipping scheme and indistinguishable from a real radius at this size.
static void stamp_corner_caps(GContext *ctx, GRect r, GColor ground) {
  static const uint8_t run[6] = {6, 4, 3, 2, 1, 1};
  graphics_context_set_stroke_color(ctx, ground);
  graphics_context_set_stroke_width(ctx, 1);
  const int left = r.origin.x;
  const int right = r.origin.x + r.size.w - 1;
  for (int dy = 0; dy < 6; dy++) {
    const int n = run[dy];
    const int top = r.origin.y + dy;
    const int bottom = r.origin.y + r.size.h - 1 - dy;
    graphics_draw_line(ctx, GPoint(left, top), GPoint(left + n - 1, top));
    graphics_draw_line(ctx, GPoint(right - n + 1, top), GPoint(right, top));
    graphics_draw_line(ctx, GPoint(left, bottom), GPoint(left + n - 1, bottom));
    graphics_draw_line(ctx, GPoint(right - n + 1, bottom), GPoint(right, bottom));
  }
}

// "Art bleed": the cover art *is* the card, with a solid band across the bottom
// carrying the track. The band follows the same rule every other bespoke surface does
// - accent fill with on-accent ink when this is the selected ring entry, ground with
// normal ink when it is not - so selection still reads the same way it does in a list,
// without needing a highlight the artwork would fight.
//
// The progress strip sits above the band rather than inside it: on the art it stays
// visible whichever way the artwork is lit, and when the band is accent-filled the
// played portion runs into it so the two read as one shape.
static void draw_home_hero(GContext *ctx, bool selected) {
  const GRect card = GRect(5, 6, 190, 150);
  const int band_h = 42;
  const int band_y = card.origin.y + card.size.h - band_h;   // 112

  if (!s_bridge_ready) {
    // Nothing is playing and there is no art to bleed, so this state keeps the plain
    // card it always had.
    const GColor text = selected ? on_accent_color() : ui_fg();
    if (selected) {
      graphics_context_set_fill_color(ctx, accent_color());
      graphics_fill_rect(ctx, card, 6, GCornersAll);
    }
    draw_text(ctx, "NOT CONNECTED", ui_font(FONT_KEY_GOTHIC_14_BOLD),
              text, GRect(18, 22, 176, 16), GTextAlignmentLeft);
    graphics_context_set_text_color(ctx, selected ? on_accent_color() : ui_dim());
    graphics_draw_text(ctx, "Open music-src on phone",
                       ui_font(FONT_KEY_GOTHIC_18),
                       GRect(18, 44, 164, 48), GTextOverflowModeWordWrap,
                       GTextAlignmentLeft, NULL);
    return;
  }

  const GColor band_bg = selected ? accent_color() : ui_bg();
  const GColor band_ink = selected ? on_accent_color() : ui_fg();

  // Same condition draw_cover_art_window() gates itself on, so the fallback shows
  // exactly when the art would not have drawn.
  const GRect art = GRect(card.origin.x, card.origin.y,
                          card.size.w, card.size.h - band_h);
  if (s_cover_art_background && s_cover_art_ready) {
    // A window onto the shared composition, not a crop of its own: the band-less
    // card is the shortest of the three artwork surfaces, so it shows the least of
    // the cover - and opening Now Playing from here only grows the window downward.
    draw_cover_art_window(ctx, ART_FRAME, art);
  } else {
    // The same placeholder Now Playing uses, so a cover still on its way in looks the
    // same on both screens and the card keeps a face either way. It replaced a bare
    // note glyph on the ground, which read as "nothing is playing" rather than
    // "the artwork has not arrived".
    draw_art_placeholder(ctx, art, s_cover_art_receiving, GCornersTop);
  }

  graphics_context_set_fill_color(ctx, band_bg);
  graphics_fill_rect(ctx, GRect(card.origin.x, band_y, card.size.w, band_h),
                     6, GCornersBottom);
  stamp_corner_caps(ctx, card, ui_bg());

  // No progress rail here. Home's job is to say what is playing and let you get to it;
  // the rail belongs to Now Playing, which is one press away and has the room to show
  // it properly. Drawing it in both places also meant Home redrawing once a second to
  // advance four pixels.

  // Play state keeps a narrow column at the band's right end. It was the word "PAUSED"
  // before; as a glyph it costs 22px instead of 70, which is what buys the title its
  // full width. The route is not here - it is on Now Playing and in the More popup, and
  // on Home it was one more thing competing with the artwork.
  const GPoint state_at = GPoint(177, band_y + 21);
  if (s_playback_active) {
    graphics_context_set_fill_color(ctx, band_ink);
    graphics_fill_rect(ctx, GRect(state_at.x - 5, state_at.y - 6, 3, 12), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(state_at.x + 2, state_at.y - 6, 3, 12), 0, GCornerNone);
  } else {
    graphics_context_set_fill_color(ctx, band_ink);
    GPoint tri[] = {
      GPoint(state_at.x - 4, state_at.y - 6),
      GPoint(state_at.x + 6, state_at.y),
      GPoint(state_at.x - 4, state_at.y + 6),
    };
    GPathInfo pi = { .num_points = 3, .points = tri };
    GPath *p = gpath_create(&pi);
    gpath_draw_filled(ctx, p);
    gpath_destroy(p);
  }
  draw_text(ctx, s_now_playing.title, ui_font(FONT_KEY_GOTHIC_18_BOLD),
            band_ink, GRect(14, band_y + 2, 148, 22), GTextAlignmentLeft);
  draw_text(ctx, s_now_playing.artist, ui_font(FONT_KEY_GOTHIC_14),
            band_ink, GRect(14, band_y + 22, 148, 18), GTextAlignmentLeft);
}

// Parallel to MENU_ITEMS: what each destination holds. Shown under the destination's
// name when the dock highlight is on it - the More popup's name-over-description idiom.
static const char *const MENU_HINTS[] = {
  "Songs, artists, radio", "Recent, cached, saved", "Output, volume, more", "Version and credits",
};

// Home's dock leads with a Now Playing entry that exists only while something is
// playing - the one destination on the dock that is not always reachable - followed
// by MENU_ITEMS. Its icon is a fixed play glyph; the live transport state stays on
// the card and the Now Playing screen.
#define HOME_DOCK_NOW_PLAYING 0

static bool home_dock_is_now_playing(int i) {
  return s_has_now_playing && i == HOME_DOCK_NOW_PLAYING;
}

// MENU_ITEMS index for a dock entry; only meaningful when it is not Now Playing.
static int home_dock_menu_index(int i) {
  return s_has_now_playing ? i - 1 : i;
}

static int home_dock_count(void) {
  return (int) ARRAY_LENGTH(MENU_ITEMS) + (s_has_now_playing ? 1 : 0);
}

static const char *home_dock_name(int i) {
  return home_dock_is_now_playing(i) ? "Now Playing" : MENU_ITEMS[home_dock_menu_index(i)];
}

static const char *home_dock_hint(int i) {
  return home_dock_is_now_playing(i) ? (s_playback_active ? "Playing now" : "Paused")
                                     : MENU_HINTS[home_dock_menu_index(i)];
}

// The dock's Now Playing entry is a fixed play glyph - the card it selects into
// already carries the transport state, so the icon just says what the entry opens.
static void draw_play_glyph(GContext *ctx, GPoint c, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  GPoint tri[] = {
    GPoint(c.x - 5, c.y - 7),
    GPoint(c.x + 7, c.y),
    GPoint(c.x - 5, c.y + 7),
  };
  GPathInfo pi = { .num_points = 3, .points = tri };
  GPath *path = gpath_create(&pi);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);
}

static void draw_home_list(GContext *ctx) {
  // A stale selection can outlive the track that added a fifth dock entry.
  int sel = s_home_selection;
  if (sel < 0 || sel >= home_dock_count()) sel = 0;
  graphics_context_set_fill_color(ctx, ui_bg());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);

  // The upper surface belongs to the highlighted dock entry: the now-playing card
  // for the Now Playing entry, otherwise the destination's name over what it holds.
  // A down bridge overrides both - that alert matters more than menu context.
  if (!s_bridge_ready) {
    draw_home_hero(ctx, false);
  } else if (home_dock_is_now_playing(sel)) {
    draw_home_hero(ctx, true);
  } else {
    // The card surfaces (now playing, not connected) own the top of the screen, but
    // every menu destination gets the app title back above its name.
    bespoke_eyebrow(ctx, "MUSIC-PBL");
    draw_text(ctx, home_dock_name(sel), ui_font(FONT_KEY_GOTHIC_28_BOLD),
              ui_fg(), GRect(18, 54, 164, 40), GTextAlignmentLeft);
    draw_text(ctx, home_dock_hint(sel), ui_font(FONT_KEY_GOTHIC_18),
              ui_dim(), GRect(18, 96, 164, 24), GTextAlignmentLeft);
  }

  // Nothing here scrolls, so no bespoke_frame stamping - just the dock and the hint.
  const int dock_y = BESPOKE_DOCK_Y;
  const int count = home_dock_count();
  // A fifth icon at the four-icon pitch would push the outer two within 3px of the
  // bezel, so the row tightens instead of overflowing.
  const int dock_pitch = count > 4 ? 38 : BESPOKE_DOCK_PITCH;
  for (int i = 0; i < count; i++) {
    const int cx = 100 + (2 * i - (count - 1)) * dock_pitch / 2;
    GColor glyph = ui_fg();
    if (sel == i) {
      graphics_context_set_fill_color(ctx, accent_color());
      graphics_fill_circle(ctx, GPoint(cx, dock_y), 17);
      glyph = on_accent_color();
    }
    const GPoint gc = GPoint(cx, dock_y);
    if (home_dock_is_now_playing(i)) {
      draw_play_glyph(ctx, gc, glyph);
      continue;
    }
    switch (home_dock_menu_index(i)) {
      case 0: draw_search_icon(ctx, gc, glyph); break;
      case 1: draw_vinyl_icon(ctx, gc, glyph); break;
      case 2: draw_sliders_icon(ctx, gc, glyph); break;
      default: draw_info_icon(ctx, gc, glyph); break;
    }
  }
  draw_text(ctx, "UP/DOWN choose    SELECT open",
            ui_font(FONT_KEY_GOTHIC_14), ui_fg(),
            GRect(8, 204, 184, 18), GTextAlignmentCenter);
}

static const char *const INPUT_CHOICE_ITEMS[] = {"Voice search", "Keyboard"};
// Parallel to INPUT_CHOICE_ITEMS: what each input actually asks of you, shown under
// the highlighted name - the search-type pager's name-over-description idiom.
static const char *const INPUT_CHOICE_HINTS[] = {
  "Speak an artist or song", "Type it on the watch",
};

static const char *const SEARCH_TYPE_ITEMS[] = {"Song Search", "Artist Radio", "Song Radio"};
// Symfonium's local search has no radios (those spin endless queues from a seed - a
// YouTube feature), but it does search its library as songs, albums or artists.
static const char *const SEARCH_TYPE_ITEMS_SYMFONIUM[] = {"Song Search", "Album Search", "Artist Search"};

// Row -> protocol SearchMode, per source. Row order is display order; the mode values
// are the protocol's (song 0 / artist 1 / song-radio 2 / album 3).
static const uint8_t SEARCH_TYPE_MODES[] = {
  SearchModeSong, SearchModeArtist, SearchModeSongRadio,
};
static const uint8_t SEARCH_TYPE_MODES_SYMFONIUM[] = {
  SearchModeSong, SearchModeAlbum, SearchModeArtist,
};

static int search_type_count(void) {
  return (int) ARRAY_LENGTH(SEARCH_TYPE_ITEMS);
}

static const char *search_type_label(int row) {
  return source_is_symfonium() ? SEARCH_TYPE_ITEMS_SYMFONIUM[row] : SEARCH_TYPE_ITEMS[row];
}

static SearchMode search_type_mode(int row) {
  return (SearchMode) (source_is_symfonium() ? SEARCH_TYPE_MODES_SYMFONIUM[row]
                                             : SEARCH_TYPE_MODES[row]);
}

// Keyboard is the only row shown until unlocked from About (7x SELECT on VERSION) -
// see advanced_item_count()/s_advanced_unlocked - so it must stay index 0.
// "Cover art bg" used to sit between Home quotes and History. The artwork is now the
// Now Playing screen's whole upper half and Home's card face, so turning it off left
// two screens built around a hole; it is always on and the row is gone.
static const char *const ADVANCED_ITEMS[] = {
  "Keyboard",        // 0  \_ Interface
  "Bespoke UI",      // 1  |
  "Theme",           // 2  |
  "Sophie mode",     // 3  |  (Mono only - sits directly under Theme)
  "Home style",      // 4  |
  "Home quotes",     // 5  /
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
    case 0: return s_keyboard_pt2 ? "Grid" : "T9";
    case 1: return s_bespoke_ui ? "On" : "Off";
    case 2: return theme_name();
    case 3: return s_sophie_mode ? "On" : "Off";
    case 4: return s_alt_home ? "Kiwi" : "Unicorn";
    case 5: return s_show_home_quotes ? "Show" : "Hide";
    case 6: snprintf(buf, sizeof(buf), "%d songs", s_history_limit); return buf;
    case 7:
      if (s_search_limit == SEARCH_LIMIT_DEEP) return "Deep";
      snprintf(buf, sizeof(buf), "%d", s_search_limit);
      return buf;
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
  if (s_screen == ScreenSearchType) return search_type_count();
  if (s_screen == ScreenSettings) return settings_item_count();
  if (s_screen == ScreenAdvanced) return advanced_item_count();
  if (s_screen == ScreenResults || s_screen == ScreenLibraryItems || s_screen == ScreenQueue) {
    return s_result_count;
  }
  return 0;
}

static const char *native_menu_title(void) {
  if (s_screen == ScreenLibrary) return "LIBRARY";
  if (s_screen == ScreenMenu) return "MUSIC-PBL";
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
    return LIBRARY_ITEMS[library_item_id(index)];
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
    if (index < 0 || index >= search_type_count()) return "";
    return search_type_label(index);
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
    static char back_stops[32];
    static char music_source[32];
    static char auto_shuffle[28];
    static char advanced[24];
    snprintf(route, sizeof(route), "Output: %s", s_phone_audio ? "Phone" : "Watch");
    snprintf(music_source, sizeof(music_source), "Music source: %s",
             source_is_symfonium() ? "Symfonium" : "YouTube");
    snprintf(watch_volume, sizeof(watch_volume), "Watch volume: %d%%", s_watch_volume);
    snprintf(phone_volume, sizeof(phone_volume), "Phone volume: %d%%", s_phone_volume);
    snprintf(input_mode, sizeof(input_mode), "Input: %s",
             s_input_mode == InputVoice ? "Voice" :
             s_input_mode == InputKeyboard ? "Keyboard" : "Ask");
    snprintf(progress_bar, sizeof(progress_bar), "Progress bar: %s",
             progress_mode_name());
    snprintf(back_stops, sizeof(back_stops), "Back stops: %s", s_back_stops ? "On" : "Off");
    snprintf(auto_shuffle, sizeof(auto_shuffle), "Auto shuffle: %s",
             s_symfonium_auto_shuffle ? "On" : "Off");
    snprintf(advanced, sizeof(advanced), "Advanced");
    // Indexed by SETTINGS_ITEMS id, not by visible row - rows are filtered per source.
    const char *items[] = {input_mode, route, watch_volume, phone_volume, progress_bar,
                           back_stops, music_source, auto_shuffle, advanced};
    if (index < 0 || index >= settings_item_count()) return "";
    return items[settings_item_id(index)];
  }
  if (s_screen == ScreenAdvanced) {
    static char row[40];
    if (index < 0 || index >= advanced_item_count()) return "";
    int id = advanced_item_id(index);
    snprintf(row, sizeof(row), "%s: %s", ADVANCED_ITEMS[id], advanced_value(id));
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
                     ui_font(FONT_KEY_GOTHIC_18_BOLD),
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
    graphics_draw_text(ctx, ">", ui_font(FONT_KEY_GOTHIC_18_BOLD),
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
    DW_TRACE("[MenuSync] screen=%d animated=%d raw_sel=%d count=%d",
            (int) s_screen, (int) animated, *selection, count);
    if (count <= 0) {
      *selection = 0;
    } else {
      if (*selection < 0) *selection = 0;
      if (*selection >= count) *selection = count - 1;
    }
    menu_layer_reload_data(s_native_menu_layer);
    MenuIndex selected = (MenuIndex) {.section = 0, .row = *selection};
    DW_TRACE("[MenuSync] target_row=%d widget_row_before=%d",
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
    DW_TRACE("[MenuSync] widget_row_after=%d",
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
    draw_text(ctx, items[i], ui_font(FONT_KEY_GOTHIC_24_BOLD),
              selected ? GColorWhite : GColorBlack, GRect(14, y + 7, 168, 24),
              GTextAlignmentLeft);
    draw_text(ctx, ">", ui_font(FONT_KEY_GOTHIC_18_BOLD),
              selected ? GColorWhite : GColorDarkGray, GRect(176, y + 7, 14, 24),
              GTextAlignmentRight);
  }
  // Header drawn last so rows scrolled above it are covered.
  graphics_context_set_fill_color(ctx, accent_color());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 31), 0, GCornerNone);
  draw_text(ctx, title, ui_font(FONT_KEY_GOTHIC_18_BOLD), GColorWhite,
            GRect(9, 4, 182, 24), GTextAlignmentLeft);
  draw_scrollbar(ctx, GRect(195, 35, 3, 189), offset, viewport_h,
                 viewport_h + s_scroll_max);
}

static const char *library_items_title(void) {
  switch (s_library_type) {
    case LibraryRecent: return "RECENTLY PLAYED";
    case LibraryCached: return "CACHED MUSIC";
    case LibraryFavorites: return "FAVORITES";
    case LibraryPlaylists: return "PLAYLISTS";
    case LibraryContinue: return "CONTINUE";
    case LibraryRecentSearches: return "RECENT SEARCHES";
    case LibraryMostPlayed: return "MOST PLAYED";
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
    // The backing smart playlist is something the user makes in Symfonium; the empty
    // state is where that gets said.
    case LibraryMostPlayed: return "Add a Most Played playlist";
    default: return "Nothing here yet";
  }
}

// What each Library section holds. These are descriptions, not counts: the watch only
// learns a section's size once it has asked the phone for that section, so a live count
// on this screen would be a number we do not have yet.
static const char *const LIBRARY_HINTS[] = {
  "What you played last",
  "Your most-played songs",
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
  graphics_context_set_fill_color(ctx, ui_bg());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  for (int i = 0; i < count; i++) {
    int y = BESPOKE_LIST_TOP + i * row_pitch - offset;
    if (y + row_h < BESPOKE_LIST_TOP || y > BESPOKE_FOOTER_TOP) continue;
    bespoke_row2(ctx, y, row_h, LIBRARY_ITEMS[library_item_id(i)], LIBRARY_HINTS[library_item_id(i)],
                 i == s_menu_selection, ui_fg());
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
  draw_text(ctx, library_items_title(), ui_font(FONT_KEY_GOTHIC_18_BOLD),
            GColorWhite, GRect(8, 4, 184, 24), GTextAlignmentLeft);
  if (s_library_loading) {
    draw_status_mascot(ctx, GPoint(76, 66));
    draw_text(ctx, "Loading...", ui_font(FONT_KEY_GOTHIC_18_BOLD),
              GColorBlack, GRect(10, 125, 180, 26), GTextAlignmentCenter);
    return;
  }
  // Only loading/empty reach the canvas in stock style: screen_uses_native_menu()
  // hands every populated library list to the MenuLayer, so there is no row-drawing
  // path here to keep.
  draw_status_mascot(ctx, GPoint(76, 58));
  draw_text(ctx, library_items_empty(), ui_font(FONT_KEY_GOTHIC_18_BOLD),
            GColorBlack, GRect(10, 116, 180, 28), GTextAlignmentCenter);
}

// Queue screen: reachable from the Now Playing "More" popup. Styled after the
// redesigned Now Playing screens (chromeless white canvas, small dark-gray eyebrow
// label instead of a colored header bar) rather than the older accent-header list
// style draw_library_items/draw_native_menu use, per the visual direction those were
// redesigned toward. The currently-playing row is picked out in accent_color() (even
// when not selected) so "what's now" stays visible while scrolling "what's next".
// Queue is the screen the rest of the bespoke UI was derived from, so its populated
// list goes through the shared row renderer (byte-identical output). Its loading/empty
// states were the last place still drawing the mascot; they now use the same flat
// bespoke_empty() as Library items, so every empty state in the app reads alike.
static void draw_queue(GContext *ctx) {
  // With Bespoke UI off, a populated queue is rendered by the native MenuLayer instead,
  // same as Cached Music; this only handles the loading/empty states then.
  if (screen_uses_native_menu(ScreenQueue)) return;
  if (s_queue_loading) {
    bespoke_empty(ctx, "QUEUE", "Loading...", NULL, NULL);
    return;
  }
  if (s_result_count == 0) {
    bespoke_empty(ctx, "QUEUE", "Nothing queued", NULL, "BACK close");
    return;
  }
  bespoke_song_list(ctx, "QUEUE", "SELECT jump    BACK close", bespoke_now_playing_id());
}

// Must track SETTINGS_ITEMS, which is declared below this point:
// Input, Output, Watch volume, Phone volume, Progress bar, Back stops, Music source,
// Auto shuffle, Advanced. Advanced stays last - the native menu keys the "link onward"
// chevron off it being the final row.
#define SETTINGS_ITEM_COUNT 9

// Whether a Settings row is on screen. Symfonium plays through its own app on the phone,
// so for that source the watch-speaker route and the watch volume that feeds it have
// nothing to act on and would only offer a switch that silently does nothing.
static bool settings_item_shown(int id) {
  if (id == 1 || id == 2) return !source_is_symfonium();
  // Auto shuffle is the mirror case: it asks the Symfonium backend to shuffle the
  // playlists it starts, so under YouTube there is nothing for it to act on.
  if (id == 7) return source_is_symfonium();
  return true;
}

static int settings_item_count(void) {
  int n = 0;
  for (int id = 0; id < SETTINGS_ITEM_COUNT; id++) {
    if (settings_item_shown(id)) n++;
  }
  return n;
}

// Visible row -> SETTINGS_ITEMS index, so the gaps live in one place (as with Advanced).
static int settings_item_id(int row) {
  for (int id = 0; id < SETTINGS_ITEM_COUNT; id++) {
    if (!settings_item_shown(id)) continue;
    if (row-- == 0) return id;
  }
  return SETTINGS_ITEM_COUNT - 1;
}

// Whether an Advanced row is on screen at all. Two filters stack:
//
//  - Only Keyboard (index 0) is strictly needed day-to-day; everything else stays
//    hidden until unlocked by holding SELECT on the menu's About row - see
//    s_advanced_unlocked.
//  - Home style and Home quotes dress the *stock* Home only: the bespoke Home draws
//    neither the full-screen background art nor the quote banner, so under the
//    bespoke UI they were two rows that changed nothing you could see.
//
// Everything else - keyboard style, theme, cover art, the Library and Audio groups -
// is shared by both UIs and stays put.
static bool advanced_item_shown(int id) {
  if (!s_advanced_unlocked) return id == 0;
  // Sophie mode rides with Mono and is offered nowhere else, so it appears directly
  // under Theme only once Theme is set to it.
  if (id == 3) return s_theme == ThemeMono;
  if (id == 4 || id == 5) return !s_bespoke_ui;
  // The Audio group tunes the watch-speaker stream and the phone's own stream cache.
  // Symfonium plays through its own app, so none of it applies to that source.
  if (id == 9 || id == 10 || id == 11) return !source_is_symfonium();
  return true;
}

// Rows currently on screen. Callers index rows, not ADVANCED_ITEMS - advanced_item_id()
// converts, so the gaps live in one place.
static int advanced_item_count(void) {
  int n = 0;
  for (int id = 0; id < (int) ARRAY_LENGTH(ADVANCED_ITEMS); id++) {
    if (advanced_item_shown(id)) n++;
  }
  return n;
}

// Visible row -> ADVANCED_ITEMS index, for the label, the value and the SELECT handler.
static int advanced_item_id(int row) {
  for (int id = 0; id < (int) ARRAY_LENGTH(ADVANCED_ITEMS); id++) {
    if (!advanced_item_shown(id)) continue;
    if (row-- == 0) return id;
  }
  return 0;
}

// Library shows Recently Played + Cached Music + Favorites + Playlists by default;
// the remaining sections (Continue / Recent Searches) appear when Library extras is
// enabled.
static bool library_item_shown(int id) {
  // Most Played is backed by a Symfonium smart playlist, so only that source can
  // serve it.
  if (id == 1) return source_is_symfonium();
  // Cached Music is the stream cache the YouTube backend fills, and Continue / Recent
  // Searches are its listening history. Symfonium keeps its own library and serves none
  // of the three, so those rows would open onto an empty list.
  if (source_is_symfonium()) return id == 0 || id == 3 || id == 4;
  if (id == 5 || id == 6) return s_extra_library;
  return true;
}

static int library_item_count(void) {
  int n = 0;
  for (int id = 0; id < (int) ARRAY_LENGTH(LIBRARY_ITEMS); id++) {
    if (library_item_shown(id)) n++;
  }
  return n;
}

// Visible row -> LIBRARY_ITEMS index, so the gaps live in one place (as with Settings
// and Advanced). LIBRARY_TYPES is parallel to LIBRARY_ITEMS, so this indexes both.
static int library_item_id(int row) {
  for (int id = 0; id < (int) ARRAY_LENGTH(LIBRARY_ITEMS); id++) {
    if (!library_item_shown(id)) continue;
    if (row-- == 0) return id;
  }
  return 0;
}

static int current_menu_item_count(void) {
  if (s_screen == ScreenLibrary) return library_item_count();
  if (s_screen == ScreenMenu) return (int) ARRAY_LENGTH(MENU_ITEMS);
  if (s_screen == ScreenInputChoice) return 2;
  if (s_screen == ScreenSearchType) return search_type_count();
  if (s_screen == ScreenAdvanced) return advanced_item_count();
  return settings_item_count();
}

// Parallel to the Settings row order used by native_menu_item_title() and the SELECT
// handler: Input, Output, Watch volume, Phone volume, Progress bar, Advanced.
static const char *const SETTINGS_ITEMS[] = {
  "Input", "Output", "Watch volume", "Phone volume", "Progress bar", "Back stops",
  "Music source", "Auto shuffle", "Advanced",
};

static const char *settings_value(int index) {
  static char buf[10];
  switch (index) {
    case 0: return s_input_mode == InputVoice ? "Voice"
                 : s_input_mode == InputKeyboard ? "Keyboard" : "Ask";
    case 1: return s_phone_audio ? "Phone" : "Watch";
    case 2: snprintf(buf, sizeof(buf), "%d%%", s_watch_volume); return buf;
    case 3: snprintf(buf, sizeof(buf), "%d%%", s_phone_volume); return buf;
    case 4: return progress_mode_name();
    case 5: return s_back_stops ? "On" : "Off";
    case 6: return source_is_symfonium() ? "Symfonium" : "YouTube";
    case 7: return s_symfonium_auto_shuffle ? "On" : "Off";
    default: return NULL;   // Advanced is a link onward, not a value.
  }
}

static void draw_settings_bespoke(GContext *ctx) {
  const int row_pitch = BESPOKE_ROW_PITCH;
  const int row_h = BESPOKE_ROW_H;
  int count = settings_item_count();
  int offset = scroll_list_layout(row_pitch, count, s_menu_selection,
                                  BESPOKE_LIST_TOP, BESPOKE_VIEWPORT_H, true);
  graphics_context_set_fill_color(ctx, ui_bg());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  for (int i = 0; i < count; i++) {
    int y = BESPOKE_LIST_TOP + i * row_pitch - offset;
    if (y + row_h < BESPOKE_LIST_TOP || y > BESPOKE_FOOTER_TOP) continue;
    const int id = settings_item_id(i);
    bespoke_row1(ctx, y, row_h, SETTINGS_ITEMS[id], settings_value(id), i == s_menu_selection);
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
  // Groups are spans of ADVANCED_ITEMS, but the rows inside one can be filtered out
  // (see advanced_item_shown), so both passes walk item ids while counting visible
  // rows separately - that row counter is what s_menu_selection indexes. A group whose
  // rows are all hidden drops its header with them.
  int content_h = 0, sel_top = 0, sel_bottom = row_pitch;
  int row = 0;
  for (unsigned g = 0; g < ARRAY_LENGTH(ADVANCED_GROUPS); g++) {
    const AdvancedGroup *grp = &ADVANCED_GROUPS[g];
    bool header_drawn = false;
    for (int id = grp->first; id < grp->first + grp->count; id++) {
      if (!advanced_item_shown(id)) continue;
      if (!header_drawn) {
        content_h += header_h;
        header_drawn = true;
      }
      if (row == s_menu_selection) {
        sel_top = content_h;
        sel_bottom = content_h + row_pitch;
      }
      content_h += row_pitch;
      row++;
    }
  }
  int offset = bespoke_scroll(content_h, sel_top, sel_bottom);
  graphics_context_set_fill_color(ctx, ui_bg());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  int y = BESPOKE_LIST_TOP - offset;
  row = 0;
  for (unsigned g = 0; g < ARRAY_LENGTH(ADVANCED_GROUPS); g++) {
    const AdvancedGroup *grp = &ADVANCED_GROUPS[g];
    bool header_drawn = false;
    for (int id = grp->first; id < grp->first + grp->count; id++) {
      if (!advanced_item_shown(id)) continue;
      if (!header_drawn) {
        if (y + header_h > BESPOKE_LIST_TOP && y < BESPOKE_FOOTER_TOP) {
          draw_text(ctx, grp->label, header_font(HeaderFont14),
                    ui_dim(), GRect(18, y + 1, 164, 18), GTextAlignmentLeft);
        }
        y += header_h;
        header_drawn = true;
      }
      if (y + row_h > BESPOKE_LIST_TOP && y < BESPOKE_FOOTER_TOP) {
        bespoke_row1(ctx, y, row_h, ADVANCED_ITEMS[id], advanced_value(id),
                     row == s_menu_selection);
      }
      y += row_pitch;
      row++;
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
  char back_stops[32];
  char advanced[24];
  snprintf(route, sizeof(route), "Output: %s", s_phone_audio ? "Phone" : "Watch");
  snprintf(watch_volume, sizeof(watch_volume), "Watch volume: %d%%", s_watch_volume);
  snprintf(phone_volume, sizeof(phone_volume), "Phone volume: %d%%", s_phone_volume);
  snprintf(input_mode, sizeof(input_mode), "Input: %s",
           s_input_mode == InputVoice ? "Voice" :
           s_input_mode == InputKeyboard ? "Keyboard" : "Ask");
  snprintf(progress_bar, sizeof(progress_bar), "Progress bar: %s",
           progress_mode_name());
  snprintf(back_stops, sizeof(back_stops), "Back stops: %s", s_back_stops ? "On" : "Off");
  snprintf(advanced, sizeof(advanced), "Advanced");
  const char *items[] = {input_mode, route, watch_volume, phone_volume, progress_bar,
                         back_stops, advanced};
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
    int id = advanced_item_id(i);
    snprintf(rows[i], sizeof(rows[i]), "%s: %s", ADVANCED_ITEMS[id], advanced_value(id));
    items[i] = rows[i];
  }
  draw_native_menu(ctx, "ADVANCED", items, count);
}

static void draw_input_choice(GContext *ctx) {
  static const char *const items[] = {"Voice search", "Keyboard"};
  if (s_bespoke_ui) {
    // The third pager, on bespoke Home's grid like the search-type screen it follows:
    // eyebrow, the highlighted input in the 28px name slot, what it asks of you in
    // the 18px description slot, and its glyph on Home's dock line with the selection
    // in the accent disc. UP/DOWN walk the pair (s_menu_selection cycles via the
    // shared menu handlers); SELECT opens one. Nothing scrolls, so no
    // bespoke_ground/frame stamping.
    const int count = (int) ARRAY_LENGTH(items);
    int sel = s_menu_selection;
    if (sel < 0) sel = 0;
    if (sel >= count) sel = count - 1;

    graphics_context_set_fill_color(ctx, ui_bg());
    graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
    bespoke_eyebrow(ctx, "SEARCH WITH");
    draw_text(ctx, items[sel], ui_font(FONT_KEY_GOTHIC_28_BOLD),
              ui_fg(), GRect(18, 54, 164, 40), GTextAlignmentLeft);
    draw_text(ctx, INPUT_CHOICE_HINTS[sel], ui_font(FONT_KEY_GOTHIC_18),
              ui_dim(), GRect(18, 96, 164, 24), GTextAlignmentLeft);
    for (int i = 0; i < count; i++) {
      const int cx = 100 + (2 * i - (count - 1)) * BESPOKE_DOCK_PITCH / 2;
      GColor glyph = ui_fg();
      if (i == sel) {
        graphics_context_set_fill_color(ctx, accent_color());
        graphics_fill_circle(ctx, GPoint(cx, BESPOKE_DOCK_Y), 17);
        glyph = on_accent_color();
      }
      const GPoint gc = GPoint(cx, BESPOKE_DOCK_Y);
      if (i == 0) draw_mic_icon(ctx, gc, glyph);
      else        draw_keyboard_icon(ctx, gc, glyph);
    }
    draw_text(ctx, "UP/DOWN choose    SELECT open", ui_font(FONT_KEY_GOTHIC_14),
              ui_fg(), GRect(8, 204, 184, 18), GTextAlignmentCenter);
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
static const char *const SEARCH_TYPE_HINTS_SYMFONIUM[] = {
  "Find one track",
  "Find an album",
  "An artist's catalogue",
};

static void draw_search_type(GContext *ctx) {
  if (s_bespoke_ui) {
    // A pager on bespoke Home's grid: the shared LECO eyebrow, the highlighted mode
    // in Home's 28px name slot, what it will queue in the 18px description slot, and
    // the mode icons as a dock on Home's dock line (BESPOKE_DOCK_Y/_PITCH), the
    // selection in the same accent disc Home uses. UP/DOWN page between modes
    // (s_menu_selection already cycles via the shared menu handlers); SELECT chooses.
    // Nothing here scrolls, so no bespoke_ground/frame stamping.
    const int count = search_type_count();
    int sel = s_menu_selection;
    if (sel < 0) sel = 0;
    if (sel >= count) sel = count - 1;

    graphics_context_set_fill_color(ctx, ui_bg());
    graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
    bespoke_eyebrow(ctx, "SEARCH");
    draw_text(ctx, search_type_label(sel), ui_font(FONT_KEY_GOTHIC_28_BOLD),
              ui_fg(), GRect(18, 54, 164, 40), GTextAlignmentLeft);
    // 18px descriptive text keeps the gray (like Home's destination hint) so the
    // big name stays on top of the hierarchy; only 14px text goes full ink.
    graphics_context_set_text_color(ctx, ui_dim());
    graphics_draw_text(ctx,
                       source_is_symfonium() ? SEARCH_TYPE_HINTS_SYMFONIUM[sel]
                                             : SEARCH_TYPE_HINTS[sel],
                       ui_font(FONT_KEY_GOTHIC_18),
                       GRect(18, 96, 164, 48), GTextOverflowModeWordWrap,
                       GTextAlignmentLeft, NULL);
    for (int i = 0; i < count; i++) {
      const int cx = 100 + (2 * i - (count - 1)) * BESPOKE_DOCK_PITCH / 2;
      GColor glyph = ui_fg();
      if (i == sel) {
        graphics_context_set_fill_color(ctx, accent_color());
        graphics_fill_circle(ctx, GPoint(cx, BESPOKE_DOCK_Y), 17);
        glyph = on_accent_color();
      }
      const GPoint gc = GPoint(cx, BESPOKE_DOCK_Y);
      if (source_is_symfonium()) {
        switch (i) {
          case 0: draw_note_icon(ctx, gc, glyph); break;
          case 1: draw_vinyl_icon(ctx, gc, glyph); break;
          default: draw_person_icon(ctx, gc, glyph); break;
        }
      } else {
        switch (i) {
          case 0: draw_note_icon(ctx, gc, glyph); break;
          case 1: draw_person_icon(ctx, gc, glyph); break;
          default: draw_broadcast_icon(ctx, gc, glyph); break;
        }
      }
    }
    draw_text(ctx, "UP/DOWN page    SELECT choose", ui_font(FONT_KEY_GOTHIC_14),
              ui_fg(), GRect(8, 204, 184, 18), GTextAlignmentCenter);
    return;
  }
  draw_native_menu(ctx, "SEARCH",
                   source_is_symfonium() ? SEARCH_TYPE_ITEMS_SYMFONIUM : SEARCH_TYPE_ITEMS,
                   search_type_count());
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
// ---- What's New (About, long-press SELECT) ----
// The running version's changelog, Nothing-style: plain section headers, and every
// bullet led by its own emoji (the firmware's Noto fallback renders them).

typedef enum { WnSection, WnBullet } WnLineKind;
typedef struct { WnLineKind kind; const char *text; } WnLine;

// Keep in step with CHANGELOG.md and APP_VERSION. Newest release only - this screen
// answers "what changed?", not "what is the project history".
// Body only. The screen's name and the version are a fixed header drawn by
// draw_whats_new() - the version from APP_VERSION directly, so it cannot drift from the
// build the way a copy in this table could.
static const WnLine WHATS_NEW[] = {
  // Same shape as 0.6.0's list, and for the same reasons - see whats-new-0.6.0.backup.md
  // in the repo root, which keeps that release's table and the note on why it was ordered
  // this way. General first, source-specific last, fixes under their own header so they
  // never read as things the release added.
  //
  // 0.7.0 leads with Now Playing, then the keyboard - the two places the release is
  // actually felt. Bullets are written to sit on one line at GOTHIC_18 in the 164px
  // column; draw_whats_new() measures each one and wraps, so overshooting costs a second
  // line rather than a clipped bullet.
  //
  // Pick emoji from the set that has already rendered on this watch - 0.6.0's list is the
  // proven one, archived in whats-new-0.6.0.backup.md. Two ways to get a blank box:
  //
  //   - anything needing U+FE0F to ask for emoji presentation. "↔️" shipped here briefly
  //     and drew as nothing: U+2194 is an ancient arrow that defaults to a *text* glyph,
  //     and the firmware's fallback does not honour the variation selector.
  //   - anything much past Unicode 11. "🧹" (11.0) renders, "🪶" (13.0) does not.
  //
  // When in doubt reuse a proven glyph, even one already used in another section, rather
  // than reaching for a better-fitting new one.
  {WnSection, "NOW PLAYING"},
  {WnBullet, "🎨 Art expands, doesn't jump"},
  {WnBullet, "🎯 One button style throughout"},
  {WnBullet, "🧭 Now Playing touch has 6 targets"},
  {WnBullet, "⭕ Center deadzone prevents misfires"},
  {WnBullet, "⬇ Swipe down toggles full-screen art"},
  {WnBullet, "🎛 Top target swaps output / playpause"},
  {WnBullet, "🔀 Clearer shuffle & loop icons"},
  {WnBullet, "🖼 Idle icons stay hidden"},
  {WnBullet, "📏 Titles get the whole card"},
  {WnSection, "KEYBOARD"},
  {WnBullet, "🔤 T9 replaces Classic"},
  {WnBullet, "📱 The letters a phone had"},
  {WnBullet, "⏱ Hold a key for its number"},
  {WnBullet, "📖 Symbols, not a number pad"},
  {WnSection, "IMPROVED"},
  {WnBullet, "🎯 Quick launch to Now Playing"},
  {WnBullet, "⏱ Hold to scroll, all the way"},
  {WnBullet, "🔁 Lists wrap at both ends"},
  {WnBullet, "🧹 1.2KB freed for future work"},
  {WnSection, "SYMFONIUM"},
  {WnBullet, "🎶 Auto shuffle playlists"},
  {WnBullet, "⏯ Top touch target is play/pause"},
  {WnBullet, "✋ No output or heart to tease"},
  {WnBullet, "📚 Recently Played, for real"},
  {WnBullet, "⭐ Most Played joins Library"},
  {WnBullet, "📏 History steps up to 100"},
  {WnSection, "FIXED"},
  {WnBullet, "🔀 Shuffle & repeat now stick"},
  {WnBullet, "💥 Scrolling stopped after a beat"},
};

// The masthead every document screen opens with: the screen's name, and under it the one
// value that names what you are looking at. Both in LECO now. The face the app used to be
// unable to have: every LECO the *platform* ships is a numeral subset, so a title set in
// one came out as missing glyphs and the pair could only match on Gothic. The app carries
// its own full-ASCII cut of LECO 1976 instead (see header_font).
//
// It returns the next free y and is drawn as part of the content, not as a fixed band, so
// it scrolls away with everything else.
static int draw_doc_header(GContext *ctx, int y, const char *title, const char *sub) {
  // 26 rather than the 28 this drew at in Gothic: LECO is the wider face, and at 28
  // both "What's New" and "music-pbl" measure within a couple of pixels of the 176px
  // column - close enough that a rasterizer rounding the other way ellipsizes the
  // masthead. 26 clears it by ~20px and reads no smaller, LECO having the taller cap.
  draw_text(ctx, title, header_font(HeaderFont26), ui_fg(),
            GRect(18, y, 164, 34), GTextAlignmentLeft);
  if (!sub) return y + 42;
  draw_text(ctx, sub, header_font(HeaderFont26), accent_color(),
            GRect(18, y + 31, 164, 34), GTextAlignmentLeft);
  return y + 76;
}

// Bottom of a document screen: publish the scroll extent and draw the bar. The bar spans
// the full display because nothing on these screens is pinned any more.
static void end_doc_screen(GContext *ctx, int y) {
  int overflow = (y + 8) - 228 + s_scroll;
  s_scroll_max = overflow > 0 ? overflow : 0;
  draw_scrollbar(ctx, GRect(194, 0, 4, 228), s_scroll, 228, 228 + s_scroll_max);
}

static void draw_whats_new(GContext *ctx) {
  graphics_context_set_fill_color(ctx, ui_bg());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  // The masthead scrolls with the body now rather than sitting in a fixed band. It cost
  // a third of the display to keep two lines on screen that never changed while you read,
  // and drawing it as content means no re-stamping and no clipping test per bullet.
  int y = 8 - s_scroll;
  y = draw_doc_header(ctx, y, "What's New", APP_VERSION);
  for (unsigned i = 0; i < ARRAY_LENGTH(WHATS_NEW); i++) {
    const WnLine *line = &WHATS_NEW[i];
    const char *font_key;
    GColor color = ui_fg();
    switch (line->kind) {
      case WnSection: font_key = FONT_KEY_GOTHIC_18_BOLD; color = ui_dim(); y += 10; break;
      default: font_key = FONT_KEY_GOTHIC_18; break;
    }
    GFont font = fonts_get_system_font(font_key);
    GRect box = GRect(18, y, 164, 200);
    // Wrapped height, measured - bullets with an emoji can run long enough to wrap.
    GSize size = graphics_text_layout_get_content_size(
        line->text, font, box, GTextOverflowModeWordWrap, GTextAlignmentLeft);
    if (y + size.h > 0 && y < 228) {
      graphics_context_set_text_color(ctx, color);
      graphics_draw_text(ctx, line->text, font, box, GTextOverflowModeWordWrap,
                         GTextAlignmentLeft, NULL);
    }
    y += size.h + 8;
  }
  end_doc_screen(ctx, y);
}

static void draw_about_bespoke(GContext *ctx) {
  graphics_context_set_fill_color(ctx, ui_bg());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  // Same masthead as the screens it leads to, so About reads as their index rather than
  // as a different kind of screen. The standalone "Version" row is gone: the version
  // rides in the header and in What's New's badge, which is where it was ever read.
  int y = 8 - s_scroll;
  y = draw_doc_header(ctx, y, "About", "music-pbl") + 4;
  bespoke_row1(ctx, y, BESPOKE_ROW_H, "What's New", APP_VERSION, s_menu_selection == 0);
  y += BESPOKE_ROW_PITCH;
  bespoke_row1(ctx, y, BESPOKE_ROW_H, "Watch", "Time 2", s_menu_selection == 1);
  y += BESPOKE_ROW_PITCH;
  bespoke_row1(ctx, y, BESPOKE_ROW_H, "Bridge", s_bridge_ready ? "Ready" : "Offline",
               s_menu_selection == 2);
  y += BESPOKE_ROW_PITCH;
  // The credits used to run on underneath these rows, which made About half list and half
  // document and meant the only pressable things on it were above an unrelated wall of
  // text. They are a screen now, so About is four rows and nothing else.
  bespoke_row1(ctx, y, BESPOKE_ROW_H, "Credits", NULL, s_menu_selection == 3);
  y += BESPOKE_ROW_PITCH;

  end_doc_screen(ctx, y);
}

// The credits, lifted off About so each screen does one job. Still a scrolling document
// rather than a list - there is nothing here to press.
static void draw_acks_screen(GContext *ctx) {
  graphics_context_set_fill_color(ctx, ui_bg());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  int y = 8 - s_scroll;
  y = draw_doc_header(ctx, y, "Credits", NULL) + 4;
  for (unsigned i = 0; i < ARRAY_LENGTH(ACK_NAMES); i++) {
    draw_text(ctx, ACK_NAMES[i], ui_font(FONT_KEY_GOTHIC_14_BOLD), ui_fg(),
              GRect(18, y, 164, 18), GTextAlignmentLeft);
    // ui_dim() for the licence line, so the project name is what you scan for and the
    // licence is what you find when you stop on one.
    draw_text(ctx, ACK_DESC[i], ui_font(FONT_KEY_GOTHIC_14), ui_dim(),
              GRect(18, y + 15, 164, 18), GTextAlignmentLeft);
    y += 37;
  }
  end_doc_screen(ctx, y);
}

// About's first row in content space: the 8px top inset, the 76px masthead, and the 4px
// the header adds before the list. Kept next to the draw that lays them out.
// What's New, Watch, Bridge, Credits - the four rows draw_about_bespoke() lays out.
#define ABOUT_ROW_COUNT 4
#define ABOUT_ROW0_Y (8 + 76 + 4)

// Now that About's masthead scrolls with the rows, walking the selection has to bring the
// row with it or the highlight walks straight off the bottom edge.
static void about_reveal_selection(void) {
  int top = ABOUT_ROW0_Y + s_menu_selection * BESPOKE_ROW_PITCH;
  int bottom = top + BESPOKE_ROW_H;
  int target = s_scroll_target;
  if (top < target) target = top;
  else if (bottom > target + 228) target = bottom - 228;
  if (target < 0) target = 0;
  if (target > s_scroll_max) target = s_scroll_max;
  if (target != s_scroll_target) scroll_to(target);
}

// Watch and Bridge answer "what is this number?" for every row at once, rather than one
// row at a time. Those two screens are documents with no selection to land on, so there
// is nothing for a per-row press to be about; SELECT turns the whole readout into a
// glossary and back. Deliberately not persisted - it is a reading mode, not a setting.
static bool s_stats_explain;

// A section rule for the document screens: a small dim caption over a hairline. Cheaper
// than a card and it survives every theme, because both parts are theme colors.
static int draw_doc_section(GContext *ctx, int y, const char *caption) {
  draw_text(ctx, caption, ui_font(FONT_KEY_GOTHIC_14_BOLD), ui_dim(),
            GRect(18, y, 164, 16), GTextAlignmentLeft);
  graphics_context_set_fill_color(ctx, ui_dim());
  graphics_fill_rect(ctx, GRect(18, y + 18, 164, 1), 0, GCornerNone);
  return y + 25;
}

/**
 * One row of a stats document: the label, its value, and - in explain mode - a plain
 * line underneath saying what the number is.
 *
 * Compact, this is exactly the bespoke_row1() call it replaced and returns the same
 * pitch. Explaining, the row box tightens to 26 so the label sits just above its own
 * note instead of floating in the middle of a 39px box, and the pitch becomes whatever
 * the wrapped note actually measured - which is why the caller adds nothing itself and
 * end_doc_screen()'s scroll extent follows the mode for free.
 */
// ---- Doc-screen explainer notes, held in a resource rather than in the binary ----
//
// The thirty sentences explaining the Watch and Bridge rows are 1683 bytes of prose, and
// they are on screen only when "explain stats" is on. As string literals they were 1683
// bytes of the app's 65535-byte static budget (.text + .data + .bss) - the ceiling this
// app actually runs into, and the reason DW_TRACE is compiled out a few lines into this
// file. As a resource they cost nothing there: the resource pack has its own 256KB, and
// the copy loaded here lives on the heap, which is outside the ceiling too.
//
// resources/doc_notes.txt is one note per line, in DOC_NOTE index order. That coupling
// between a text file and integer indices in this file is the fragile part, so it fails
// safe rather than quietly: a file that does not hold exactly DOC_NOTE_COUNT lines turns
// the whole feature off, and every row draws without its note. Pairing a row with some
// other row's sentence would be worse than showing none.
#define DOC_NOTE_COUNT 30
#define DOC_NOTE(n) doc_note(n)

static char *s_doc_notes;        // heap copy, one NUL-terminated note after another
static bool s_doc_notes_failed;  // tried once and could not; do not keep retrying

static void doc_notes_unload(void) {
  free(s_doc_notes);
  s_doc_notes = NULL;
}

static void doc_notes_load(void) {
  if (s_doc_notes || s_doc_notes_failed) return;
  ResHandle handle = resource_get_handle(RESOURCE_ID_DOC_NOTES);
  size_t size = resource_size(handle);
  char *buf = size ? malloc(size + 1) : NULL;
  if (!buf || resource_load(handle, (uint8_t *) buf, size) != size) {
    free(buf);
    s_doc_notes_failed = true;
    APP_LOG(APP_LOG_LEVEL_ERROR, "doc notes: could not load %d bytes", (int) size);
    return;
  }
  buf[size] = '\0';
  int lines = 0;
  for (size_t i = 0; i < size; i++) {
    if (buf[i] == '\n') {
      buf[i] = '\0';
      lines++;
    }
  }
  if (lines != DOC_NOTE_COUNT) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "doc notes: %d lines, expected %d", lines, DOC_NOTE_COUNT);
    free(buf);
    s_doc_notes_failed = true;
    return;
  }
  s_doc_notes = buf;
}

// Walks to the nth note instead of keeping a pointer table. Thirty pointers would be 120
// bytes of .bss - taken from the very budget this exists to free - and the walk is a
// handful of strlen()s on a screen that only redraws when it scrolls.
//
// NULL when unavailable, which is exactly what doc_stat() already treats as "no note",
// so a failed load degrades to the compact layout instead of breaking the screen.
static const char *doc_note(int index) {
  doc_notes_load();
  if (!s_doc_notes || index < 0 || index >= DOC_NOTE_COUNT) return NULL;
  const char *p = s_doc_notes;
  while (index-- > 0) p += strlen(p) + 1;
  return p;
}

static int doc_stat(GContext *ctx, int y, const char *label, const char *value,
                    const char *note) {
  const int row_h = s_stats_explain && note ? 26 : BESPOKE_ROW_H;
  bespoke_row1(ctx, y, row_h, label, value, false);
  if (!s_stats_explain || !note) return y + BESPOKE_ROW_PITCH;
  GFont font = ui_font(FONT_KEY_GOTHIC_14);
  GRect box = GRect(BESPOKE_TEXT_X, y + row_h, BESPOKE_TEXT_W, 80);
  GSize size = graphics_text_layout_get_content_size(note, font, box,
                                                     GTextOverflowModeWordWrap,
                                                     GTextAlignmentLeft);
  graphics_context_set_text_color(ctx, ui_dim());
  graphics_draw_text(ctx, note, font, box, GTextOverflowModeWordWrap,
                     GTextAlignmentLeft, NULL);
  return y + row_h + size.h + 8;
}

// These two screens carry no footer hint band, and a wall of numbers gives no sign that
// it has anything more to say - so the offer goes directly under the masthead.
static int draw_stats_hint(GContext *ctx, int y) {
  draw_text(ctx, s_stats_explain ? "SELECT hides notes" : "SELECT explains these",
            ui_font(FONT_KEY_GOTHIC_14), accent_color(),
            GRect(18, y, BESPOKE_TEXT_W, 18), GTextAlignmentLeft);
  return y + 22;
}

// Watch and Bridge deliberately share no rows. The split is by ownership, not by topic:
// Watch is what this device is and what it is doing with its own silicon, Bridge is the
// state of the link and everything negotiated across it. Audio quality lives on Bridge
// for both ends, because it is a term of the connection rather than a property of a watch.

// Watch: this device. Hardware identity, power, memory, and the decode path the speaker
// is actually fed by. Battery is peeked rather than subscribed - the screen is a snapshot.
static void draw_watch_screen(GContext *ctx) {
  graphics_context_set_fill_color(ctx, ui_bg());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  int y = 8 - s_scroll;
  y = draw_doc_header(ctx, y, "Watch", "Time 2");
  y = draw_stats_hint(ctx, y);

  char buf[24];
  y = draw_doc_section(ctx, y, "HARDWARE");
  // Spelled out rather than taken from watch_info_get_model(): the SDK enum still names
  // this hardware by its Pebble-era codename, and the thing on the wrist is a Core
  // Devices Time 2.
  y = doc_stat(ctx, y, "Brand", "Core Devices", DOC_NOTE(0));
  y = doc_stat(ctx, y, "Platform", "Emery", DOC_NOTE(1));
  y = doc_stat(ctx, y, "Display", "200x228", DOC_NOTE(2));
  WatchInfoVersion fw = watch_info_get_firmware_version();
  snprintf(buf, sizeof(buf), "%d.%d.%d", fw.major, fw.minor, fw.patch);
  y = doc_stat(ctx, y, "Firmware", buf, DOC_NOTE(3));

  y = draw_doc_section(ctx, y, "POWER");
  BatteryChargeState battery = battery_state_service_peek();
  snprintf(buf, sizeof(buf), "%d%%", battery.charge_percent);
  y = doc_stat(ctx, y, "Charge", buf, DOC_NOTE(4));
  y = doc_stat(ctx, y, "Charger",
               battery.is_charging ? "Charging" : (battery.is_plugged ? "Plugged" : "No"), DOC_NOTE(5));

  // In KB, because the numbers this app cares about are KB-sized - a colour cover is 20
  // of them and the whole heap is 76.
  y = draw_doc_section(ctx, y, "MEMORY");
  snprintf(buf, sizeof(buf), "%d KB", (int) (heap_bytes_free() / 1024));
  y = doc_stat(ctx, y, "Heap free", buf, DOC_NOTE(6));
  snprintf(buf, sizeof(buf), "%d KB", (int) (heap_bytes_used() / 1024));
  y = doc_stat(ctx, y, "Heap used", buf, DOC_NOTE(7));

  // The decode path, as constants rather than settings: this is what the watch does to a
  // block on its way to the speaker, and it does not vary.
  y = draw_doc_section(ctx, y, "AUDIO PATH");
  y = doc_stat(ctx, y, "Codec", "IMA ADPCM", DOC_NOTE(8));
  y = doc_stat(ctx, y, "PCM", "16k/16-bit", DOC_NOTE(9));
  snprintf(buf, sizeof(buf), "%d B", ADPCM_BLOCK_SIZE);
  y = doc_stat(ctx, y, "Block", buf, DOC_NOTE(10));
  snprintf(buf, sizeof(buf), "%d", ADPCM_SAMPLES_PER_BLOCK);
  y = doc_stat(ctx, y, "Samples", buf, DOC_NOTE(11));

  // Cache posture, not cache contents: the cached-song list is only fetched on demand, so
  // a count here would be either stale or a lie. The configuration is what this side knows.
  y = draw_doc_section(ctx, y, "CACHE");
  y = doc_stat(ctx, y, "Enabled", s_cache_enabled ? "Yes" : "No", DOC_NOTE(12));
  if (s_cache_enabled) {
    snprintf(buf, sizeof(buf), "%d MB", (int) s_cache_size_mb);
    y = doc_stat(ctx, y, "Budget", buf, DOC_NOTE(13));
    y = doc_stat(ctx, y, "Radio", s_cache_radio ? "On" : "Off", DOC_NOTE(14));
  }

  end_doc_screen(ctx, y);
}

// Bridge: the link. Every row is state main.c already holds, so this screen adds no
// protocol traffic - it only says out loud what the app already knew.
static void draw_bridge_screen(GContext *ctx) {
  graphics_context_set_fill_color(ctx, ui_bg());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  int y = 8 - s_scroll;
  y = draw_doc_header(ctx, y, "Bridge", s_bridge_ready ? "Ready" : "Offline");
  y = draw_stats_hint(ctx, y);

  char buf[24];
  y = draw_doc_section(ctx, y, "LINK");
  snprintf(buf, sizeof(buf), "v%d", PROTOCOL_VERSION);
  y = doc_stat(ctx, y, "Protocol", buf, DOC_NOTE(15));
  // The raw word as well as the decoded bits below it. A companion that negotiated
  // something this build does not know how to name still shows up here as a number.
  snprintf(buf, sizeof(buf), "0x%02X", (unsigned) (s_companion_capabilities & 0xFF));
  y = doc_stat(ctx, y, "Caps", buf, DOC_NOTE(16));
  snprintf(buf, sizeof(buf), "%d B", (int) app_message_inbox_size_maximum());
  y = doc_stat(ctx, y, "Inbox", buf, DOC_NOTE(17));
  snprintf(buf, sizeof(buf), "%d B", (int) app_message_outbox_size_maximum());
  y = doc_stat(ctx, y, "Outbox", buf, DOC_NOTE(18));

  // Negotiated at handshake, so a "No" here is the difference between the companion being
  // old and the companion being down - which the single Ready/Offline line cannot tell you.
  y = draw_doc_section(ctx, y, "CAPABILITIES");
  y = doc_stat(ctx, y, "Snapshots",
               (s_companion_capabilities & CAPABILITY_STATE_SNAPSHOT) ? "Yes" : "No", DOC_NOTE(19));
  y = doc_stat(ctx, y, "Search IDs",
               (s_companion_capabilities & CAPABILITY_SEARCH_REQUEST_ID) ? "Yes" : "No", DOC_NOTE(20));

  y = draw_doc_section(ctx, y, "SESSION");
  y = doc_stat(ctx, y, "Source", source_is_symfonium() ? "Symfonium" : "YouTube", DOC_NOTE(21));
  y = doc_stat(ctx, y, "Route", s_phone_audio ? "Phone" : "Watch", DOC_NOTE(22));
  // The counters that decide whether a late packet is ours or a ghost from the last
  // track. When audio arrives for a stream nobody is playing, these are the two numbers
  // that say so.
  snprintf(buf, sizeof(buf), "%d", (int) s_route_epoch);
  y = doc_stat(ctx, y, "Epoch", buf, DOC_NOTE(23));
  snprintf(buf, sizeof(buf), "%d", (int) s_stream_generation);
  y = doc_stat(ctx, y, "Stream gen", buf, DOC_NOTE(24));
  snprintf(buf, sizeof(buf), "%d", (int) s_expected_sequence);
  y = doc_stat(ctx, y, "Next seq", buf, DOC_NOTE(25));

  // Both ends' terms in one place. Quality is a property of the connection, not of either
  // device, which is why neither half of it lives on the Watch screen.
  y = draw_doc_section(ctx, y, "AUDIO TERMS");
  snprintf(buf, sizeof(buf), "%d%%", s_watch_volume);
  y = doc_stat(ctx, y, "Watch vol", buf, DOC_NOTE(26));
  snprintf(buf, sizeof(buf), "%d%%", s_phone_volume);
  y = doc_stat(ctx, y, "Phone vol", buf, DOC_NOTE(27));
  y = doc_stat(ctx, y, "Watch qual", s_watch_audio_quality ? "Balanced" : "Efficient", DOC_NOTE(28));
  y = doc_stat(ctx, y, "Phone qual", s_phone_audio_quality ? "Balanced" : "Efficient", DOC_NOTE(29));

  end_doc_screen(ctx, y);
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
  draw_text(ctx, "music-pbl", ui_font(FONT_KEY_GOTHIC_24_BOLD), c.foreground,
            GRect(10, y, 180, 30), GTextAlignmentCenter);
  y += 30;
  draw_text(ctx, "Music made for Pebble", ui_font(FONT_KEY_GOTHIC_18), c.secondary,
            GRect(10, y, 180, 24), GTextAlignmentCenter);
  y += 26;
  draw_text(ctx, s_bridge_ready ? "Phone bridge connected" : "Phone bridge unavailable",
            ui_font(FONT_KEY_GOTHIC_14), c.secondary,
            GRect(10, y, 180, 22), GTextAlignmentCenter);
  y += 30;

  graphics_context_set_fill_color(ctx, c.surface);
  graphics_fill_rect(ctx, GRect(20, y, 160, 46), 4, GCornersAll);
  draw_text(ctx, "VERSION", ui_font(FONT_KEY_GOTHIC_14_BOLD), c.secondary,
             GRect(31, y + 5, 138, 16), GTextAlignmentCenter);
  draw_text(ctx, APP_VERSION, ui_font(FONT_KEY_GOTHIC_18_BOLD),
             GColorBlack,
             GRect(31, y + 20, 138, 22), GTextAlignmentCenter);
  y += 57;

  graphics_context_set_stroke_color(ctx, c.surface);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(16, y), GPoint(184, y));
  y += 8;

  draw_text(ctx, "ACKNOWLEDGEMENTS", ui_font(FONT_KEY_GOTHIC_14_BOLD),
            c.accent, GRect(10, y, 180, 25), GTextAlignmentCenter);
  y += 24;
  for (unsigned i = 0; i < ARRAY_LENGTH(ACK_NAMES); i++) {
    draw_text(ctx, ACK_NAMES[i], ui_font(FONT_KEY_GOTHIC_14_BOLD), c.foreground,
              GRect(12, y, 176, 18), GTextAlignmentLeft);
    draw_text(ctx, ACK_DESC[i], ui_font(FONT_KEY_GOTHIC_14), c.secondary,
              GRect(12, y + 16, 176, 18), GTextAlignmentLeft);
    y += 39;
  }

  // 'y' is the bottom of the content in screen space (already offset by -s_scroll).
  // The maximum scroll is however far past the bottom edge the content currently extends.
  int overflow = (y + 8) - 228 + s_scroll;  // 8px bottom padding.
  s_scroll_max = overflow > 0 ? overflow : 0;

  graphics_context_set_fill_color(ctx, c.accent);
  graphics_fill_rect(ctx, GRect(0, 0, 200, 31), 0, GCornerNone);
  draw_text(ctx, "ABOUT", ui_font(FONT_KEY_GOTHIC_18_BOLD), GColorWhite,
             GRect(8, 4, 184, 24), GTextAlignmentLeft);
  // Pixel-based scrollbar: the viewport is the visible area below the header and
  // the content extent is that viewport plus however far the content overflows.
  const int32_t viewport = 228 - 31;
  draw_scrollbar(ctx, GRect(194, 35, 4, 189), s_scroll, viewport,
                 viewport + s_scroll_max);
}

// The T9 keyboard, drawn on the grid keyboard's own layout so the two are the same
// keypad seen twice: the same nine keys in the same places, the same text field, the
// same mode badge, the same three buttons. Only the gesture differs - tap a key and
// tap it again to cycle its three characters, rather than swiping toward a neighbour.
static void draw_keyboard_t9(GContext *ctx) {
  const int grid_left = 2;
  const int grid_top = 58;
  const int gap = 2;
  const int cell_w = 64;
  const int cell_h = 54;

  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);

  graphics_context_set_fill_color(ctx, ui_bg());
  graphics_fill_rect(ctx, GRect(2, 2, 196, 53), 4, GCornersAll);
  draw_text(ctx, s_query_length > 0 ? s_query : "Start typing...",
            ui_font(FONT_KEY_GOTHIC_18_BOLD),
            s_query_length > 0 ? ui_fg() : ui_dim(),
            GRect(8, 8, 184, 39), GTextAlignmentLeft);

  const bool symbols = s_keyboard_mode == 2;
  const bool uppercase = s_keyboard_mode == 1;
  // The grid puts HELP here; T9's gesture is a tap and needs no explaining, so the slot
  // carries the length counter the Classic keyboard used to show along its footer.
  char counter[8];
  snprintf(counter, sizeof(counter), "%d/%d", (int) s_query_length, TEXT_LENGTH - 1);
  draw_text(ctx, counter, ui_font(FONT_KEY_GOTHIC_14_BOLD), GColorDarkGray,
            GRect(143, 4, 48, 16), GTextAlignmentRight);
  draw_text(ctx, symbols ? "!?#" : uppercase ? "ABC" : "abc",
            ui_font(FONT_KEY_GOTHIC_14_BOLD), accent_color(),
            GRect(153, 37, 38, 16), GTextAlignmentRight);

  for (int i = 0; i < 9; i++) {
    const int x = grid_left + (i % 3) * (cell_w + gap);
    const int y = grid_top + (i / 3) * (cell_h + gap);
    const bool pending = i == s_t9_pending;
    // Lit while the finger is on the key, so a tap that lands is visible before it
    // types anything - the grid highlights its keys the same way.
    bool pressed = false;
#ifdef PBL_PLATFORM_EMERY
    pressed = s_touch_active && i == s_touch_origin_key;
#endif
    const bool lit = pending || pressed;

    graphics_context_set_fill_color(ctx, lit ? accent_color() : ui_bg());
    graphics_fill_rect(ctx, GRect(x, y, cell_w, cell_h), 3, GCornersAll);

    // The face of a phone key, printed the way they were: one character large, the
    // key's three small underneath. On the letter screens the large one is the key's
    // number - what holding it types - set in the quiet ink, because it is a landmark
    // rather than a thing you are picking and the size alone carries the hierarchy. The
    // symbol screen has no number to print, so it heroes the character a single tap
    // gives, in full ink.
    const char *cell = symbols ? T9_SYMBOLS[i] : T9_LETTERS[i];
    const char hero[2] = {symbols ? cell[0] : (char) ('1' + i), '\0'};
    draw_text(ctx, hero, ui_font(FONT_KEY_GOTHIC_24_BOLD),
              lit ? on_accent_color() : symbols ? ui_fg() : ui_dim(),
              GRect(x + 2, y + 1, cell_w - 4, 27), GTextAlignmentCenter);

    // Each character gets its own slot rather than the key being drawn as one centred
    // label, so the one the next tap would land on can be marked without the row moving.
    // The slot pitch is the same on every key and the row is centred in what it needs,
    // so the 1 key's two characters and the 7 key's four sit on one rhythm.
    const int count = (int) strlen(cell);
    const int slot_w = (cell_w - 6) / T9_MAX_CHARS;
    const int row_x = x + (cell_w - count * slot_w) / 2;
    for (int j = 0; j < count; j++) {
      char glyph[2] = {cell[j], '\0'};
      if (uppercase && glyph[0] >= 'a' && glyph[0] <= 'z') glyph[0] -= 'a' - 'A';
      if (glyph[0] == ' ') glyph[0] = '_';
      const bool active = pending && j == s_t9_index;
      const GRect slot = GRect(row_x + j * slot_w, y + 31, slot_w, 17);
      // The character the next tap would replace is inverted against the rest of the
      // key. On-accent over accent rather than the page ground: that pairing is the one
      // every theme guarantees to be readable, and Default Dark's indigo on black is
      // exactly the pairing that would not have been.
      if (active) {
        graphics_context_set_fill_color(ctx, on_accent_color());
        graphics_fill_rect(ctx, GRect(slot.origin.x, slot.origin.y, slot_w, 17), 3,
                           GCornersAll);
      }
      draw_text(ctx, glyph, ui_font(FONT_KEY_GOTHIC_14_BOLD),
                active ? accent_color() : lit ? on_accent_color() : ui_fg(),
                slot, GTextAlignmentCenter);
    }
  }
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

    graphics_context_set_fill_color(ctx, ui_bg());
    graphics_fill_rect(ctx, GRect(2, 2, 196, 53), 4, GCornersAll);
    draw_text(ctx, s_query_length > 0 ? s_query : "Start typing...",
              ui_font(FONT_KEY_GOTHIC_18_BOLD),
              s_query_length > 0 ? ui_fg() : ui_dim(),
              GRect(8, 8, 184, 39), GTextAlignmentLeft);

    const bool numbers = s_keyboard_mode == 2;
    const bool uppercase = s_keyboard_mode == 1;
    draw_text(ctx, "HELP", ui_font(FONT_KEY_GOTHIC_14_BOLD), GColorDarkGray,
              GRect(153, 4, 38, 16), GTextAlignmentRight);
    draw_text(ctx, numbers ? "123" : uppercase ? "ABC" : "abc",
              ui_font(FONT_KEY_GOTHIC_14_BOLD), accent_color(),
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

        graphics_context_set_fill_color(ctx, active ? accent_color() : ui_bg());
        graphics_fill_rect(ctx, GRect(x, y, cell_w, cell_h), 3, GCornersAll);
        if (numbers && i == PT2_ZERO_CELL && !zero_fan) {
          // Idle "8" cell: a large 8 with a small 0 hint (swipe up-right for 0).
          GColor glyph = active ? on_accent_color() : dimmed ? GColorLightGray : ui_fg();
          draw_text(ctx, "8", ui_font(FONT_KEY_GOTHIC_18_BOLD), glyph,
                    GRect(x + 2, y + 16, cell_w - 4, 23), GTextAlignmentCenter);
          draw_text(ctx, "0", ui_font(FONT_KEY_GOTHIC_14_BOLD),
                    active ? on_accent_color() : GColorLightGray,
                    GRect(x + cell_w - 20, y + 3, 16, 14), GTextAlignmentRight);
        } else {
          draw_text(ctx, display, ui_font(FONT_KEY_GOTHIC_18_BOLD),
                    active ? on_accent_color() : dimmed ? GColorLightGray : ui_fg(),
                    GRect(x + 2, y + 16, cell_w - 4, 23), GTextAlignmentCenter);
        }
      }
    }
    draw_feedback_overlay(ctx);
    return;
  }

  draw_keyboard_t9(ctx);
}

// Chromeless placeholder: the eyebrow replaces the accent header band, and the mascot
// and message drop to clear it. Same content, same order - only the chrome changes.
static void draw_placeholder_bespoke(GContext *ctx) {
  bespoke_ground(ctx, s_placeholder_title);
  draw_status_mascot(ctx, GPoint(76, 72));
  draw_text(ctx, s_placeholder_message, ui_font(FONT_KEY_GOTHIC_18_BOLD),
            ui_fg(), GRect(18, 165, 164, 48), GTextAlignmentCenter);
}

static void draw_placeholder_screen(GContext *ctx) {
  if (s_bespoke_ui) {
    draw_placeholder_bespoke(ctx);
    return;
  }
  graphics_context_set_fill_color(ctx, ui_bg());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  draw_header(ctx, s_placeholder_title);
  draw_status_mascot(ctx, GPoint(76, 62));
  draw_text(ctx, s_placeholder_message, ui_font(FONT_KEY_GOTHIC_18_BOLD),
            ui_fg(), GRect(18, 157, 164, 48), GTextAlignmentCenter);
}

// Searching, on draw_modern_buffering()'s grid - eyebrow at 18, the subject in the 28px
// title slot, the caption in the 18px description slot, the animation centred on the
// icon line at cy=158, hint at 202. Search and Buffering are back-to-back in the flow,
// so sharing a grid makes the handoff read as one screen resolving rather than a cut.
//
// The dots stay rather than borrowing Buffering's equalizer bars: the two screens
// should be distinguishable at a glance even though they are laid out alike.
//
// Possible future redesign: drop the dots onto the dock line (BESPOKE_DOCK_Y) so the
// transient screens share the pagers' icon height. Deferred for now - Searching and
// Buffering deliberately share a grid, so moving one means moving the other, and
// Buffering belongs to the playback flow, not to this search redesign.
static void draw_searching_bespoke(GContext *ctx) {
  bespoke_ground(ctx, "SEARCHING");
  draw_text(ctx, s_query, ui_font(FONT_KEY_GOTHIC_28_BOLD), ui_fg(),
            GRect(18, 54, 164, 40), GTextAlignmentLeft);
  draw_text(ctx, "Phone is finding audio", ui_font(FONT_KEY_GOTHIC_18), ui_dim(),
            GRect(18, 96, 164, 24), GTextAlignmentLeft);
  for (int i = 0; i < 7; i++) {
    int distance = (i - (s_animation_frame % 7) + 7) % 7;
    int radius = distance == 0 ? 6 : distance == 6 ? 4 : 3;
    graphics_context_set_fill_color(ctx,
        distance <= 1 || distance == 6 ? accent_color() : ui_dim());
    graphics_fill_circle(ctx, GPoint(49 + i * 17, 158), radius);
  }
  draw_text(ctx, "BACK cancel", ui_font(FONT_KEY_GOTHIC_14), ui_fg(),
            GRect(8, 202, 184, 18), GTextAlignmentCenter);
}

static void draw_searching(GContext *ctx) {
  if (s_bespoke_ui) {
    draw_searching_bespoke(ctx);
    return;
  }
  ThemeColors c = colors();
  draw_header(ctx, "PHONE LINK");
  draw_text(ctx, "SEARCHING FOR", ui_font(FONT_KEY_GOTHIC_14_BOLD),
            c.secondary, GRect(10, 31, 180, 19), GTextAlignmentLeft);
  draw_text(ctx, s_query, ui_font(FONT_KEY_GOTHIC_24_BOLD), c.foreground,
            GRect(10, 52, 180, 70), GTextAlignmentLeft);
  for (int i = 0; i < 7; i++) {
    int distance = (i - (s_animation_frame % 7) + 7) % 7;
    int radius = distance == 0 ? 6 : distance == 6 ? 4 : 3;
    graphics_context_set_fill_color(ctx, distance <= 1 || distance == 6 ? c.accent : c.surface);
    graphics_fill_circle(ctx, GPoint(49 + i * 17, 180), radius);
  }
  draw_text(ctx, "Android is finding audio", ui_font(FONT_KEY_GOTHIC_14),
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
  graphics_context_set_fill_color(ctx, ui_bg());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, accent_color());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 31), 0, GCornerNone);
  draw_text(ctx, "SEARCH RESULTS", ui_font(FONT_KEY_GOTHIC_18_BOLD),
            on_accent_color(), GRect(8, 4, 184, 24), GTextAlignmentLeft);
  draw_text(ctx, s_query, ui_font(FONT_KEY_GOTHIC_14), GColorDarkGray,
            GRect(9, 34, 182, 20), GTextAlignmentLeft);
  draw_status_mascot(ctx, GPoint(76, 55));
  draw_text(ctx, "No songs found", ui_font(FONT_KEY_GOTHIC_24_BOLD),
            ui_fg(), GRect(16, 109, 168, 34), GTextAlignmentCenter);
  draw_text(ctx, "Select to search again", ui_font(FONT_KEY_GOTHIC_14),
            GColorDarkGray, GRect(10, 148, 180, 22), GTextAlignmentCenter);
}

static void draw_modern_buffering(GContext *ctx) {
  const SearchResult *result = current_playing_result();
  if (!result) return;
  graphics_context_set_fill_color(ctx, ui_bg());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  // Chromeless, on the pager grid: eyebrow, track title in the 28px slot, artist in
  // the 18px description slot, and the equalizer bars centered on the icon line
  // (cy=158) - so the handoff to Now Playing reads as one screen fading into the next.
  draw_text(ctx, "FINDING AUDIO", ui_font(FONT_KEY_GOTHIC_14_BOLD),
            ui_fg(), GRect(18, 18, 164, 18), GTextAlignmentLeft);
  draw_text(ctx, result->title, ui_font(FONT_KEY_GOTHIC_28_BOLD),
            ui_fg(), GRect(18, 54, 164, 40), GTextAlignmentLeft);
  draw_text(ctx, result->artist, ui_font(FONT_KEY_GOTHIC_18),
            ui_dim(), GRect(18, 96, 164, 24), GTextAlignmentLeft);
  for (int i = 0; i < 5; i++) {
    int phase = (s_animation_frame + i) % 5;
    int height = 12 + (phase <= 2 ? phase : 4 - phase) * 12;
    graphics_context_set_fill_color(ctx, accent_color());
    graphics_fill_rect(ctx, GRect(55 + i * 20, 158 - height / 2, 10, height), 3, GCornersAll);
  }
  draw_text(ctx, "BACK cancel", ui_font(FONT_KEY_GOTHIC_14),
            ui_fg(), GRect(8, 202, 184, 18), GTextAlignmentCenter);
}

static void draw_results(GContext *ctx) {
  if (screen_uses_native_menu(ScreenResults)) return;
  draw_modern_results(ctx);
}

static void draw_buffering(GContext *ctx) {
  draw_modern_buffering(ctx);
}

// Loop and shuffle, told apart by silhouette rather than by detail.
//
// They used to be near-identical: each was two horizontal strokes with an arrowhead at
// opposite ends, in the same 16x18 box, at the same 2px weight. Side by side on the card
// - which is where they appear, 24px apart - the only difference was which end the
// arrowheads sat on, and at 22px that is not a difference anyone reads. Telling them
// apart meant looking twice at a glanceable screen.
//
// Now the outlines differ before any detail resolves: loop is a closed ring, shuffle is
// an open X. Those two shapes cannot be confused at any size, in any theme, and neither
// depends on the arrowheads being legible.

// Loop: a closed ring, with one chevron riding its top edge for direction. Which mode
// the loop is in (all vs one) is the badge draw_modern_song() puts on its shoulder, so
// the glyph itself only has to say "looping".
static void draw_repeat_icon(GContext *ctx, GPoint center, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_round_rect(ctx, GRect(center.x - 8, center.y - 6, 16, 12), 4);
  graphics_draw_line(ctx, GPoint(center.x + 1, center.y - 9),
                     GPoint(center.x + 5, center.y - 6));
  graphics_draw_line(ctx, GPoint(center.x + 1, center.y - 3),
                     GPoint(center.x + 5, center.y - 6));
}

// Shuffle: two paths that cross and leave to the right, each with its own arrowhead.
// The crossing is the whole idea of the icon, and it is what the old one lacked - it
// had one diagonal and two stray horizontals, so it read as a bent arrow.
static void draw_shuffle_icon(GContext *ctx, GPoint center, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(center.x - 8, center.y - 6),
                     GPoint(center.x + 5, center.y + 6));
  graphics_draw_line(ctx, GPoint(center.x - 8, center.y + 6),
                     GPoint(center.x + 5, center.y - 6));
  // Arrowheads on both exits, each opening back along its own path.
  graphics_draw_line(ctx, GPoint(center.x + 5, center.y + 6),
                     GPoint(center.x + 0, center.y + 6));
  graphics_draw_line(ctx, GPoint(center.x + 5, center.y + 6),
                     GPoint(center.x + 5, center.y + 1));
  graphics_draw_line(ctx, GPoint(center.x + 5, center.y - 6),
                     GPoint(center.x + 0, center.y - 6));
  graphics_draw_line(ctx, GPoint(center.x + 5, center.y - 6),
                     GPoint(center.x + 5, center.y - 1));
}

// ---- Transport glyphs ----
//
// Play, pause, next and previous are one family drawn from one geometry, so they read
// as the same control in four states wherever they appear. Everything derives from
// 'half_h', half the glyph's height:
//
//   triangle - back edge TRANSPORT_BACK behind centre, tip a full half_h ahead. At
//              half_h 9 that is the 14x18 play triangle the play button has always
//              drawn, which is where the 5/9 comes from.
//   bar      - TRANSPORT_BAR wide, the glyph's full height. Pause is two of them;
//              skip is the triangle with one at its leading edge.
//   air      - one bar width, between pause's two bars and between skip's triangle
//              and bar alike.
//
// They used to be three independent hand-tuned copies: the play button's 14x18
// triangle, the action bar's 11x14 one, and a skip triangle at 32x52 - half again as
// tall for its width as the other two, with its tip stopping short of its own bar. Put
// beside a play button the skip glyph read as a different icon set. Deriving all three
// from one rule is what makes them match; the play button keeps the exact pixels it
// had, since its proportions are the ones the others were pulled onto.
#define TRANSPORT_BACK(hh) ((hh) * 5 / 9)
#define TRANSPORT_BAR(hh)  ((hh) * 5 / 9 < 2 ? 2 : (hh) * 5 / 9)
#define TRANSPORT_AIR(hh)  ((hh) * 2 / 3)

// The family's right-pointing triangle, filled in the current fill color. 'dir' is 1
// for a triangle pointing right, -1 for left.
static void draw_transport_triangle(GContext *ctx, GPoint c, int half_h, int dir) {
  const int back = TRANSPORT_BACK(half_h);
  GPoint tri[] = {
    GPoint(c.x - dir * back, c.y - half_h),
    GPoint(c.x + dir * half_h, c.y),
    GPoint(c.x - dir * back, c.y + half_h),
  };
  GPathInfo info = { .num_points = ARRAY_LENGTH(tri), .points = tri };
  GPath *path = gpath_create(&info);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);
}

// The family's pair of bars, filled in the current fill color: pause. At half_h 9 the
// bars land on the play button's exact columns (x-8 and x+3, 5 wide), which is where
// TRANSPORT_AIR's 2/3 comes from.
static void draw_transport_bars(GContext *ctx, GPoint c, int half_h) {
  const int bar = TRANSPORT_BAR(half_h);
  const int air = TRANSPORT_AIR(half_h);
  graphics_fill_rect(ctx, GRect(c.x - air / 2 - bar, c.y - half_h, bar, half_h * 2),
                     0, GCornerNone);
  graphics_fill_rect(ctx, GRect(c.x + air / 2, c.y - half_h, bar, half_h * 2),
                     0, GCornerNone);
}

// Skip: the family's triangle with a bar at its leading edge, the pair centred on 'c'.
// The composite is centred rather than the triangle, so a next and a previous glyph at
// the same point are mirror images of each other.
static void draw_skip_glyph(GContext *ctx, GPoint c, GColor color, bool next, int half_h) {
  const int dir = next ? 1 : -1;
  const int back = TRANSPORT_BACK(half_h);
  const int bar = TRANSPORT_BAR(half_h);
  const int air = TRANSPORT_AIR(half_h);
  // Triangle runs back..half_h, then the family's air, then the bar itself.
  const int span = back + half_h + air + bar;
  const GPoint tc = GPoint(c.x - dir * (span / 2 - back), c.y);
  const int bar_x = tc.x + dir * (half_h + air);
  graphics_context_set_fill_color(ctx, color);
  draw_transport_triangle(ctx, tc, half_h, dir);
  graphics_fill_rect(ctx, GRect(next ? bar_x : bar_x - bar, c.y - half_h,
                                bar, half_h * 2), 0, GCornerNone);
}

static void draw_skip_icon(GContext *ctx, GPoint center, GColor color, bool next) {
  draw_skip_glyph(ctx, center, color, next, 8);
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

// The keyboard's "how do I type on this" card. This routine used to serve the Now
// Playing confirmations too - a 72px box with a glyph and a label under it - but those
// moved onto the artwork (draw_art_answer()), and the keyboard has no artwork to move
// onto, so the card survives here and only here.
static void draw_feedback_overlay(GContext *ctx) {
  if (s_feedback_icon != FeedbackKeyboardHint) return;
  const int box_w = 180;
  const int box_h = 70;
  GRect card = GRect(100 - box_w / 2, 114 - box_h / 2, box_w, box_h);
  graphics_context_set_fill_color(ctx, ui_fg());
  graphics_fill_rect(ctx, card, 10, GCornersAll);
  graphics_context_set_stroke_color(ctx, ui_bg());
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_round_rect(ctx, card, 10);
  draw_text(ctx, "Tap for key", ui_font(FONT_KEY_GOTHIC_14_BOLD),
            ui_bg(), GRect(card.origin.x + 6, card.origin.y + 4, box_w - 12, 16),
            GTextAlignmentCenter);
  draw_text(ctx, "Swipe for letters", ui_font(FONT_KEY_GOTHIC_14_BOLD),
            ui_bg(), GRect(card.origin.x + 6, card.origin.y + 19, box_w - 12, 16),
            GTextAlignmentCenter);
  draw_text(ctx, "UP mode   SELECT enter",
            ui_font(FONT_KEY_GOTHIC_14), ui_bg(),
            GRect(card.origin.x + 6, card.origin.y + 35, box_w - 12, 16), GTextAlignmentCenter);
  draw_text(ctx, "DOWN delete   HELP info",
            ui_font(FONT_KEY_GOTHIC_14), ui_bg(),
            GRect(card.origin.x + 6, card.origin.y + 50, box_w - 12, 16), GTextAlignmentCenter);
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
      // The same family as the skips on page 1 and the button on the artwork - bare
      // here rather than on a disc, because the action bar gives every icon the same
      // flat treatment and the segment is only 30px wide.
      if (paused) {
        draw_transport_triangle(ctx, center, 7, 1);
      } else {
        draw_transport_bars(ctx, center, 7);
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

// Which face a transport button wears. One button, four states.
typedef enum { TransportPlay, TransportPause, TransportNext, TransportPrev } TransportFace;

// The accent disc every answer on the artwork is drawn on, returning the ink to knock
// the glyph out in. One helper because every one of those answers - resume, skip,
// favorite, shuffle, output - is the same kind of statement in the same place, and they
// only stay identical if they are literally the same code.
static GColor draw_art_disc(GContext *ctx, GPoint center, int radius) {
  graphics_context_set_fill_color(ctx, accent_color());
  graphics_fill_circle(ctx, center, radius);
  return on_accent_color();
}

// A transport glyph on that disc. Resuming, skipping forward and skipping back used to
// be a disc for play/pause and a bare white glyph for the skips at nearly three times
// the height, which read as two unrelated pieces of UI answering the same press.
//
// The glyph's half-height is 9/20 of the radius: the play triangle's 18px height in
// the radius-20 disc it has always been drawn in, so a paused screen is pixel-identical
// to what it was.
static void draw_transport_button(GContext *ctx, GPoint center, int radius,
                                  TransportFace face) {
  const GColor ink = draw_art_disc(ctx, center, radius);
  graphics_context_set_fill_color(ctx, ink);
  const int half_h = radius * 9 / 20;
  switch (face) {
    case TransportPlay:  draw_transport_triangle(ctx, center, half_h, 1); break;
    case TransportPause: draw_transport_bars(ctx, center, half_h); break;
    case TransportNext:  draw_skip_glyph(ctx, center, ink, true, half_h); break;
    case TransportPrev:  draw_skip_glyph(ctx, center, ink, false, half_h); break;
  }
}

static void draw_play_button(GContext *ctx, GPoint center, int radius, bool paused) {
  draw_transport_button(ctx, center, radius, paused ? TransportPlay : TransportPause);
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

// Microphone, used for "Voice search" on the input-choice pager: a capsule on a
// cradle - the silhouette a mic reads as at this size.
static void draw_mic_icon(GContext *ctx, GPoint c, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_fill_rect(ctx, GRect(c.x - 3, c.y - 9, 6, 11), 3, GCornersAll);
  // Cradle: the bottom of a 12px circle. Same angle convention as
  // draw_broadcast_icon() - 135->225 sweeps through the bottom.
  graphics_draw_arc(ctx, GRect(c.x - 6, c.y - 7, 12, 12), GOvalScaleModeFitCircle,
                    DEG_TO_TRIGANGLE(135), DEG_TO_TRIGANGLE(225));
  graphics_draw_line(ctx, GPoint(c.x, c.y + 5), GPoint(c.x, c.y + 8));
  graphics_draw_line(ctx, GPoint(c.x - 4, c.y + 8), GPoint(c.x + 4, c.y + 8));
}

// Keyboard, used for "Keyboard" on the input-choice pager: a rounded deck with a
// two-row dot grid for keys.
static void draw_keyboard_icon(GContext *ctx, GPoint c, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_fill_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_round_rect(ctx, GRect(c.x - 9, c.y - 6, 18, 13), 2);
  for (int row = 0; row < 2; row++) {
    for (int i = -1; i <= 1; i++) {
      graphics_fill_circle(ctx, GPoint(c.x + i * 5, c.y - 2 + row * 5), 1);
    }
  }
}

static int np_more_count(void);
static NpMoreItem np_more_item_at(int row);

// Modal "More" popup over the now-playing screen. A pager on bespoke Home's grid,
// like the search-type and input-choice screens: the shared LECO eyebrow naming its
// parent screen, the highlighted action in the 28px name slot, its current state in
// the 18px description slot, and the action icons as a dock on the Home line with the
// selection in an accent disc.
static void draw_np_more(GContext *ctx) {
  // Plain solid panel (a dimmed-artwork background was tried but was too slow to redraw).
  graphics_context_set_fill_color(ctx, ui_bg());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  // The 18px state value keeps the dim ink so the big action name stays on top of
  // the hierarchy; only 14px text goes full ink.
  GColor dim_text = ui_dim();
  GColor name_text = ui_fg();
  GColor idle_glyph = ui_fg();

  bespoke_eyebrow(ctx, "NOW PLAYING");

  // The selected action's name and current state, echoing the track title/artist.
  static const char *const names[NP_MORE_COUNT] = {
    "Shuffle", "Repeat", "Favorite", "Output", "New search", "Queue",
  };
  const NpMoreItem current = np_more_item_at(s_np_more_selection);
  char value[12];
  switch (current) {
    case NpMoreShuffle: snprintf(value, sizeof value, "%s", s_shuffle_enabled ? "On" : "Off"); break;
    case NpMoreRepeat: snprintf(value, sizeof value, "%s",
              s_loop_mode == LoopModeAll ? "All" : s_loop_mode == LoopModeOne ? "One" : "Off"); break;
    case NpMoreFavorite: snprintf(value, sizeof value, "%s", s_current_favorite ? "On" : "Off"); break;
    case NpMoreOutput: snprintf(value, sizeof value, "%s", s_phone_audio ? "Phone" : "Watch"); break;
    default: value[0] = '\0'; break;
  }
  draw_text(ctx, names[current], ui_font(FONT_KEY_GOTHIC_28_BOLD),
            name_text, GRect(18, 54, 164, 40), GTextAlignmentLeft);
  if (value[0]) {
    draw_text(ctx, value, ui_font(FONT_KEY_GOTHIC_18), dim_text,
              GRect(18, 96, 164, 24), GTextAlignmentLeft);
  }

  // Row of action icons on Home's dock line; the selected one sits inside an accent
  // circle, and toggles that are currently on are tinted with the accent even when
  // not selected. Six icons at the dock pitch would push the outer discs within 5px
  // of the bezel, so a full row tightens - Home's own rule for a crowded dock.
  const int count = np_more_count();
  const int icon_pitch = count > 4 ? 30 : BESPOKE_DOCK_PITCH;
  for (int i = 0; i < count; i++) {
    int cx = 100 + (2 * i - (count - 1)) * icon_pitch / 2;
    const NpMoreItem item = np_more_item_at(i);
    bool selected = i == s_np_more_selection;
    bool active = (item == NpMoreShuffle && s_shuffle_enabled) ||
                  (item == NpMoreRepeat && s_loop_enabled) ||
                  (item == NpMoreFavorite && s_current_favorite);
    GColor glyph;
    if (selected) {
      graphics_context_set_fill_color(ctx, accent_color());
      graphics_fill_circle(ctx, GPoint(cx, BESPOKE_DOCK_Y), 17);
      glyph = on_accent_color();
    } else {
      glyph = active ? accent_color() : idle_glyph;
    }
    GPoint gc = GPoint(cx, BESPOKE_DOCK_Y);
    switch (item) {
      case NpMoreShuffle: draw_shuffle_icon(ctx, gc, glyph); break;
      case NpMoreRepeat: draw_repeat_icon(ctx, gc, glyph); break;
      // Always filled - black when not favorited, accent (pink by default) when
      // favorited or selected - rather than an outline for the idle state.
      case NpMoreFavorite: draw_heart_icon(ctx, gc, 8, glyph, true); break;
      case NpMoreOutput:
        if (s_phone_audio) {
          draw_phone_icon(ctx, gc, 13, glyph);
        } else {
          draw_watch_icon(ctx, gc, 14, glyph);
        }
        break;
      case NpMoreNewSearch: draw_search_icon(ctx, gc, glyph); break;
      default: draw_queue_icon(ctx, gc, glyph); break;
    }
  }

  draw_text(ctx, "UP/DOWN page    SELECT choose",
            ui_font(FONT_KEY_GOTHIC_14), ui_fg(),
            GRect(8, 204, 184, 18), GTextAlignmentCenter);
}

// Now Playing geometry, in one place. draw_modern_song() draws from this and
// np_touch_handle() hit-tests against it: they used to derive the same numbers
// separately and drifted apart, which left the repeat hit-test 26px from the icon it
// was meant to hit whenever the action bar was showing. Anything positional on this
// screen belongs here.
//
// The layout is two detached cards. The artwork card is deliberately the exact rect
// draw_home_hero() gives the cover - 190x108 at (5,6), its card width by its card
// height less the band - so opening Now Playing from Home leaves the sleeve on the
// same pixels and only the card underneath it changes. The accent card below holds
// everything else: times, title, artist, and the shuffle/loop icons.
typedef struct {
  GRect art;         // artwork card
  GRect rail;        // progress, in the gap between the two cards
  GRect card;        // accent card
  GRect elapsed;     // time chip, artwork's bottom-left corner
  GRect total;       // time chip, artwork's bottom-right corner
  GRect title;
  GRect artist;
  GPoint output;     // phone/watch badge, artwork's top-left corner
  GPoint favorite;   // heart badge, artwork's top-right corner
  GPoint shuffle;
  GPoint loop;
} NowPlayingLayout;

static NowPlayingLayout np_layout(void) {
  NowPlayingLayout l;
  // The artwork takes Home's whole card footprint - the art region *and* the band it
  // sits above - rather than just the art region, so opening Now Playing turns the
  // card you were looking at into pure cover without moving its edges.
  l.art  = GRect(5, 6, 190, 150);
  // 6px, centred in the gap. It was 4px and got lost: unlike Home's rail, which is
  // pinned between the art and the band and reads off both, this one floats with
  // nothing to give it an edge.
  l.rail = GRect(5, 160, 190, 6);
  // Battery-saver hides the rail, and with it the gap the rail needed. Rather than
  // leave a band of bare ground between the two cards, the card reclaims it and grows
  // upward - the artwork keeps its size either way, so the pair still reads as a pair.
  const int card_top = progress_visible() ? 170 : 162;
  l.card = GRect(5, card_top, 190, 222 - card_top);

  // Badges on the artwork's corners, inset clear of the rounded caps. These are the
  // only things drawn over the cover, and each gets a solid disc behind it, so none
  // of them depends on what the artwork happens to look like underneath.
  l.output   = GPoint(l.art.origin.x + 18, l.art.origin.y + 18);
  l.favorite = GPoint(l.art.origin.x + l.art.size.w - 18, l.art.origin.y + 18);
  // Time chips on the bottom corners, in the same badge language.
  const int chip_w = 44, chip_h = 18;
  const int chip_y = l.art.origin.y + l.art.size.h - chip_h - 6;
  l.elapsed = GRect(l.art.origin.x + 6, chip_y, chip_w, chip_h);
  l.total     = GRect(l.art.origin.x + l.art.size.w - chip_w - 6, chip_y, chip_w, chip_h);

  // The toggles are centred on the card, so they occupy a column down its right side
  // rather than a corner - which means the text has to clear them on both lines, not
  // just the artist's. That is what caps the text column at 120px whenever either one
  // is lit.
  const int toggles_y = l.card.origin.y + l.card.size.h / 2;
  l.shuffle = GPoint(154, toggles_y);
  l.loop    = GPoint(178, toggles_y);

  // The card holds nothing but the track, centred in whatever height the card ended up
  // with. Both rects used to be absolute (173 and 199), which happened to centre the
  // pair when the rail was showing and left 11px of dead space above them when it was
  // not - the card grew upward to reclaim the rail's gap, but the text stayed put.
  // Deriving the origin from the card keeps the showing case pixel-identical.
  const int title_h = 26, artist_h = 20;
  const int text_top = l.card.origin.y + (l.card.size.h - (title_h + artist_h)) / 2;
  // With neither toggle lit there is nothing in the right-hand column to clear, so the
  // text takes the whole card - 170px out to the same 10px inset the left edge uses.
  // That is the common case now that an off toggle is not drawn at all, and it is worth
  // the extra 50px: these are song titles, and 120px ellipsized most of them.
  //
  // Either toggle showing pulls it back to 120 rather than to something in between. A
  // width that tracked *which* toggle was lit would make the title reflow when shuffle
  // came on, and a title that changes length when you toggle shuffle reads as a bug.
  const int text_w = (s_shuffle_enabled || s_loop_enabled) ? 120 : 170;
  l.title  = GRect(15, text_top, text_w, title_h);
  l.artist = GRect(15, text_top + title_h, text_w, artist_h);
  return l;
}

// A time readout on the artwork's corner: ground-colored pill, ink text. Same badge
// treatment as the corner glyphs, for the same reason - it has to stay readable over
// a cover whose colors nobody chose. Empty text draws nothing rather than a bare pill.
static void draw_time_chip(GContext *ctx, GRect chip, const char *text) {
  if (!text || !text[0]) return;
  graphics_context_set_fill_color(ctx, ui_bg());
  graphics_fill_rect(ctx, chip, 5, GCornersAll);
  draw_text(ctx, text, ui_font(FONT_KEY_GOTHIC_14_BOLD), ui_fg(),
            GRect(chip.origin.x, chip.origin.y + 1, chip.size.w, chip.size.h),
            GTextAlignmentCenter);
}

// Everything the Now Playing screen has to answer, answered on the artwork.
//
// This used to be two other things: a 72px card that flashed over the middle of the
// screen for skips and toggles, and a second square for volume. Both sat on top of
// the cover, both had to pick an ink that would survive whatever was behind them, and
// once pausing started veiling the artwork there were two competing answers to the
// same press. Now there is one surface. The veil goes down, the answer is drawn at
// the size of the artwork, and it reads the same whatever the cover looks like.
//
// Precedence is deliberate: a transient beats the resting paused state, because the
// transient is the thing that just happened.
static void draw_art_answer(GContext *ctx, GRect art, bool paused, bool on_art) {
  const bool volume = s_show_volume;
  const bool feedback = s_feedback_icon != FeedbackNone &&
                        s_feedback_icon != FeedbackKeyboardHint;
#ifdef PBL_PLATFORM_EMERY
  const bool touch_preview = s_np_touching &&
                             s_np_touch_target != NpTouchTargetNone;
#else
  const bool touch_preview = false;
#endif
  if (!volume && !feedback && !touch_preview && !paused) return;

  if (on_art) draw_dither_scrim(ctx, art);
  // Over a veiled cover white always wins. With no cover the placeholder card is
  // showing instead, and white reads on that too.
  const GColor ink = GColorWhite;
  const GPoint c = GPoint(art.origin.x + art.size.w / 2, art.origin.y + art.size.h / 2);

  if (volume) {
    char pct[8];
    snprintf(pct, sizeof pct, "%d%%", displayed_volume());
    draw_text(ctx, "VOLUME", ui_font(FONT_KEY_GOTHIC_14_BOLD), ink,
              GRect(art.origin.x, c.y - 46, art.size.w, 18), GTextAlignmentCenter);
    draw_text(ctx, pct, ui_font(FONT_KEY_GOTHIC_28_BOLD), ink,
              GRect(art.origin.x, c.y - 28, art.size.w, 36), GTextAlignmentCenter);
    const int w = 120;
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(c.x - w / 2, c.y + 20, w, 8), 4, GCornersAll);
    graphics_context_set_fill_color(ctx, accent_color());
    graphics_fill_rect(ctx, GRect(c.x - w / 2, c.y + 20, w * displayed_volume() / 100, 8),
                       4, GCornersAll);
    return;
  }

  if (feedback) {
    // Every answer sits on the artwork's exact centre, x and y alike, and every one is
    // the same accent disc the resting paused state uses. It was not so: the transports
    // had the disc and the rest were bare glyphs at wildly different scales (a heart 44px
    // across, a watch 34), and all of them were nudged 14px above centre to leave room
    // for the caption. So a screen answering "shuffle on" and the same screen answering
    // "paused" put differently-sized marks in different places. One disc, one position,
    // and only the glyph inside it changes.
    //
    // The glyph scales below are picked to sit inside a radius-20 disc rather than kept
    // from when they were drawn bare, which is why they are roughly half what they were.
    const GColor glyph = draw_art_disc(ctx, c, 20);
    const char *label = "";
    switch (s_feedback_icon) {
      case FeedbackNext:
        draw_skip_glyph(ctx, c, glyph, true, 9);   label = "Next"; break;
      case FeedbackPrev:
        draw_skip_glyph(ctx, c, glyph, false, 9);  label = "Previous"; break;
      case FeedbackFavoriteOn:
        draw_heart_icon(ctx, c, 12, glyph, true);  label = "Favorited"; break;
      case FeedbackFavoriteOff:
        draw_heart_icon(ctx, c, 12, glyph, false); label = "Unfavorited"; break;
      case FeedbackShuffleOn:
        draw_shuffle_icon(ctx, c, glyph);          label = "Shuffle on"; break;
      case FeedbackShuffleOff:
        draw_shuffle_icon(ctx, c, glyph);          label = "Shuffle off"; break;
      case FeedbackOutputPhone:
        draw_phone_icon(ctx, c, 18, glyph);        label = "Phone"; break;
      case FeedbackOutputWatch:
        draw_watch_icon(ctx, c, 20, glyph);        label = "Watch"; break;
      default: break;
    }
    // Clear of the disc (which ends at c.y + 20) and clear of the time chips, which are
    // drawn after this and would otherwise sit on top of the caption.
    draw_text(ctx, label, ui_font(FONT_KEY_GOTHIC_18_BOLD), ink,
              GRect(art.origin.x, c.y + 26, art.size.w, 24), GTextAlignmentCenter);
    return;
  }

  if (touch_preview) {
    const GColor glyph = draw_art_disc(ctx, c, 20);
#ifdef PBL_PLATFORM_EMERY
    switch ((NpTouchTarget) s_np_touch_target) {
      case NpTouchTargetN:
        if (source_is_symfonium()) {
          graphics_context_set_fill_color(ctx, glyph);
          if (paused) {
            draw_transport_triangle(ctx, c, 9, 1);
          } else {
            draw_transport_bars(ctx, c, 9);
          }
        } else {
          if (s_phone_audio) {
            draw_phone_icon(ctx, c, 18, glyph);
          } else {
            draw_watch_icon(ctx, c, 20, glyph);
          }
        }
        break;
      case NpTouchTargetS:
        draw_vinyl_icon(ctx, c, glyph);
        break;
      case NpTouchTargetNW:
        draw_skip_glyph(ctx, c, glyph, false, 9);
        break;
      case NpTouchTargetNE:
        draw_skip_glyph(ctx, c, glyph, true, 9);
        break;
      case NpTouchTargetSW:
        draw_shuffle_icon(ctx, c, glyph);
        break;
      case NpTouchTargetSE:
        draw_repeat_icon(ctx, c, glyph);
        break;
      default:
        break;
    }
#endif
    return;
  }

  draw_transport_button(ctx, c, 20, TransportPlay);
}

// Touch-target overlay shown while touching Now Playing, including artwork-only mode.
// Targets are drawn against the active artwork frame so they stay aligned when the
// artwork expands to full-screen.
static void draw_np_touch_targets(GContext *ctx, GRect frame, bool paused) {
#ifdef PBL_PLATFORM_EMERY
  if (!s_np_touching) return;
  const GPoint p_n  = GPoint(frame.origin.x + frame.size.w / 2, frame.origin.y + 16);
  const GPoint p_s  = GPoint(frame.origin.x + frame.size.w / 2, frame.origin.y + frame.size.h - 16);
  const GPoint p_nw = GPoint(frame.origin.x + 19, frame.origin.y + 20);
  const GPoint p_ne = GPoint(frame.origin.x + frame.size.w - 18, frame.origin.y + 20);
  const GPoint p_sw = GPoint(frame.origin.x + 19, frame.origin.y + frame.size.h - 20);
  const GPoint p_se = GPoint(frame.origin.x + frame.size.w - 18, frame.origin.y + frame.size.h - 20);

  typedef struct {
    NpTouchTarget id;
    GPoint p;
  } OverlayTarget;
  const OverlayTarget targets[] = {
    {NpTouchTargetN,  p_n},
    {NpTouchTargetS,  p_s},
    {NpTouchTargetNW, p_nw},
    {NpTouchTargetNE, p_ne},
    {NpTouchTargetSW, p_sw},
    {NpTouchTargetSE, p_se},
  };

  for (int i = 0; i < (int) ARRAY_LENGTH(targets); i++) {
    const bool active = s_np_touch_target == targets[i].id;
    const int r = active ? 11 : 9;
    const GColor bg = active ? accent_color() : ui_bg();
    const GColor ink = active ? on_accent_color() : accent_color();
    graphics_context_set_fill_color(ctx, bg);
    graphics_fill_circle(ctx, targets[i].p, r);
    switch (targets[i].id) {
      case NpTouchTargetN:
        if (source_is_symfonium()) {
          graphics_context_set_fill_color(ctx, ink);
          if (paused) {
            draw_transport_triangle(ctx, targets[i].p, 5, 1);
          } else {
            draw_transport_bars(ctx, targets[i].p, 5);
          }
        } else if (s_phone_audio) {
          draw_phone_icon(ctx, targets[i].p, 10, ink);
        } else {
          draw_watch_icon(ctx, targets[i].p, 11, ink);
        }
        break;
      case NpTouchTargetS:
        draw_vinyl_icon(ctx, targets[i].p, ink);
        break;
      case NpTouchTargetNW:
        draw_skip_glyph(ctx, targets[i].p, ink, false, 5);
        break;
      case NpTouchTargetNE:
        draw_skip_glyph(ctx, targets[i].p, ink, true, 5);
        break;
      case NpTouchTargetSW:
        draw_shuffle_icon(ctx, targets[i].p, ink);
        break;
      case NpTouchTargetSE:
        draw_repeat_icon(ctx, targets[i].p, ink);
        break;
      default:
        break;
    }
  }
#else
  (void) ctx;
  (void) frame;
  (void) paused;
#endif
}

// One of the accent card's state toggles, drawn only when that toggle is on - the
// caller decides, and an off toggle is simply absent.
//
// It used to draw either way, carrying state by shape: idle was a plain ink glyph,
// enabled filled the ink in behind it and knocked the glyph back out in the card's own
// accent. The card is an accent fill, so there was no third color left to dim an idle
// icon with - Arcade has only two colors in the whole theme - and a full-strength idle
// glyph read as an available button on a card where nothing is tappable. Showing only
// what is on turns the pair from two controls into a status line that says nothing
// when there is nothing to say.
//
// The filled treatment is what survives, so an on toggle looks exactly as it did.
static void draw_card_toggle(GContext *ctx, GPoint center,
                             void (*glyph)(GContext *, GPoint, GColor)) {
  graphics_context_set_fill_color(ctx, on_accent_color());
  // 11, not 12: the two toggles sit 24px apart, so a 12 would leave their filled
  // circles touching and read as one lozenge whenever both are on.
  graphics_fill_circle(ctx, center, 11);
  glyph(ctx, center, accent_color());
}

// Two detached cards: the cover art at exactly its Home-card size, and an accent card
// under it carrying every piece of text and every icon. There is no transport button
// - play state is shown by veiling the artwork and dropping a resume glyph on it, and
// only while paused, so a playing screen is just the cover and the card.
static void draw_modern_song(GContext *ctx, AppScreen state) {
  bool paused = state == ScreenPaused;
  const SearchResult *result = current_playing_result();
  if (!result) return;

  bool on_art = s_cover_art_background && s_cover_art_ready;

  // Artwork-only mode (touchscreen long-press): fill the screen with just the cover
  // art and nothing else. Only honored while art is actually available. This is also
  // the reason the framed layout below never grows to fill the screen - the full
  // bleed view is a gesture away, so the default does not have to try to be both.
  if (s_artwork_only && on_art) {
    draw_cover_art_window(ctx, ART_FRAME, ART_FRAME);
    draw_np_touch_targets(ctx, ART_FRAME, paused);
    if (s_np_touching && s_np_touch_target != NpTouchTargetNone) {
      draw_art_answer(ctx, ART_FRAME, paused, on_art);
    }
    return;
  }

  const NowPlayingLayout l = np_layout();
  const GColor ink = on_accent_color();

  graphics_context_set_fill_color(ctx, ui_bg());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);

  // Artwork card. Nothing is ever drawn over it except the paused veil, so the
  // per-cover luma branch that used to pick black-or-white UI is gone: no text lands
  // on artwork any more, and s_cover_art_dark no longer has anything to decide.
  if (on_art) {
    draw_cover_art_window(ctx, ART_FRAME, l.art);
    stamp_corner_caps(ctx, l.art, ui_bg());
  } else {
    draw_art_placeholder(ctx, l.art, s_cover_art_receiving, GCornersAll);
  }

  // Pause, volume, skips and toggles all answer here, on the artwork. Playing with
  // nothing happening draws none of it, so the resting screen is just the cover.
  draw_art_answer(ctx, l.art, paused, on_art);
  draw_np_touch_targets(ctx, l.art, paused);

  // Corner badges, drawn after the veil so they stay crisp while paused. Each sits on
  // a solid disc of the ground color: the accent alone can land on a cover close
  // enough in tone to swallow it, and these are the only glyphs left that have to
  // survive whatever the artwork happens to be.
  //
  // The output badge goes away under Symfonium, for the same reason its Settings row
  // and its More popup entry do: that source always plays through Symfonium's own
  // player on the phone, so the badge could only ever report "Phone" and would read as
  // a control for a route that cannot be changed.
  if (!source_is_symfonium()) {
    graphics_context_set_fill_color(ctx, ui_bg());
    graphics_fill_circle(ctx, l.output, 13);
    if (s_phone_audio) {
      draw_phone_icon(ctx, l.output, 13, accent_color());
    } else {
      draw_watch_icon(ctx, l.output, 14, accent_color());
    }
  }
  if (s_current_favorite) {
    graphics_context_set_fill_color(ctx, ui_bg());
    graphics_fill_circle(ctx, l.favorite, 13);
    draw_heart_icon(ctx, l.favorite, 9, accent_color(), true);
  }

  // Progress lives in the gap between the two cards, so it reads as belonging to the
  // artwork rather than to the text. Under the bespoke UI the volume gets the shared
  // popup card like every other transient, which leaves the rail showing progress.
  bool volume_on_rail = s_show_volume && !s_bespoke_ui;
  uint32_t elapsed = s_elapsed_seconds > s_duration_seconds ? s_duration_seconds
                                                            : s_elapsed_seconds;
  if (progress_visible()) {
    int filled_width = 0;
    if (volume_on_rail) {
      filled_width = l.rail.size.w * displayed_volume() / 100;
    } else if (s_duration_seconds > 0) {
      filled_width = l.rail.size.w * elapsed / s_duration_seconds;
    }
    // Black track rather than the dim ink: the rail floats on the ground with the
    // accent running along it, and light gray on a deep ground put too little between
    // the played and unplayed halves to read at 6px.
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, l.rail, 2, GCornersAll);
    graphics_context_set_fill_color(ctx, accent_color());
    graphics_fill_rect(ctx, GRect(l.rail.origin.x, l.rail.origin.y, filled_width,
                                  l.rail.size.h), 2, GCornersAll);

    // Elapsed and total, as chips on the artwork's bottom corners - position within the
    // track, and how long the track is. They are chips rather than bare text because this
    // is the one place numbers land on the cover.
    //
    // Elapsed rather than remaining: the left chip used to count down from the end, which
    // reads as a timer rather than as a position, and it disagreed with the progress rail
    // directly above it - the rail fills left to right while the number counted the other
    // way. Both now describe the same thing.
    char left[16];
    char right[16];
    if (volume_on_rail) {
      snprintf(left, sizeof(left), "VOL");
      snprintf(right, sizeof(right), "%d%%", displayed_volume());
    } else if (s_duration_seconds > 0) {
      format_time(elapsed, left, sizeof(left));
      format_time(s_duration_seconds, right, sizeof(right));
    } else {
      // Length unknown (livestreams, and every track before the first position
      // event): counting up is the only honest thing to show.
      format_time(s_elapsed_seconds, left, sizeof(left));
      right[0] = '\0';
    }
    draw_time_chip(ctx, l.elapsed, left);
    draw_time_chip(ctx, l.total, right);
  }

  // The accent card. Everything from here down is inside it, in on-accent ink.
  graphics_context_set_fill_color(ctx, accent_color());
  graphics_fill_rect(ctx, l.card, 6, GCornersAll);

  draw_text(ctx, result->title, ui_font(FONT_KEY_GOTHIC_24_BOLD), ink,
            l.title, GTextAlignmentLeft);
  draw_text(ctx, result->artist, ui_font(FONT_KEY_GOTHIC_18), ink,
            l.artist, GTextAlignmentLeft);

  // Shuffle and loop, right-hand side of the card - each drawn only while it is on, so
  // the usual case (both off) leaves the card as just the track. They keep their fixed
  // slots rather than collapsing toward the edge: a lone toggle that moves depending on
  // which one is lit is harder to read at a glance than one that is always in the same
  // place, and the text column is capped at 120px for the both-on case regardless.
  if (s_shuffle_enabled) draw_card_toggle(ctx, l.shuffle, draw_shuffle_icon);
  if (s_loop_enabled) {
    draw_card_toggle(ctx, l.loop, draw_repeat_icon);
    // Which loop mode, as a badge on the toggle's upper-right shoulder. Below it
    // instead would hang off the card's bottom edge, which the card no longer has the
    // room to absorb.
    const GPoint badge = GPoint(l.loop.x + 9, l.loop.y - 10);
    graphics_context_set_fill_color(ctx, ink);
    graphics_fill_circle(ctx, badge, 5);
    draw_text(ctx, s_loop_mode == LoopModeAll ? "A" : "1",
              ui_font(FONT_KEY_GOTHIC_14_BOLD), accent_color(),
              GRect(badge.x - 5, badge.y - 9, 10, 14), GTextAlignmentCenter);
  }

  // Volume and every confirmation were drawn here, as cards over the middle of the
  // screen. They are on the artwork now - see draw_art_answer() - so nothing is left
  // to stack on top of the layout but the More popup.
  if (s_action_bar_visible) draw_action_bar(ctx, paused);
  if (s_np_more_open) draw_np_more(ctx);
}

static void draw_song(GContext *ctx, AppScreen state) {
  draw_modern_song(ctx, state);
}

static void draw_error(GContext *ctx) {
  graphics_context_set_fill_color(ctx, ui_bg());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);
  // Chromeless, but failure keeps its accent as a thin eyebrow band so an error can
  // never be mistaken for an empty list.
  graphics_context_set_fill_color(ctx, accent_color());
  graphics_fill_rect(ctx, GRect(0, 0, 200, 40), 0, GCornerNone);
  draw_text(ctx, "SOMETHING WENT WRONG", ui_font(FONT_KEY_GOTHIC_14_BOLD),
            on_accent_color(), GRect(18, 18, 164, 18), GTextAlignmentLeft);
  graphics_context_set_text_color(ctx, ui_fg());
  graphics_draw_text(ctx, s_status, ui_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(18, 92, 164, 44), GTextOverflowModeWordWrap,
                     GTextAlignmentCenter, NULL);
  draw_text(ctx, s_result_count > 0 ? "SELECT retry    BACK close"
                                    : "SELECT search    BACK close",
            ui_font(FONT_KEY_GOTHIC_14), ui_fg(),
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
        // LIBRARY_ITEMS is indexed by id and rows are filtered per source, so the raw
        // array cannot be handed over with a filtered count - collapse it to rows first.
        const char *rows[ARRAY_LENGTH(LIBRARY_ITEMS)];
        const int row_count = library_item_count();
        for (int i = 0; i < row_count; i++) rows[i] = LIBRARY_ITEMS[library_item_id(i)];
        draw_native_menu(ctx, "LIBRARY", rows, row_count);
      }
      break;
    case ScreenLibraryItems: draw_library_items(ctx); break;
    case ScreenQueue: draw_queue(ctx); break;
    case ScreenMenu:
      if (s_bespoke_ui) {
        bespoke_ground(ctx, "MUSIC-PBL");
        int offset = scroll_list_layout(BESPOKE_ROW_PITCH, ARRAY_LENGTH(MENU_ITEMS),
                                        s_menu_selection, BESPOKE_LIST_TOP,
                                        BESPOKE_VIEWPORT_H, true);
        for (int i = 0; i < (int) ARRAY_LENGTH(MENU_ITEMS); i++) {
          bespoke_row1(ctx, BESPOKE_LIST_TOP + i * BESPOKE_ROW_PITCH - offset,
                       BESPOKE_ROW_H, MENU_ITEMS[i], NULL, i == s_menu_selection);
        }
        bespoke_frame(ctx, "MUSIC-PBL", "SELECT open    BACK home");
        bespoke_scrollbar(ctx, offset);
      } else {
        draw_native_menu(ctx, "MUSIC-PBL", MENU_ITEMS, ARRAY_LENGTH(MENU_ITEMS));
      }
      break;
    case ScreenSettings: draw_settings(ctx); break;
    case ScreenAdvanced: draw_advanced(ctx); break;
    case ScreenAbout: draw_about(ctx); break;
    case ScreenWatch: draw_watch_screen(ctx); break;
    case ScreenBridge: draw_bridge_screen(ctx); break;
    case ScreenAcks: draw_acks_screen(ctx); break;
    case ScreenWhatsNew: draw_whats_new(ctx); break;
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
  free(s_pcm_buffer);
  s_pcm_buffer = NULL;
  stop_progress_timer();
}

static bool start_audio(void) {
  stop_audio();
  s_expected_sequence = 0;
  s_pcm_buffer = malloc(ADPCM_PCM_BYTES);
  if (!s_pcm_buffer) return false;
  s_stream_open = speaker_stream_open(SpeakerPcmFormat_16kHz_16bit, s_watch_volume);
  if (!s_stream_open) {
    free(s_pcm_buffer);
    s_pcm_buffer = NULL;
  }
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
  if (size != ADPCM_BLOCK_SIZE || !s_pcm_buffer) return false;
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
  const uint32_t pcm_size = ADPCM_PCM_BYTES;
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
  DW_TRACE("[ClaySync] Sending settings route=%d watchVol=%d phoneVol=%d input=%d progress=%d cacheEn=%d cacheMB=%d coverArt=%d watchQ=%d phoneQ=%d cacheRadio=%d",
          s_phone_audio ? 1 : 0,
          s_watch_volume,
          s_phone_volume,
          s_input_mode,
          s_progress_mode,
          s_cache_enabled ? 1 : 0,
          s_cache_size_mb,
          s_cover_art_background ? 1 : 0,
          s_watch_audio_quality ? 1 : 0,
          s_phone_audio_quality ? 1 : 0,
          s_cache_radio ? 1 : 0);
  DW_TRACE("[ClaySync] autoShuffle=%d", s_symfonium_auto_shuffle ? 1 : 0);
  dict_write_int32(iterator, MESSAGE_KEY_COMMAND, CommandSyncSettings);
  dict_write_int32(iterator, MESSAGE_KEY_CONFIG_AUDIO_ROUTE, s_phone_audio ? 1 : 0);
  dict_write_int32(iterator, MESSAGE_KEY_CONFIG_WATCH_VOLUME, s_watch_volume);
  dict_write_int32(iterator, MESSAGE_KEY_CONFIG_PHONE_VOLUME, s_phone_volume);
  dict_write_int32(iterator, MESSAGE_KEY_CONFIG_INPUT_MODE, s_input_mode);
  dict_write_int32(iterator, MSG_CONFIG_SHOW_PROGRESS, s_progress_mode);
  dict_write_int32(iterator, MSG_CONFIG_CACHE_ENABLED, s_cache_enabled ? 1 : 0);
  dict_write_int32(iterator, MSG_CONFIG_CACHE_SIZE_MB, s_cache_size_mb);
  dict_write_int32(iterator, MSG_CONFIG_COVER_ART_BG, s_cover_art_background ? 1 : 0);
  dict_write_int32(iterator, MSG_THEME, s_theme);
  dict_write_int32(iterator, MSG_CONFIG_WATCH_AUDIO_QUALITY, s_watch_audio_quality ? 1 : 0);
  dict_write_int32(iterator, MSG_CONFIG_PHONE_AUDIO_QUALITY, s_phone_audio_quality ? 1 : 0);
  dict_write_int32(iterator, MSG_CONFIG_CACHE_RADIO, s_cache_radio ? 1 : 0);
  dict_write_int32(iterator, MSG_ROUTE_EPOCH, s_route_epoch);
  dict_write_int32(iterator, MSG_CONFIG_MUSIC_SOURCE, s_music_source);
  dict_write_int32(iterator, MSG_SOURCE_EPOCH, s_source_epoch);
  dict_write_int32(iterator, MSG_CONFIG_SYMFONIUM_AUTO_SHUFFLE,
                   s_symfonium_auto_shuffle ? 1 : 0);
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

/**
 * Applies a music-source change and re-handshakes, which is what makes the incoming
 * backend repopulate the screen - nothing here has to fetch anything itself.
 *
 * `local` separates a change made on this watch, which owns the epoch bump, from one
 * pushed by the phone, which arrives carrying an epoch already.
 */
static void apply_music_source(int source, bool local) {
  if (source != MusicSourceYouTube && source != MusicSourceSymfonium) return;
  bool changed = source != s_music_source;
  s_music_source = source;
  persist_write_int(MUSIC_SOURCE_KEY, s_music_source);
  if (local) {
    s_source_epoch++;
    persist_write_int(SOURCE_EPOCH_KEY, s_source_epoch);
  }
  if (!changed) return;
  DW_TRACE("[Source] now %s (epoch %d, %s)",
          source_is_symfonium() ? "Symfonium" : "YouTube", (int) s_source_epoch,
          local ? "local" : "from phone");
  // Nothing the old backend gave us survives the switch: its track ids mean nothing to
  // the new one, so holding on to them would only produce "song no longer available".
  s_has_now_playing = false;
  span_reset();
  clear_cover_art();
  if (source_is_symfonium() && !s_phone_audio) {
    // Symfonium plays through its own player - there is no watch-speaker route to switch
    // back to. Assert phone audio with a fresh epoch so the companion's snapshots and this
    // watch agree, instead of the watch sitting on the Watch route waiting for a stream
    // that never comes.
    s_phone_audio = true;
    persist_write_int(AUDIO_ROUTE_KEY, s_phone_audio);
    s_route_epoch++;
    persist_write_int(ROUTE_EPOCH_KEY, s_route_epoch);
  }
  send_settings_sync();
  send_hello();
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

static bool request_queue(void) {
  DictionaryIterator *iterator;
  if (app_message_outbox_begin(&iterator) != APP_MSG_OK) return false;
  dict_write_int32(iterator, MESSAGE_KEY_COMMAND, CommandRequestQueue);
  dict_write_end(iterator);
  return app_message_outbox_send() == APP_MSG_OK;
}

static void open_queue(void) {
  // span_reset(), not just a count of zero: a paged search leaves a non-zero window base
  // behind, and the Queue indexes s_results directly from row 0.
  span_reset();
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

// ---------------------------------------------------------------------------
// T9 keyboard input
//
// Multi-tap, as a phone did it, and by finger only: tapping a key types its first
// character, and each further tap of the same key inside T9_COMMIT_MS replaces that
// character with the next one on the key. Anything else - the timer expiring, tapping
// a different key, changing mode, deleting, searching - commits what is showing, so a
// tap can never be read as belonging to the character before it.
//
// The buttons are the grid keyboard's, unchanged: UP cycles abc/ABC/123, SELECT runs
// the search, DOWN deletes. Nothing is typed with them.
// ---------------------------------------------------------------------------

static const char *t9_cell(int cell) {
  return s_keyboard_mode == 2 ? T9_SYMBOLS[cell] : T9_LETTERS[cell];
}

static char t9_character(int cell, int index) {
  char character = t9_cell(cell)[index];
  if (s_keyboard_mode == 1 && character >= 'a' && character <= 'z') {
    character -= 'a' - 'A';
  }
  return character;
}

static void t9_commit(void) {
  if (s_t9_timer) {
    app_timer_cancel(s_t9_timer);
    s_t9_timer = NULL;
  }
  s_t9_pending = -1;
}

// The character stands as typed; only the highlight goes, so the key stops
// advertising a cycle that is no longer live.
static void t9_commit_timer_cb(void *context) {
  (void) context;
  s_t9_timer = NULL;
  s_t9_pending = -1;
  layer_mark_dirty(s_canvas);
}

static void t9_tap(int cell) {
  if (cell < 0 || cell > 8) return;
  if (cell == s_t9_pending && s_query_length > 0) {
    s_t9_index = (s_t9_index + 1) % strlen(t9_cell(cell));
    s_query[s_query_length - 1] = t9_character(cell, s_t9_index);
  } else {
    t9_commit();
    if (s_query_length >= TEXT_LENGTH - 1) {
      vibes_double_pulse();
      return;
    }
    s_t9_index = 0;
    s_query[s_query_length++] = t9_character(cell, 0);
    s_query[s_query_length] = '\0';
    s_t9_pending = (int8_t) cell;
  }
  if (s_t9_timer) app_timer_cancel(s_t9_timer);
  s_t9_timer = app_timer_register(T9_COMMIT_MS, t9_commit_timer_cb, NULL);
  vibes_short_pulse();
  layer_mark_dirty(s_canvas);
}

static void open_keyboard(void) {
  s_query[0] = '\0';
  s_query_length = 0;
  s_keyboard_mode = 0;
  t9_commit();
  nav_push(ScreenKeyboard);
  layer_mark_dirty(s_canvas);
}

// Deep results are a Symfonium feature: the companion widens a capped search by walking
// the albums it matched, and serves the result a page at a time. The YouTube backend has
// no offset to honour.
//
// Bespoke UI only, and not as a style preference: the stock path hands the list to a
// MenuLayer, whose selection is a row index that sync_native_menu() clamps against the
// rows it holds. A paged list's selection is an index into the whole list, so that clamp
// would quietly drag it back inside the window on every sync.
static bool search_is_deep(void) {
  return s_search_limit == SEARCH_LIMIT_DEEP && source_is_symfonium() && s_bespoke_ui;
}

// The count to ask for when not paging. Guards the case where Deep was chosen under
// Symfonium and then the source or the UI style changed underneath it - the stored
// sentinel is 0, and asking the phone for zero results would return nothing at all.
static int search_fixed_limit(void) {
  return s_search_limit == SEARCH_LIMIT_DEEP ? 10 : s_search_limit;
}

static void span_stall_cb(void *context) {
  s_span_timer = NULL;
  if (s_span_pending < 0) return;
  APP_LOG(APP_LOG_LEVEL_WARNING, "[Span] page %d never arrived; releasing", s_span_pending);
  // Released rather than retried: the next selection move asks again by itself, and a
  // retry loop against a phone that is not answering only burns battery.
  s_span_pending = -1;
  layer_mark_dirty(s_canvas);
}

/**
 * Asks for the page of results starting at `offset`, keeping the current request id so
 * the reply is recognised as belonging to this search rather than a new one - which is
 * also what lets the phone serve every page from one stable ranking.
 *
 * One page may be in flight at a time. Without that, holding Down queues a request per
 * row and the replies interleave into a window assembled out of order.
 */
static bool request_search_page(int offset) {
  if (s_span_pending >= 0 || s_query_length == 0) return false;
  DictionaryIterator *iterator;
  if (app_message_outbox_begin(&iterator) != APP_MSG_OK) return false;
  dict_write_int32(iterator, MESSAGE_KEY_COMMAND, CommandSearch);
  dict_write_cstring(iterator, MESSAGE_KEY_QUERY, s_query);
  dict_write_int32(iterator, MESSAGE_KEY_SEARCH_REQUEST_ID, s_search_request_id);
  dict_write_int32(iterator, MESSAGE_KEY_SEARCH_LIMIT, SPAN_PAGE);
  dict_write_int32(iterator, MSG_SEARCH_MODE, s_search_mode);
  dict_write_int32(iterator, MSG_SEARCH_OFFSET, offset);
  dict_write_end(iterator);
  if (app_message_outbox_send() != APP_MSG_OK) return false;
  s_span_pending = offset;
  if (s_span_timer) app_timer_cancel(s_span_timer);
  s_span_timer = app_timer_register(SPAN_STALL_MS, span_stall_cb, NULL);
  DW_TRACE("[Span] requested %d..%d of %d (base=%d have=%d)",
          offset, offset + SPAN_PAGE - 1, s_span_total, s_span_base, s_result_count);
  return true;
}

/**
 * Which library lists are read through the sliding window instead of being held whole.
 *
 * Favorites is the one with no natural ceiling - a few thousand favourited tracks is an
 * ordinary library, and MAX_LIBRARY quietly truncated it to 60 rows with nothing on
 * screen to say so. The others are either bounded by a user-facing setting (Recently
 * Played, Recent Searches) or short by nature, and paging a list that already fits buys
 * nothing but round trips.
 *
 * Bespoke only, and Symfonium only. Both restrictions are for the same reason: a paged
 * list that the other end will not page shows one page and presents it as the whole
 * thing, which is worse than the honest cap it replaces. The stock MenuLayer path builds
 * its rows straight from s_result_count with no notion of a window, and the YouTube
 * companion answers a library request with one whole list and no total.
 */
static bool library_is_paged(int library_type) {
  return s_bespoke_ui && source_is_symfonium() && library_type == LibraryFavorites;
}

/**
 * Asks for the page of library rows starting at `offset`. The unpaged types keep exactly
 * the request they always sent - one call, one whole list, no offset on the wire - so a
 * companion that has never heard of library paging sees no change from them.
 */
static bool request_library_page(int library_type, int offset) {
  const bool paged = library_is_paged(library_type);
  if (paged && s_span_pending >= 0) return false;
  DictionaryIterator *iterator;
  if (app_message_outbox_begin(&iterator) != APP_MSG_OK) return false;
  dict_write_int32(iterator, MESSAGE_KEY_COMMAND, CommandRequestLibrary);
  dict_write_int32(iterator, MESSAGE_KEY_LIBRARY_TYPE, library_type);
  // Recently Played and Recent Searches are capped by their user-configurable display
  // limits. A paged type asks for one page; the rest send the full-array cap so the
  // companion returns everything available.
  int32_t limit = paged ? SPAN_PAGE :
                  library_type == LibraryRecent ? s_history_limit :
                  library_type == LibraryRecentSearches ? s_recent_search_limit : MAX_LIBRARY;
  dict_write_int32(iterator, MESSAGE_KEY_LIBRARY_LIMIT, limit);
  if (paged) dict_write_int32(iterator, MSG_LIBRARY_OFFSET, offset);
  dict_write_end(iterator);
  if (app_message_outbox_send() != APP_MSG_OK) return false;
  if (paged) {
    s_span_pending = offset;
    if (s_span_timer) app_timer_cancel(s_span_timer);
    s_span_timer = app_timer_register(SPAN_STALL_MS, span_stall_cb, NULL);
    DW_TRACE("[Span] library %d requested %d..%d of %d (base=%d have=%d)", library_type,
             offset, offset + SPAN_PAGE - 1, s_span_total, s_span_base, s_result_count);
  }
  return true;
}

static bool request_library(int library_type) {
  return request_library_page(library_type, 0);
}

/**
 * Fetches the page beyond whichever edge the selection is approaching, if there is one.
 * Called after every selection move on a paged list.
 *
 * Search and Library both land here, so which request to send is decided by the screen
 * rather than by the caller - a paged list is a paged list, and the two differ only in
 * the command that refills it.
 */
static void span_page_request(int offset) {
  if (s_screen == ScreenLibraryItems) request_library_page(s_library_type, offset);
  else request_search_page(offset);
}

static void span_maybe_prefetch(void) {
  if (!s_span_paged || s_span_pending >= 0) return;
  const int first = s_span_base;
  const int last = s_span_base + s_result_count - 1;
  if (s_selected_result >= last - SPAN_PREFETCH && last + 1 < span_total()) {
    span_page_request(last + 1);
  } else if (s_selected_result <= first + SPAN_PREFETCH && first > 0) {
    int offset = first - SPAN_PAGE;
    if (offset < 0) offset = 0;
    span_page_request(offset);
  }
}

/**
 * Moves the selection on a song list by one row.
 *
 * An unpaged list keeps the wrap-around it has always had - the whole list is in front of
 * you, so wrapping is a shortcut rather than a way to walk off the end. A paged list walks
 * the full length instead and pulls pages in as it nears an edge, and deliberately stops
 * at the last resident row rather than running past it: the row beyond has nothing to
 * draw yet, and letting the highlight sit on it makes the list look broken for as long as
 * the page takes.
 */
static void results_move(int delta) {
  if (s_result_count <= 0) return;
  if (!s_span_paged) {
    s_selected_result = (s_selected_result + s_result_count + delta) % s_result_count;
    return;
  }
  int next = s_selected_result + delta;
  const int total = span_total();
  if (next >= total) next = total - 1;
  const int first = s_span_base;
  const int last = s_span_base + s_result_count - 1;
  if (next < first) next = first;
  if (next > last) next = last;
  if (next < 0) next = 0;
  s_selected_result = next;
  span_maybe_prefetch();
}

static bool submit_search_query(void) {
  if (!s_bridge_ready || s_query_length == 0) return false;
  span_reset();
  s_selected_result = 0;
  s_search_active = true;
  s_search_request_id++;
  s_span_paged = search_is_deep();
  DictionaryIterator *iterator;
  if (app_message_outbox_begin(&iterator) != APP_MSG_OK) {
    s_search_active = false;
    s_span_paged = false;
    return false;
  }
  dict_write_int32(iterator, MESSAGE_KEY_COMMAND, CommandSearch);
  dict_write_cstring(iterator, MESSAGE_KEY_QUERY, s_query);
  dict_write_int32(iterator, MESSAGE_KEY_SEARCH_REQUEST_ID, s_search_request_id);
  dict_write_int32(iterator, MESSAGE_KEY_SEARCH_LIMIT,
                   s_span_paged ? SPAN_PAGE : search_fixed_limit());
  dict_write_int32(iterator, MSG_SEARCH_MODE, s_search_mode);
  if (s_span_paged) dict_write_int32(iterator, MSG_SEARCH_OFFSET, 0);
  dict_write_end(iterator);
  if (app_message_outbox_send() != APP_MSG_OK) {
    s_search_active = false;
    s_span_paged = false;
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

// Deletes one character, whichever keyboard is up. A pending T9 cycle is committed
// first, so this always removes the character on screen rather than half-cancelling a
// cycle. Returns false when there is nothing left to delete - BACK's cue to leave.
static bool keyboard_back_level(void) {
  t9_commit();
  if (s_query_length == 0) return false;
  s_query[--s_query_length] = '\0';
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
// The peek expiring. Only ever set while the rail is switched off, so there is nothing
// to restore - dropping the flag puts the card back where it was.
static void progress_peek_timer_cb(void *context) {
  (void) context;
  s_progress_peek_timer = NULL;
  s_progress_peek = false;
  layer_mark_dirty(s_canvas);
}

// Show the rail for a few seconds. Flicking again restarts the clock rather than
// stacking timers, so holding a conversation with the screen keeps it up.
static void progress_peek_start(void) {
  if (s_progress_mode != ProgressFlick) return;
  if (s_progress_peek_timer) app_timer_cancel(s_progress_peek_timer);
  s_progress_peek = true;
  s_progress_peek_timer = app_timer_register(PROGRESS_PEEK_MS, progress_peek_timer_cb, NULL);
  layer_mark_dirty(s_canvas);
}

// A flick of the wrist, as reported by the tap service. Which axis and direction is not
// interesting - any deliberate jolt means "show me where I am".
static void np_accel_tap_handler(AccelAxisType axis, int32_t direction) {
  (void) axis;
  (void) direction;
  progress_peek_start();
}

// The accelerometer is only worth running when a flick could actually do something, so
// this follows both the setting and the screen. The tap service is global rather than
// per-window, so nothing else scopes it for us.
static void sync_accel_subscription(void) {
  const bool want = s_progress_mode == ProgressFlick &&
                    (s_screen == ScreenPlaying || s_screen == ScreenPaused);
  if (want == s_accel_subscribed) return;
  if (want) {
    accel_tap_service_subscribe(np_accel_tap_handler);
  } else {
    accel_tap_service_unsubscribe();
  }
  s_accel_subscribed = want;
}

#define NP_TOUCH_DEADZONE_R 16
#define NP_TOUCH_ACTION_MIN_DIST 18

static bool np_touch_in_deadzone(int16_t x, int16_t y) {
  // Touch targets follow the active artwork surface: the normal artwork card, or full
  // screen while artwork-only is up.
  const GRect frame = (s_artwork_only && s_cover_art_background && s_cover_art_ready)
                    ? ART_FRAME : GRect(5, 6, 190, 150);
  const int16_t cx = frame.origin.x + frame.size.w / 2;
  const int16_t cy = frame.origin.y + frame.size.h / 2;
  int32_t dx = x - cx;
  int32_t dy = y - cy;
  return dx * dx + dy * dy <= NP_TOUCH_DEADZONE_R * NP_TOUCH_DEADZONE_R;
}

static int8_t np_touch_target_at(int16_t x, int16_t y) {
  typedef struct { int16_t x, y; int8_t id; } TouchTarget;
  const GRect frame = (s_artwork_only && s_cover_art_background && s_cover_art_ready)
                    ? ART_FRAME : GRect(5, 6, 190, 150);
  const int16_t mid_x = frame.origin.x + frame.size.w / 2;
  const int16_t top_y = frame.origin.y + 16;
  const int16_t bot_y = frame.origin.y + frame.size.h - 16;
  const int16_t ul_x = frame.origin.x + 19;
  const int16_t ur_x = frame.origin.x + frame.size.w - 18;
  const int16_t u_y = frame.origin.y + 20;
  const int16_t d_y = frame.origin.y + frame.size.h - 20;
  static const TouchTarget targets[] = {
    {0, 0, NpTouchTargetS},
    {0, 0, NpTouchTargetNW},
    {0, 0, NpTouchTargetNE},
    {0, 0, NpTouchTargetSW},
    {0, 0, NpTouchTargetSE},
  };
  TouchTarget dynamic[ARRAY_LENGTH(targets)];
  memcpy(dynamic, targets, sizeof(dynamic));
  dynamic[0].x = mid_x; dynamic[0].y = bot_y;
  dynamic[1].x = ul_x;  dynamic[1].y = u_y;
  dynamic[2].x = ur_x;  dynamic[2].y = u_y;
  dynamic[3].x = ul_x;  dynamic[3].y = d_y;
  dynamic[4].x = ur_x;  dynamic[4].y = d_y;
  const int32_t r2 = 16 * 16;
  int32_t dxn = x - mid_x;
  int32_t dyn = y - top_y;
  if (dxn * dxn + dyn * dyn <= r2) return NpTouchTargetN;
  for (int i = 0; i < (int) ARRAY_LENGTH(dynamic); i++) {
    int32_t dx = x - dynamic[i].x;
    int32_t dy = y - dynamic[i].y;
    if (dx * dx + dy * dy <= r2) return dynamic[i].id;
  }
  return NpTouchTargetNone;
}

static int8_t np_touch_target_for_drag(int16_t x, int16_t y, int16_t sx, int16_t sy) {
  if (np_touch_in_deadzone(x, y)) return NpTouchTargetNone;
  int8_t target = np_touch_target_at(x, y);
  if (target != NpTouchTargetNone) return target;
  int16_t dx = x - sx;
  int16_t dy = y - sy;
  int32_t dist2 = (int32_t) dx * dx + (int32_t) dy * dy;
  if (dist2 < NP_TOUCH_ACTION_MIN_DIST * NP_TOUCH_ACTION_MIN_DIST) {
    return NpTouchTargetNone;
  }
  int16_t adx = dx < 0 ? -dx : dx;
  if (dy > 12 && adx * 2 <= dy) return NpTouchTargetS;
  if (dy < -12 && adx * 2 <= -dy) return NpTouchTargetN;
  if (dx < 0 && dy < 0) return NpTouchTargetNW;
  if (dx > 0 && dy < 0) return NpTouchTargetNE;
  if (dx < 0 && dy > 0) return NpTouchTargetSW;
  if (dx > 0 && dy > 0) return NpTouchTargetSE;
  return NpTouchTargetNone;
}

// Handles touch on the now-playing screen. Drags away from the center deadzone arm
// one of six touch targets: top (output, or play/pause on Symfonium), bottom
// (artwork-only), upper-left (previous), upper-right (next), lower-left (shuffle),
// lower-right (loop).
static void np_touch_handle(const TouchEvent *event) {
  switch (event->type) {
    case TouchEvent_Touchdown:
      s_np_touching = true;
      s_np_touch_moved = false;
      s_np_touch_x = event->x;
      s_np_touch_y = event->y;
      s_np_touch_target = np_touch_target_for_drag(event->x, event->y,
                                                   s_np_touch_x, s_np_touch_y);
      layer_mark_dirty(s_canvas);
      break;
    case TouchEvent_PositionUpdate: {
      int16_t dx = event->x - s_np_touch_x;
      int16_t dy = event->y - s_np_touch_y;
      if (dx * dx + dy * dy > 20 * 20) s_np_touch_moved = true;
      int8_t target = np_touch_target_for_drag(event->x, event->y,
                                               s_np_touch_x, s_np_touch_y);
      if (target != s_np_touch_target) {
        s_np_touch_target = target;
        layer_mark_dirty(s_canvas);
      }
      break;
    }
    case TouchEvent_Liftoff:
      {
        int16_t dx = event->x - s_np_touch_x;
        int16_t dy = event->y - s_np_touch_y;
        int32_t dist2 = (int32_t) dx * dx + (int32_t) dy * dy;
        int8_t target = np_touch_target_for_drag(event->x, event->y,
                                                 s_np_touch_x, s_np_touch_y);
        if (!np_touch_in_deadzone(event->x, event->y) &&
            dist2 >= NP_TOUCH_ACTION_MIN_DIST * NP_TOUCH_ACTION_MIN_DIST) {
          switch ((NpTouchTarget) target) {
            case NpTouchTargetN:
              if (source_is_symfonium()) {
                np_toggle_play_pause();
              } else {
                np_toggle_output();
              }
              break;
            case NpTouchTargetS:
              if (s_cover_art_background && s_cover_art_ready) {
                s_artwork_only = !s_artwork_only;
                vibes_short_pulse();
                layer_mark_dirty(s_canvas);
              }
              break;
            case NpTouchTargetNW:
              np_previous();
              break;
            case NpTouchTargetNE:
              np_next();
              break;
            case NpTouchTargetSW:
              np_toggle_shuffle();
              break;
            case NpTouchTargetSE:
              np_cycle_loop();
              break;
            default:
              break;
          }
        }
      }
      s_np_touching = false;
      s_np_touch_moved = false;
      s_np_touch_target = NpTouchTargetNone;
      layer_mark_dirty(s_canvas);
      break;
  }
}

static void t9_cancel_hold(void) {
  if (s_t9_hold_timer) {
    app_timer_cancel(s_t9_hold_timer);
    s_t9_hold_timer = NULL;
  }
}

// A key held still for T9_HOLD_MS types its number, the way a phone did. The number is
// the key's position, so no table says which is which - cell 0 is the 1 key.
static void t9_hold_timer_cb(void *context) {
  (void) context;
  s_t9_hold_timer = NULL;
  if (!s_touch_active || s_touch_origin_key < 0) return;
  t9_commit();
  if (s_query_length >= TEXT_LENGTH - 1) {
    vibes_double_pulse();
    return;
  }
  s_query[s_query_length++] = (char) ('1' + s_touch_origin_key);
  s_query[s_query_length] = '\0';
  // The press is spent: liftoff must not also type the key's first letter.
  s_t9_hold_fired = true;
  vibes_double_pulse();
  layer_mark_dirty(s_canvas);
}

// T9 by finger, which is the only way it types. Nothing here is a swipe: every
// character on a T9 key is reachable by tapping the key, which is the whole point of it
// next to the grid, so a drag off the key it started on can only ever mean a tap the
// user changed their mind about.
static void t9_touch_handle(const TouchEvent *event) {
  switch (event->type) {
    case TouchEvent_Touchdown: {
      int key = pt2_touch_key_at(event->x, event->y);
      if (key < 0) return;
      s_touch_active = true;
      s_touch_origin_key = (int8_t) key;
      s_touch_start_x = event->x;
      s_touch_start_y = event->y;
      s_t9_hold_fired = false;
      t9_cancel_hold();
      // Only the letter screens: those are the keys with a number printed on them, and
      // the symbol screen would have the 8 key disagreeing with the 0 it types.
      if (s_keyboard_mode != 2) {
        s_t9_hold_timer = app_timer_register(T9_HOLD_MS, t9_hold_timer_cb, NULL);
      }
      vibes_short_pulse();
      break;
    }
    case TouchEvent_PositionUpdate: {
      if (!s_touch_active) break;
      int16_t dx = event->x - s_touch_start_x;
      int16_t dy = event->y - s_touch_start_y;
      if ((int32_t) dx * dx + (int32_t) dy * dy >= PT2_SWIPE_MIN_DIST_SQ &&
          pt2_touch_key_at(event->x, event->y) != s_touch_origin_key) {
        t9_cancel_hold();
        s_touch_active = false;
        s_touch_origin_key = -1;
      }
      break;
    }
    case TouchEvent_Liftoff:
      t9_cancel_hold();
      if (s_touch_active && !s_t9_hold_fired &&
          pt2_touch_key_at(event->x, event->y) == s_touch_origin_key) {
        t9_tap(s_touch_origin_key);
      }
      s_t9_hold_fired = false;
      s_touch_active = false;
      s_touch_origin_key = -1;
      break;
  }
}

static void pt2_touch_handler(const TouchEvent *event, void *context) {
  (void) context;
  if (!event) return;
  if (s_screen == ScreenPlaying || s_screen == ScreenPaused) {
    np_touch_handle(event);
    return;
  }
  if (s_screen != ScreenKeyboard) return;
  if (!s_keyboard_pt2) {
    t9_touch_handle(event);
    layer_mark_dirty(s_canvas);
    return;
  }
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
  // Both keyboards want the panel now: the grid for its swipes, T9 for its taps.
  bool should_subscribe = s_screen == ScreenKeyboard ||
                          s_screen == ScreenPlaying || s_screen == ScreenPaused;
  if (should_subscribe && !s_touch_subscribed) {
    touch_service_subscribe(pt2_touch_handler, NULL);
    s_touch_subscribed = true;
  } else if (!should_subscribe && s_touch_subscribed) {
    touch_service_unsubscribe();
    s_touch_subscribed = false;
    t9_cancel_hold();
    s_t9_hold_fired = false;
    s_touch_active = false;
    s_np_touching = false;
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
  const SearchResult *picked = result_at(s_selected_result);
  // The selection can name a row a paged list has since dropped. Nothing sane to play,
  // and the page carrying it is already on its way.
  if (!picked) return;
  s_now_playing = *picked;
  s_has_now_playing = true;
  // So Home leads with Now Playing when the user backs out to it.
  s_home_selection = 0;
  s_played_samples = 0;
  s_elapsed_seconds = 0;
  s_duration_seconds = 0;
  s_stream_generation++;
  send_generation_command(CommandPlay, s_now_playing.video_id, MESSAGE_KEY_VIDEO_ID);
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
    int row = place_result(index->value->int32);
    if (row < 0) return;
    copy_tuple_text(s_results[row].video_id, TEXT_LENGTH, video);
    copy_tuple_text(s_results[row].title, TEXT_LENGTH, title);
    copy_tuple_text(s_results[row].artist, TEXT_LENGTH, artist);
    // Rows land while the list is on screen during a page fetch, so the list has to
    // repaint as they arrive rather than only when the page completes.
    if (s_span_paged && s_screen == ScreenResults) layer_mark_dirty(s_canvas);
  } else if (command_tuple && command == EventSearchComplete) {
    Tuple *request = dict_find(iterator, MESSAGE_KEY_SEARCH_REQUEST_ID);
    if (request && request->value->int32 != s_search_request_id) return;
    // Both of these belong to the page, not to the screen transition below: a page
    // fetched mid-scroll completes while s_search_active is already false, and dropping
    // out before here would leave the request pending forever and freeze paging.
    Tuple *total = dict_find(iterator, MSG_SEARCH_TOTAL);
    if (total) s_span_total = total->value->int32;
    if (s_span_pending >= 0) {
      s_span_pending = -1;
      if (s_span_timer) {
        app_timer_cancel(s_span_timer);
        s_span_timer = NULL;
      }
      // A page that came back short of where it claimed to start means the list shrank
      // under us; trust the total and pull the selection back inside it.
      if (s_span_total >= 0 && s_selected_result >= s_span_total) {
        s_selected_result = s_span_total > 0 ? s_span_total - 1 : 0;
      }
      layer_mark_dirty(s_canvas);
    }
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
    // Show the placeholder the moment the transfer opens. Without this the screen only
    // learns art is coming when something else happens to repaint it, which on Home is
    // nothing at all.
    layer_mark_dirty(s_canvas);
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
    // Repaint as the bytes land, or the placeholder's progress bar is drawn once and
    // then sits still for the whole transfer. Only completion used to mark the canvas,
    // which Now Playing masked with its once-a-second redraw and Home did not.
    layer_mark_dirty(s_canvas);
    if (s_cover_art_received_bytes >= s_cover_art_expected_bytes) {
      cancel_cover_art_timeout();
      s_cover_art_receiving = false;
      s_cover_art_ready = true;
      update_cover_art_brightness();
      DW_TRACE("[CoverArt] complete bytes=%d mode=%s dark=%s",
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
    free(s_pcm_buffer);
    s_pcm_buffer = NULL;
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
      if (value > ThemeDefaultDark) value = ThemeDefaultDark;
      if (!theme_is_available((AppTheme) value)) value = ThemeDefault;
      s_theme = (AppTheme) value;
      persist_write_int(THEME_KEY, s_theme);
      menu_layer_set_highlight_colors(s_native_menu_layer, accent_color(), on_accent_color());
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
      // A track appearing while Home is showing selects the dock's Now Playing
      // entry - it leads the dock, and Home opens on "what is playing".
      if (!s_has_now_playing && s_screen == ScreenHome) s_home_selection = 0;
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
      } else if (playback_state == PlaybackIdle &&
                 (s_screen == ScreenPlaying || s_screen == ScreenPaused)) {
        // Playback stopped/ended externally while we were watching it: leave the
        // playback screen by retracing the Back history (falls back to Home).
        // ScreenBuffering is deliberately excluded: a freshly picked track can report
        // a transient idle while it spins up (the Symfonium legacy bridge does this
        // between setMediaItem and prepare), and bailing on it stranded the user on
        // the results screen while the song played on without them. A genuine start
        // failure arrives as EventError instead.
        if (!nav_back()) set_screen(ScreenHome);
        clear_playing_track();
      }
    }
    // The one exception to "snapshots never choose the screen": a quick-launch press is
    // a request for the music, not for the menu, so the first snapshot after one opens
    // Now Playing. It goes through nav_push so BACK still returns to Home, and only
    // fires while we are still on the Home screen the launch landed on - if the user
    // has already navigated somewhere in the second the handshake takes, they meant to
    // be there. A snapshot with nothing playing simply leaves us on Home.
    if (s_launch_now_playing) {
      s_launch_now_playing = false;
      bool has_track = s_has_now_playing || s_result_count > 0;
      if (s_screen == ScreenHome && has_track && playback_state != PlaybackIdle) {
        nav_push(playback_state == PlaybackPlaying ? ScreenPlaying
                 : playback_state == PlaybackBuffering ? ScreenBuffering : ScreenPaused);
        if (playback_state == PlaybackPlaying) start_progress_timer();
      }
    }
  } else if (command_tuple && command == EventLibraryItem) {
    Tuple *type = dict_find(iterator, MESSAGE_KEY_LIBRARY_TYPE);
    Tuple *index = dict_find(iterator, MESSAGE_KEY_RESULT_INDEX);
    Tuple *video = dict_find(iterator, MESSAGE_KEY_VIDEO_ID);
    Tuple *title = dict_find(iterator, MESSAGE_KEY_TITLE);
    Tuple *artist = dict_find(iterator, MESSAGE_KEY_ARTIST);
    if (!type || type->value->int32 != s_library_type || !index || !video || !title || !artist) return;
    // Through place_result() rather than straight into s_results[i]: on a paged type the
    // arriving index is a *global* one and has to be placed relative to the window. For
    // every other type this reduces to the bounds-checked direct index it replaced.
    int i = place_result(index->value->int32);
    if (i < 0) return;
    copy_tuple_text(s_results[i].video_id, TEXT_LENGTH, video);
    copy_tuple_text(s_results[i].title, TEXT_LENGTH, title);
    copy_tuple_text(s_results[i].artist, TEXT_LENGTH, artist);
    // Rows of a page fetched mid-scroll land while the list is already on screen.
    if (s_span_paged && s_screen == ScreenLibraryItems) layer_mark_dirty(s_canvas);
  } else if (command_tuple && command == EventLibraryComplete) {
    Tuple *type = dict_find(iterator, MESSAGE_KEY_LIBRARY_TYPE);
    if (!type || type->value->int32 != s_library_type) return;
    // Same handling as EventSearchComplete's: the total and the in-flight page belong to
    // the page rather than to the initial load, and a page that completes after the first
    // one would otherwise leave s_span_pending set and paging frozen.
    Tuple *total = dict_find(iterator, MSG_LIBRARY_TOTAL);
    if (total) s_span_total = total->value->int32;
    if (s_span_pending >= 0) {
      s_span_pending = -1;
      if (s_span_timer) {
        app_timer_cancel(s_span_timer);
        s_span_timer = NULL;
      }
      if (s_span_total >= 0 && s_selected_result >= s_span_total) {
        s_selected_result = s_span_total > 0 ? s_span_total - 1 : 0;
      }
    }
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
  } else if (command_tuple && command == EventSourceChanged) {
    // Phone-initiated source change. Epoch-gated the same way the audio route is: an
    // older epoch is a stale echo from before a newer local change and is ignored, and on
    // an equal epoch the watch keeps its own and re-asserts it on the next sync.
    Tuple *source = dict_find(iterator, MSG_CONFIG_MUSIC_SOURCE);
    Tuple *source_epoch = dict_find(iterator, MSG_SOURCE_EPOCH);
    if (!source) return;
    if (source_epoch && source_epoch->value->int32 <= s_source_epoch) {
      DW_TRACE("[Source] ignoring stale epoch %d (have %d)",
              (int) source_epoch->value->int32, (int) s_source_epoch);
      return;
    }
    if (source_epoch) {
      s_source_epoch = source_epoch->value->int32;
      persist_write_int(SOURCE_EPOCH_KEY, s_source_epoch);
    }
    apply_music_source(source->value->int32, false);
    menu_layer_reload_data(s_native_menu_layer);
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
    DW_TRACE("[ClaySync] Received CommandSyncSettings request from JS");
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
    DW_TRACE("[ClaySync] CONFIG_AUDIO_ROUTE=%d", s_phone_audio ? 1 : 0);
    s_route_epoch++;
    persist_write_int(ROUTE_EPOCH_KEY, s_route_epoch);
    persist_write_int(AUDIO_ROUTE_KEY, s_phone_audio);
    send_audio_route();
    config_changed = true;
  }
  if (config_watch_volume) {
    int value = config_watch_volume->value->int32;
    s_watch_volume = value < 0 ? 0 : value > 100 ? 100 : value;
    DW_TRACE("[ClaySync] CONFIG_WATCH_VOLUME=%d", s_watch_volume);
    persist_write_int(WATCH_VOLUME_KEY, s_watch_volume);
    config_changed = true;
  }
  if (config_phone_volume) {
    int value = config_phone_volume->value->int32;
    s_phone_volume = value < 0 ? 0 : value > 100 ? 100 : value;
    DW_TRACE("[ClaySync] CONFIG_PHONE_VOLUME=%d", s_phone_volume);
    persist_write_int(PHONE_VOLUME_KEY, s_phone_volume);
    send_phone_volume();
    config_changed = true;
  }
  if (config_input_mode) {
    int value = config_input_mode->value->int32;
    if (value >= InputVoice && value <= InputAsk) {
      s_input_mode = value;
      DW_TRACE("[ClaySync] CONFIG_INPUT_MODE=%d", s_input_mode);
      persist_write_int(INPUT_MODE_KEY, s_input_mode);
      config_changed = true;
    }
  }
  if (config_show_progress) {
    int mode = config_show_progress->value->int32;
    // Clamped rather than trusted: an older companion (or an older Clay page) knows
    // only 0/1 here, and anything else would be a mode this build cannot draw.
    if (mode < ProgressHide) mode = ProgressHide;
    if (mode > ProgressFlick) mode = ProgressFlick;
    s_progress_mode = (uint8_t) mode;
    DW_TRACE("[ClaySync] CONFIG_SHOW_PROGRESS=%d", s_progress_mode);
    persist_write_int(PROGRESS_MODE_KEY, s_progress_mode);
    sync_accel_subscription();
    config_changed = true;
  }
  if (config_cache_enabled) {
    s_cache_enabled = config_cache_enabled->value->int32 != 0;
    DW_TRACE("[ClaySync] CONFIG_CACHE_ENABLED=%d", s_cache_enabled ? 1 : 0);
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
    DW_TRACE("[ClaySync] CONFIG_CACHE_SIZE_MB=%d", s_cache_size_mb);
    persist_write_int(CACHE_SIZE_MB_KEY, s_cache_size_mb);
    config_changed = true;
  }
  // Cover art background is no longer a setting - the artwork is the Now Playing
  // screen's whole upper half - so an older Clay config still carrying the key is
  // read and ignored rather than allowed to switch it back off.
  if (config_theme) {
    int value = config_theme->value->int32;
    if (value < ThemeTeal) value = ThemeTeal;
    if (value > ThemeDefaultDark) value = ThemeDefaultDark;
    if (!theme_is_available((AppTheme) value)) value = ThemeDefault;
    s_theme = (AppTheme) value;
    DW_TRACE("[ClaySync] THEME=%d", (int) s_theme);
    persist_write_int(THEME_KEY, s_theme);
    menu_layer_set_highlight_colors(s_native_menu_layer, accent_color(), on_accent_color());
    menu_layer_reload_data(s_native_menu_layer);
    config_changed = true;
  }
  if (config_watch_quality) {
    s_watch_audio_quality = config_watch_quality->value->int32 != 0;
    DW_TRACE("[ClaySync] CONFIG_WATCH_AUDIO_QUALITY=%d", s_watch_audio_quality ? 1 : 0);
    persist_write_bool(WATCH_AUDIO_QUALITY_KEY, s_watch_audio_quality);
    config_changed = true;
  }
  if (config_phone_quality) {
    s_phone_audio_quality = config_phone_quality->value->int32 != 0;
    DW_TRACE("[ClaySync] CONFIG_PHONE_AUDIO_QUALITY=%d", s_phone_audio_quality ? 1 : 0);
    persist_write_bool(PHONE_AUDIO_QUALITY_KEY, s_phone_audio_quality);
    config_changed = true;
  }
  if (config_cache_radio) {
    s_cache_radio = config_cache_radio->value->int32 != 0;
    DW_TRACE("[ClaySync] CONFIG_CACHE_RADIO=%d", s_cache_radio ? 1 : 0);
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

// No confirmation popup here, unlike every other action: the artwork itself answers
// the press. Pausing veils the cover and puts a resume glyph in the middle of it, and
// resuming clears both - a full-screen change that a 72px card can only repeat, and
// which the card was landing on top of.
static void np_toggle_play_pause(void) {
  if (s_screen == ScreenPlaying) {
    send_generation_command(CommandPause, NULL, 0);
  } else if (s_screen == ScreenPaused) {
    s_stream_generation++;
    send_generation_command(CommandResume, NULL, 0);
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
  if (source_is_symfonium()) {
    // Symfonium is the authority; avoid speculative local cycling and wait for the
    // next snapshot so watch state lines up with Symfonium's own repeat indicator.
    send_command(CommandToggleLoop, NULL, 0);
    vibes_short_pulse();
    return;
  }
  // Advance locally so the badge answers the press straight away, then let the phone's
  // next snapshot confirm it. The order is not the same on both backends and the guess
  // has to match the one that will answer:
  //
  //   YouTube    Off -> One -> All   (PebblePlaybackService.toggleLoop)
  //   Symfonium  Off -> All -> One   (Symfonium's own Repeat action; see
  //                                   observeLegacyActions on the companion)
  //
  // Using one order for both is what made the badge appear to fight itself under
  // Symfonium: it jumped to our guess, and a moment later the snapshot corrected it to
  // the real mode, so a single press visibly changed the badge twice.
  if (source_is_symfonium()) {
    s_loop_mode = s_loop_mode == LoopModeOff ? LoopModeAll
                : s_loop_mode == LoopModeAll ? LoopModeOne
                                             : LoopModeOff;
  } else {
    s_loop_mode = (uint8_t) ((s_loop_mode + 1) % 3);
  }
  s_loop_enabled = s_loop_mode != LoopModeOff;
  persist_write_int(LOOP_MODE_KEY, s_loop_mode);
  send_command(CommandToggleLoop, NULL, 0);
  layer_mark_dirty(s_canvas);
}

static void np_toggle_shuffle(void) {
  if (source_is_symfonium()) {
    // Same rule as loop above: do not guess the post-toggle state locally.
    if (!request_shuffle_play()) {
      vibes_short_pulse();
      return;
    }
    vibes_short_pulse();
    return;
  }
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
    free(s_pcm_buffer);
    s_pcm_buffer = NULL;
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

// Whether a More action is on the popup at all. Three filters:
//  - Output goes away under Symfonium: that source always plays on the phone through
//    Symfonium's own player, so there is no watch-speaker route to switch to.
//  - Favorite goes away under Symfonium too. That backend exposes a favorite *action*
//    but no per-track favorite *state* (see the isFavorite = false in the Symfonium
//    snapshot), so the row could offer a toggle but never say what it was toggling
//    from - it read "Off" on every track, including ones already favorited, and the
//    heart never lit. An action that cannot show its own state is worse than absent.
//  - Queue only has a defined "up next" when Next/Previous do (see skipTrack() on the
//    companion side, mirrored by currentQueueList()) - loop-all or shuffle. Otherwise
//    there's nothing to show, so the action is hidden rather than opening an empty list.
static bool np_more_item_shown(NpMoreItem item) {
  if (item == NpMoreOutput) return !source_is_symfonium();
  if (item == NpMoreFavorite) return !source_is_symfonium();
  if (item == NpMoreQueue) return s_loop_mode == LoopModeAll || s_shuffle_enabled;
  return true;
}

static int np_more_count(void) {
  int n = 0;
  for (int i = 0; i < (int) NP_MORE_COUNT; i++) {
    if (np_more_item_shown((NpMoreItem) i)) n++;
  }
  return n;
}

// Visible row -> the action it stands for, so the gaps live in one place (as with
// Settings and Advanced).
static NpMoreItem np_more_item_at(int row) {
  for (int i = 0; i < (int) NP_MORE_COUNT; i++) {
    if (!np_more_item_shown((NpMoreItem) i)) continue;
    if (row-- == 0) return (NpMoreItem) i;
  }
  return NpMoreShuffle;
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
  switch (np_more_item_at(s_np_more_selection)) {
    case NpMoreShuffle: np_toggle_shuffle(); break;
    case NpMoreRepeat: np_cycle_loop(); break;
    case NpMoreFavorite: np_toggle_favorite(); break;
    case NpMoreOutput: np_toggle_output(); break;
    case NpMoreNewSearch: np_more_close(); np_new_search(); return;  // leaves now playing
    case NpMoreQueue: np_more_close(); open_queue(); return;         // leaves now playing
    case NP_MORE_COUNT: break;  // unreachable; keeps the enum switch exhaustive
  }
  // Toggles keep the popup open so several can be changed in a row; it reflects the new
  // state and is dismissed with BACK.
  layer_mark_dirty(s_canvas);
}

// Advance to the next available theme and bring everything that depends on it along.
// Shared by Advanced's Theme row and the long-press on Home's Settings dock icon, so the
// two cannot drift - the font, phone-sync and Home-artwork side effects below are all
// easy to forget at a second call site.
static void cycle_theme(void) {
  do {
    s_theme = s_theme == ThemeDefaultDark ? ThemeDefaultLight :
              s_theme == ThemeDefaultLight ? ThemeDefault :
              s_theme == ThemeDefault ? ThemeTeal :
              s_theme == ThemeTeal ? ThemePurple :
              s_theme == ThemePurple ? ThemeSunset :
              s_theme == ThemeSunset ? ThemeMono :
              s_theme == ThemeMono ? ThemeArcade : ThemeDefaultDark;
  } while (!theme_is_available(s_theme));
  persist_write_int(THEME_KEY, s_theme);
  // Sophie mode only applies under Mono (see ui_font), so the faces are only worth
  // holding there. The preference itself is left alone, so coming back to Mono
  // brings it back with it.
  if (s_theme == ThemeMono && s_sophie_mode) {
    sophie_fonts_load();
  } else if (s_theme != ThemeMono) {
    sophie_fonts_unload();
  }
  send_settings_sync();
  menu_layer_set_highlight_colors(s_native_menu_layer, accent_color(), on_accent_color());
  menu_layer_reload_data(s_native_menu_layer);
  // The Mono theme swaps to dedicated black & white Home art; swap it now so
  // Home is ready before it is shown, otherwise the first Home paint would
  // flash the previous background while loading.
  s_home_bg_failed_variant = -1;
  if (!s_bespoke_ui) ensure_home_background();
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
      // T9 types by finger only, which leaves SELECT free for the thing the keyboard
      // exists to do. Long SELECT still submits too, so the muscle memory the grid
      // built up keeps working here.
      t9_commit();
      if (!submit_search_query()) vibes_short_pulse();
    }
  } else if (s_screen == ScreenHome && s_bespoke_ui) {
    // The dock Home is a menu, so SELECT opens the highlighted destination using the
    // same actions ScreenMenu's own SELECT runs (MENU_ITEMS is shared between the two).
    // A stale selection can outlive the track that added the Now Playing entry.
    int row = s_home_selection;
    if (row < 0 || row >= home_dock_count()) row = 0;
    if (home_dock_is_now_playing(row)) {
      // nav_push so Back returns to Home.
      nav_push(s_playback_active ? ScreenPlaying : ScreenPaused);
      if (s_playback_active) start_progress_timer();
    } else {
      switch (home_dock_menu_index(row)) {
        case 0:
          begin_configured_search();
          break;
        case 1:
          s_menu_selection = 0;
          nav_push(ScreenLibrary);
          break;
        case 2:
          s_menu_selection = 0;
          nav_push(ScreenSettings);
          break;
        default:
          // After the push, so the stack keeps the row this menu was on and About still
          // opens with its first row highlighted.
          nav_push(ScreenAbout);
          s_menu_selection = 0;
          break;
      }
    }
  } else if (s_screen == ScreenHome || (s_screen == ScreenResults && s_result_count == 0)) {
    begin_configured_search();
  } else if (s_screen == ScreenResults) {
    if (s_native_menu_layer && screen_uses_native_menu(s_screen)) {
      s_selected_result = s_span_base + menu_layer_get_selected_index(s_native_menu_layer).row;
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
    if (menu < 0 || menu >= library_item_count()) menu = 0;
    s_library_type = LIBRARY_TYPES[library_item_id(menu)];
    span_reset();
    // After span_reset(), which clears it - this list is a window from the first request,
    // so the rows of page 0 are placed as global indices like every page after them.
    s_span_paged = library_is_paged(s_library_type);
    s_selected_result = 0;
    s_library_loading = true;
    nav_push(ScreenLibraryItems);
    if (!request_library(s_library_type)) {
      s_library_loading = false;
      s_span_paged = false;
    }
  } else if (s_screen == ScreenLibraryItems) {
    if (!s_library_loading && s_result_count > 0) {
      if (s_native_menu_layer && screen_uses_native_menu(s_screen)) {
        s_selected_result = s_span_base + menu_layer_get_selected_index(s_native_menu_layer).row;
      }
      if (s_library_type == LibraryRecentSearches) {
        // Selecting a recent search re-runs it and shows the results.
        const SearchResult *row = result_at(s_selected_result);
        if (!row || !submit_recent_search(row->video_id)) vibes_short_pulse();
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
      const SearchResult *row = result_at(s_selected_result);
      if (row) {
        send_generation_command(CommandQueueJump, row->video_id, MESSAGE_KEY_VIDEO_ID);
        nav_back();
      }
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
      nav_push(ScreenAbout);
      s_menu_selection = 0;
    }
  } else if (s_screen == ScreenAbout) {
    // SELECT opens the highlighted row. The hidden Advanced unlock used to live on this
    // press and has moved to the long press (see the long-press handler): these are
    // things you go and read, so they get the plain press and visible rows, and the
    // easter egg gets the gesture nobody hits by accident.
    // Stock About has no rows to highlight, so its SELECT keeps going straight to the
    // changelog the way it always did.
    AppScreen target = !s_bespoke_ui        ? ScreenWhatsNew
                     : s_menu_selection == 1 ? ScreenWatch
                     : s_menu_selection == 2 ? ScreenBridge
                     : s_menu_selection == 3 ? ScreenAcks
                     : ScreenWhatsNew;
    nav_push(target);
    s_menu_selection = 0;
  } else if (s_screen == ScreenWatch || s_screen == ScreenBridge) {
    // Back to the top rather than holding the pixel offset: every row below the fold
    // moves when the notes appear, so the old offset points at a different row than the
    // one that was being read. The extent itself is recomputed by end_doc_screen().
    s_stats_explain = !s_stats_explain;
    scroll_reset();
    layer_mark_dirty(s_canvas);
  } else if (s_screen == ScreenSettings) {
    // Rows are filtered per source, so the highlighted row is not the item's index.
    const int setting = settings_item_id(s_menu_selection);
    if (setting == 0) {
      s_input_mode = (InputMode) ((s_input_mode + 1) % 3);
      persist_write_int(INPUT_MODE_KEY, s_input_mode);
    } else if (setting == 1) {
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
    } else if (setting == 2) {
      s_watch_volume = s_watch_volume >= 100 ? 0 : s_watch_volume + 10;
      persist_write_int(WATCH_VOLUME_KEY, s_watch_volume);
    } else if (setting == 3) {
      uint8_t previous_volume = s_phone_volume;
      s_phone_volume = s_phone_volume >= 100 ? 0 : s_phone_volume + 10;
      if (send_phone_volume()) {
        persist_write_int(PHONE_VOLUME_KEY, s_phone_volume);
      } else {
        s_phone_volume = previous_volume;
      }
    } else if (setting == 4) {
      s_progress_mode = s_progress_mode == ProgressHide  ? ProgressShow
                      : s_progress_mode == ProgressShow  ? ProgressFlick
                                                         : ProgressHide;
      persist_write_int(PROGRESS_MODE_KEY, s_progress_mode);
      sync_accel_subscription();
    } else if (setting == 5) {
      s_back_stops = !s_back_stops;
      persist_write_bool(BACK_STOPS_KEY, s_back_stops);
    } else if (setting == 6) {
      apply_music_source(source_is_symfonium() ? MusicSourceYouTube : MusicSourceSymfonium,
                         true);
      // Hiding Output/Watch volume changes the row count under the cursor.
      if (s_menu_selection >= settings_item_count()) {
        s_menu_selection = settings_item_count() - 1;
      }
      menu_layer_reload_data(s_native_menu_layer);
    } else if (setting == 7) {
      s_symfonium_auto_shuffle = !s_symfonium_auto_shuffle;
      persist_write_bool(SYMFONIUM_AUTO_SHUFFLE_KEY, s_symfonium_auto_shuffle);
      // Pushed straight across rather than waiting for the next snapshot's settings
      // blob: the phone is the side that acts on this, and the next playlist the user
      // starts may well be the one they just turned it on for.
      send_settings_sync();
    } else if (setting == 8) {
      s_menu_selection = 0;
      nav_push(ScreenAdvanced);
      return;
    }
    layer_mark_dirty(s_canvas);
  } else if (s_screen == ScreenAdvanced) {
    // Rows are filtered per UI, so the highlighted row is not the item's index.
    const int item = advanced_item_id(s_menu_selection);
    if (item == 0) {
#ifdef PBL_PLATFORM_EMERY
      s_keyboard_pt2 = !s_keyboard_pt2;
      persist_write_bool(KEYBOARD_STYLE_KEY, s_keyboard_pt2);
#else
      vibes_short_pulse();
#endif
    } else if (item == 1) {
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
        // Arcade has no stock counterpart (see theme_is_available), so leaving the
        // bespoke UI takes the theme back to the default rather than stranding cyan
        // text on the system menu's white background.
        if (s_theme == ThemeArcade) {
          s_theme = ThemeDefaultDark;
          persist_write_int(THEME_KEY, s_theme);
          send_settings_sync();
        }
        s_home_bg_failed_variant = -1;
        ensure_home_background();
      }
      // Toggling the UI also adds or removes the two stock-Home rows below this one,
      // so the highlight cannot be left past the end of the shortened list.
      if (s_menu_selection >= advanced_item_count()) {
        s_menu_selection = advanced_item_count() - 1;
      }
      // This screen is itself a list, so swap it between the MenuLayer and the canvas
      // right now rather than leaving the old renderer on screen until the next nav.
      sync_native_menu(false);
    } else if (item == 2) {
      cycle_theme();
    } else if (item == 3) {
      s_sophie_mode = !s_sophie_mode;
      persist_write_bool(SOPHIE_MODE_KEY, s_sophie_mode);
      // Load on the way on, free on the way off: four rasterized faces is real heap,
      // and this is the only thing that decides whether any of it is needed.
      if (s_sophie_mode) {
        sophie_fonts_load();
      } else {
        sophie_fonts_unload();
      }
    } else if (item == 4) {
      s_alt_home = !s_alt_home;
      persist_write_bool(ALT_HOME_KEY, s_alt_home);
      // Swap the background now so Home is ready before it is shown, otherwise the
      // first Home paint would flash the empty (white) background while loading.
      s_home_bg_failed_variant = -1;
      if (!s_bespoke_ui) ensure_home_background();
    } else if (item == 5) {
      s_show_home_quotes = !s_show_home_quotes;
      persist_write_bool(SHOW_HOME_QUOTES_KEY, s_show_home_quotes);
    } else if (item == 6) {
      // Two grids for the two sources: YouTube's own history is shallow, so 5..20 by
      // 5; under Symfonium, Recently Played reads a smart playlist that can hold far
      // more, so 20..100 by 20. A value left over from the other source's grid snaps
      // to this grid's first step rather than stepping onto a non-grid number.
      if (source_is_symfonium()) {
        if (s_history_limit < HISTORY_LIMIT_SYMFONIUM_MIN ||
            s_history_limit % HISTORY_LIMIT_SYMFONIUM_STEP != 0) {
          s_history_limit = HISTORY_LIMIT_SYMFONIUM_MIN;
        } else {
          s_history_limit += HISTORY_LIMIT_SYMFONIUM_STEP;
          if (s_history_limit > HISTORY_LIMIT_SYMFONIUM_MAX) {
            s_history_limit = HISTORY_LIMIT_SYMFONIUM_MIN;
          }
        }
      } else {
        s_history_limit += HISTORY_LIMIT_STEP;
        if (s_history_limit > HISTORY_LIMIT_MAX) s_history_limit = HISTORY_LIMIT_MIN;
      }
      persist_write_int(HISTORY_LIMIT_KEY, s_history_limit);
    } else if (item == 7) {
      // 5 > 10 > Deep > 5. Deep is only in the cycle where it can actually be served
      // (see search_is_deep), so the row never offers a setting that would silently
      // behave as 10.
      if (s_search_limit == 5) {
        s_search_limit = 10;
      } else if (s_search_limit == 10 && source_is_symfonium() && s_bespoke_ui) {
        s_search_limit = SEARCH_LIMIT_DEEP;
      } else {
        s_search_limit = 5;
      }
      persist_write_int(SEARCH_LIMIT_KEY, s_search_limit);
    } else if (item == 8) {
      s_extra_library = !s_extra_library;
      persist_write_bool(EXTRA_LIBRARY_KEY, s_extra_library);
    } else if (item == 9) {
      s_watch_audio_quality = !s_watch_audio_quality;
      persist_write_bool(WATCH_AUDIO_QUALITY_KEY, s_watch_audio_quality);
      send_settings_sync();
    } else if (item == 10) {
      s_phone_audio_quality = !s_phone_audio_quality;
      persist_write_bool(PHONE_AUDIO_QUALITY_KEY, s_phone_audio_quality);
      send_settings_sync();
    } else if (item == 11) {
      s_cache_radio = !s_cache_radio;
      persist_write_bool(CACHE_RADIO_KEY, s_cache_radio);
      send_settings_sync();
    }
    layer_mark_dirty(s_canvas);
  } else if (s_screen == ScreenInputChoice) {
    if (s_menu_selection == 0) start_search(); else open_keyboard();
  } else if (s_screen == ScreenSearchType) {
    // Rows are display order per source, not protocol values - map through.
    s_search_mode = search_type_mode(s_menu_selection);
    continue_configured_search();
  }
}

// Whether About is the highlighted destination right now, on whichever surface is
// showing it: the bespoke Home's dock, or ScreenMenu under the stock UI. About is the
// last MENU_ITEMS entry on both.
static bool about_entry_focused(void) {
  const int about = (int) ARRAY_LENGTH(MENU_ITEMS) - 1;
  if (s_screen == ScreenMenu) return s_menu_selection == about;
  if (s_screen != ScreenHome || !s_bespoke_ui) return false;
  const int row = s_home_selection;
  if (row < 0 || row >= home_dock_count()) return false;
  return !home_dock_is_now_playing(row) && home_dock_menu_index(row) == about;
}

// Whether the bespoke Home dock has its Settings icon highlighted. Bespoke only, and
// deliberately not the stock ScreenMenu row: the dock is a row of icons you thumb
// through, so holding one is a natural thing to try, while the stock menu is a list of
// labelled destinations where a hidden gesture on one row is just hidden.
static bool settings_entry_focused(void) {
  if (s_screen != ScreenHome || !s_bespoke_ui) return false;
  const int row = s_home_selection;
  if (row < 0 || row >= home_dock_count()) return false;
  return !home_dock_is_now_playing(row) &&
         home_dock_menu_index(row) == MENU_ITEM_SETTINGS;
}

static void select_long_click(ClickRecognizerRef recognizer, void *context) {
  RESTORE_UI_IF_ARTWORK_ONLY();
  if (s_screen == ScreenKeyboard) {
    t9_commit();
    if (!submit_search_query()) vibes_short_pulse();
  } else if (s_screen == ScreenPlaying || s_screen == ScreenPaused) {
    // Long-press SELECT opens the More popup (shuffle / loop / favorite / output /
    // new search). If it is already open, ignore.
    if (!s_np_more_open) np_more_open();
  } else if (settings_entry_focused()) {
    // Hold Settings on the dock to walk the themes, without opening anything. Changing
    // theme is the one setting worth seeing applied immediately - every screen repaints
    // in it - and Home is the screen with the most of the theme on show. Reaching it
    // through Settings > Advanced > Theme meant judging each one from a list of labels.
    //
    // The vibe confirms the hold registered; the repaint is the actual feedback.
    cycle_theme();
    vibes_short_pulse();
    layer_mark_dirty(s_canvas);
  } else if (about_entry_focused()) {
    // Hidden unlock: hold SELECT on About - the dock entry under the bespoke UI, the menu
    // row under the stock one - to toggle whether Advanced shows just Keyboard or
    // everything (see s_advanced_unlocked/advanced_item_count()).
    //
    // Both surfaces, because they are not two ways of reaching one screen: the bespoke
    // Home has a dock *instead of* ScreenMenu, which never exists there. Binding this to
    // ScreenMenu alone put the unlock somewhere the default UI cannot go.
    //
    // It sits here rather than on the About screen itself because About is now somewhere
    // you go to press a button, and a screen with a visible button should not also be
    // counting secret presses. One hold rather than a run of seven: a long press on a
    // specific row is already a gesture nobody arrives at by accident, so repetition was
    // only ever guarding against a press count, not against discovery. The vibe fires
    // either way - this toggles, so the pulse means "changed" rather than "unlocked".
    s_advanced_unlocked = !s_advanced_unlocked;
    persist_write_bool(ADVANCED_UNLOCKED_KEY, s_advanced_unlocked);
    vibes_short_pulse();
  } else if (s_screen == ScreenLibraryItems && s_library_type == LibraryCached &&
             !s_library_loading && s_result_count > 0) {
    // Long-press SELECT on the Cached Music list deletes the highlighted song from
    // the on-device cache. Removed from s_results immediately (rather than waiting
    // on a round trip) so the list updates right away; sync_native_menu() re-clamps
    // the selection and flips to the empty state if that was the last cached song.
    if (s_native_menu_layer && screen_uses_native_menu(s_screen)) {
      s_selected_result = s_span_base + menu_layer_get_selected_index(s_native_menu_layer).row;
    }
    const SearchResult *row = result_at(s_selected_result);
    if (!row) return;
    send_command(CommandDeleteCached, row->video_id, MESSAGE_KEY_VIDEO_ID);
    for (int i = s_selected_result - s_span_base; i < s_result_count - 1; i++) {
      s_results[i] = s_results[i + 1];
    }
    s_result_count--;
    vibes_short_pulse();
    sync_native_menu(true);
  }
}

static void up_click(ClickRecognizerRef recognizer, void *context) {
  RESTORE_UI_IF_ARTWORK_ONLY();
  if (s_screen == ScreenAbout && s_bespoke_ui) {
    // Bespoke About is a list, so UP walks the selection and drags the view along. The
    // stock About is still a plain scrolling document and falls through.
    s_menu_selection = (s_menu_selection + ABOUT_ROW_COUNT - 1) % ABOUT_ROW_COUNT;
    about_reveal_selection();
    layer_mark_dirty(s_canvas);
  } else if (s_screen == ScreenAbout || s_screen == ScreenWhatsNew ||
             s_screen == ScreenWatch || s_screen == ScreenBridge ||
             s_screen == ScreenAcks) {
    scroll_to(s_scroll_target - 48);
  } else if ((s_screen == ScreenLibraryItems || s_screen == ScreenQueue) && s_result_count > 0) {
    // The app always drives the selection. On native-menu screens the MenuLayer is
    // stepped directly with the stock call; on canvas (bespoke) lists the app owns
    // the highlight and just repaints.
    if (screen_uses_native_menu(s_screen)) {
      native_menu_scroll_step(-1);
    } else {
      results_move(-1);
      layer_mark_dirty(s_canvas);
    }
  } else if (s_screen == ScreenKeyboard) {
    // Same on both keyboards: abc -> ABC -> 123.
    t9_commit();
    s_keyboard_mode = (s_keyboard_mode + 1) % 3;
    layer_mark_dirty(s_canvas);
  } else if (s_screen == ScreenHome) {
    if (s_bespoke_ui) {
      int count = home_dock_count();
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
      results_move(-1);
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
  if (s_screen == ScreenAbout && s_bespoke_ui) {
    // Mirror of up_click.
    {
      s_menu_selection = (s_menu_selection + 1) % ABOUT_ROW_COUNT;
      about_reveal_selection();
      layer_mark_dirty(s_canvas);
    }
  } else if (s_screen == ScreenAbout || s_screen == ScreenWhatsNew ||
             s_screen == ScreenWatch || s_screen == ScreenBridge ||
             s_screen == ScreenAcks) {
    int16_t target = s_scroll_target + 48;
    if (target > s_scroll_max) target = s_scroll_max;
    scroll_to(target);
  } else if ((s_screen == ScreenLibraryItems || s_screen == ScreenQueue) && s_result_count > 0) {
    // Same as up_click: app-driven selection, stock MenuLayer step on native menus.
    if (screen_uses_native_menu(s_screen)) {
      native_menu_scroll_step(1);
    } else {
      results_move(1);
      layer_mark_dirty(s_canvas);
    }
  } else if (s_screen == ScreenKeyboard) {
    keyboard_back_level();
  } else if (s_screen == ScreenHome) {
    if (s_bespoke_ui) {
      s_home_selection = (s_home_selection + 1) % home_dock_count();
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
      results_move(1);
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
  // BACK leaves the keyboard on both styles - deleting is DOWN's job - so any cycle
  // still open is simply committed on the way out.
  if (s_screen == ScreenKeyboard) t9_commit();

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

  // Leaving live playback/buffering. With "Back stops" on this tears the stream down,
  // which is what the app always did. With it off, Back is pure navigation: the stream,
  // the generation counter and the loaded track are all left alone, so playback carries
  // on and Home still has a hero to show. Every other way out of playback (Stop from the
  // More popup, a new search) is unaffected either way.
  if ((s_screen == ScreenPlaying || s_screen == ScreenPaused ||
       s_screen == ScreenBuffering) && s_back_stops) {
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
    // Same on both keyboards: abc -> ABC -> 123. On T9 the mode decides what the
    // focused key would type next, so a pending cycle ends here.
    t9_commit();
    s_keyboard_mode = (s_keyboard_mode + 1) % 3;
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

// Whether UP/DOWN long-press means anything on this screen. It is an action on the
// keyboard (cycle mode / back a level) and on Now Playing (previous / next track);
// everywhere else both handlers fall straight through and do nothing.
//
// Subscribing it anyway is not free, which is the bug this exists to fix: a long-click
// subscription takes the button over once its threshold passes, and the repeating click
// stops firing. Holding UP on a list scrolled for 600ms and then stuck, so long lists
// could only be walked a press at a time. Stock menus hold-to-scroll indefinitely
// because the MenuLayer subscribes no long click on those buttons.
static bool screen_uses_vertical_long_press(void) {
  return s_screen == ScreenKeyboard || s_screen == ScreenPlaying ||
         s_screen == ScreenPaused;
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 600, select_long_click, NULL);
  // 100 ms repeat matches the stock MenuLayer's own Up/Down subscription.
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, up_click);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, down_click);
  if (screen_uses_vertical_long_press()) {
    window_long_click_subscribe(BUTTON_ID_UP, 600, up_long_click, NULL);
    window_long_click_subscribe(BUTTON_ID_DOWN, 600, down_long_click, NULL);
  }
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
  // Zeroed to match the .bss semantics this used to have. On failure nav_push skips
  // recording history and Back falls through to exit, which is survivable.
  s_nav_stack = calloc(NAV_STACK_MAX, sizeof(NavEntry));
  srand(time(NULL));
  s_home_quote = rand() % ARRAY_LENGTH(HOME_QUOTES);
  if (persist_exists(THEME_KEY)) {
    int theme = persist_read_int(THEME_KEY);
    if (theme == ThemeDefault || theme == ThemePurple ||
        theme == ThemeSunset || theme == ThemeTeal || theme == ThemeMono ||
        theme == ThemeArcade || theme == ThemeDefaultLight ||
        theme == ThemeDefaultDark) {
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
  if (persist_exists(MUSIC_SOURCE_KEY)) {
    int source = persist_read_int(MUSIC_SOURCE_KEY);
    s_music_source = source == MusicSourceSymfonium ? MusicSourceSymfonium : MusicSourceYouTube;
  }
  if (persist_exists(SOURCE_EPOCH_KEY)) s_source_epoch = persist_read_int(SOURCE_EPOCH_KEY);
  if (persist_exists(ALT_HOME_KEY)) s_alt_home = persist_read_bool(ALT_HOME_KEY);
  if (persist_exists(EXTRA_LIBRARY_KEY)) s_extra_library = persist_read_bool(EXTRA_LIBRARY_KEY);
  if (persist_exists(SHOW_HOME_QUOTES_KEY)) s_show_home_quotes = persist_read_bool(SHOW_HOME_QUOTES_KEY);
  if (persist_exists(HISTORY_LIMIT_KEY)) {
    int limit = persist_read_int(HISTORY_LIMIT_KEY);
    // Snap the stored value onto the active source's grid and clamp to its range
    // (MUSIC_SOURCE_KEY loads above, so the source is settled by this point). A value
    // from the other source's grid clamps to the nearest step rather than carrying
    // over - the two grids share no step past 20.
    if (source_is_symfonium()) {
      if (limit > HISTORY_LIMIT_SYMFONIUM_MAX) limit = HISTORY_LIMIT_SYMFONIUM_MAX;
      limit = (limit / HISTORY_LIMIT_SYMFONIUM_STEP) * HISTORY_LIMIT_SYMFONIUM_STEP;
      if (limit < HISTORY_LIMIT_SYMFONIUM_MIN) limit = HISTORY_LIMIT_SYMFONIUM_MIN;
    } else {
      if (limit < HISTORY_LIMIT_MIN) limit = HISTORY_LIMIT_MIN;
      if (limit > HISTORY_LIMIT_MAX) limit = HISTORY_LIMIT_MAX;
      limit = (limit / HISTORY_LIMIT_STEP) * HISTORY_LIMIT_STEP;
      if (limit < HISTORY_LIMIT_MIN) limit = HISTORY_LIMIT_MIN;
    }
    s_history_limit = limit;
  }
  if (persist_exists(SEARCH_LIMIT_KEY)) {
    int limit = persist_read_int(SEARCH_LIMIT_KEY);
    // 0 is the Unlimited sentinel and has to survive a restart; anything else still
    // rounds to one of the two fixed counts, as it always did.
    s_search_limit = limit == SEARCH_LIMIT_DEEP ? SEARCH_LIMIT_DEEP
                                                : (limit >= 10 ? 10 : 5);
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
  if (persist_exists(BACK_STOPS_KEY)) {
    s_back_stops = persist_read_bool(BACK_STOPS_KEY);
    s_symfonium_auto_shuffle = persist_read_bool(SYMFONIUM_AUTO_SHUFFLE_KEY);
  }
  if (persist_exists(SOPHIE_MODE_KEY)) {
    s_sophie_mode = persist_read_bool(SOPHIE_MODE_KEY);
    // The faces have to exist before the first paint, or the whole app draws one
    // frame in the system font and then snaps.
    if (s_sophie_mode) sophie_fonts_load();
  }
  if (persist_exists(PROGRESS_MODE_KEY)) {
    int mode = persist_read_int(PROGRESS_MODE_KEY);
    if (mode < ProgressHide) mode = ProgressHide;
    if (mode > ProgressFlick) mode = ProgressFlick;
    s_progress_mode = (uint8_t) mode;
  } else if (persist_exists(SHOW_PROGRESS_KEY)) {
    // Carried over from before this was three-valued, so nobody who had it off comes
    // back to a rail they had turned away.
    s_progress_mode = persist_read_bool(SHOW_PROGRESS_KEY) ? ProgressShow : ProgressHide;
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
  // COVER_ART_BG_KEY is deliberately not read back. Anyone who had turned cover art
  // off before it stopped being a setting would otherwise be stuck with an empty
  // upper half and no row left in Advanced to turn it back on.
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
  // The theme was restored above, before the UI flag it depends on; settle it now that
  // both are known, so a build that ever wrote the pair inconsistently still opens on
  // a theme this UI can actually paint.
  if (!theme_is_available(s_theme)) s_theme = ThemeDefault;
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
  // Quick launch (a long press on one of the buttons from the watchface) is a shortcut
  // to the music, so it opens Now Playing rather than Home. The jump waits for the
  // first state snapshot - the companion is what knows whether anything is playing, and
  // Now Playing draws nothing at all without a track - so the app still paints Home
  // first and stays there if the answer is "nothing". See EventStateSnapshot.
  s_launch_now_playing = launch_reason() == APP_LAUNCH_QUICK_LAUNCH;
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
  // Before the first paint: every bespoke screen draws a header, so a frame without
  // these is a frame in the wrong face rather than merely an unstyled one.
  header_fonts_load();
  s_root_canvas = layer_create(GRect(0, 0, 200, 228));
  s_canvas = s_root_canvas;
  layer_set_update_proc(s_root_canvas, root_canvas_update);
  layer_add_child(window_get_root_layer(s_window), s_root_canvas);

  s_overlay_canvas = layer_create(GRect(0, 0, 200, 228));
  layer_set_update_proc(s_overlay_canvas, overlay_canvas_update);
  layer_add_child(window_get_root_layer(s_overlay_window), s_overlay_canvas);
  window_set_background_color(s_overlay_window, GColorBlack);
  s_native_menu_layer = menu_layer_create(GRect(0, 0, 200, 228));
  menu_layer_set_highlight_colors(s_native_menu_layer, accent_color(), on_accent_color());
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
  sophie_fonts_unload();
  header_fonts_unload();
  tick_timer_service_unsubscribe();
#ifdef PBL_PLATFORM_EMERY
  if (s_touch_subscribed) touch_service_unsubscribe();
  if (s_t9_hold_timer) app_timer_cancel(s_t9_hold_timer);
#endif
  stop_audio();
  if (s_volume_timer) app_timer_cancel(s_volume_timer);
  if (s_animation_timer) app_timer_cancel(s_animation_timer);
  if (s_action_bar_timer) app_timer_cancel(s_action_bar_timer);
  if (s_handshake_timer) app_timer_cancel(s_handshake_timer);
  if (s_scroll_timer) app_timer_cancel(s_scroll_timer);
  if (s_span_timer) app_timer_cancel(s_span_timer);
  if (s_t9_timer) app_timer_cancel(s_t9_timer);
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
  free(s_nav_stack);
  s_nav_stack = NULL;
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
